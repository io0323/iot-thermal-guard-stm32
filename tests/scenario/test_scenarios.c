/*
 * 仮想時間のシナリオテスト（sim_harness と tests/scenario/data の CSV）。
 * NFR-01、昇温と冷却の一連、故障（断線・短絡・ADC 無応答）、急な跳び、INIT の無応答、SET_THRESHOLD。
 * P3：承認の流れ、条件未達の承認、SENSOR_FAULT からの復帰、長押し、hang による再起動。
 * sim_harness は疑似ウォッチドッグを常に見張るので、どのシナリオも health が正しくリフレッシュしていることを兼ねて確かめる。
 */
#include <stdio.h>
#include <string.h>

#include "byteorder.h"
#include "sim_harness.h"
#include "temp_sensor.h"
#include "unity.h"

#define NFR01_LIMIT_MS 2500

static scenario_t S;

void setUp(void) {}
void tearDown(void) {}

static void load(const char *name)
{
    char path[512];
    char err[160];
    (void)snprintf(path, sizeof path, "%s/%s", SCENARIO_DIR, name);
    TEST_ASSERT_TRUE_MESSAGE(scenario_load(&S, path, err, sizeof err), err);
    sim_init(&S);
}

static const link_frame_t *tx(int i)
{
    TEST_ASSERT_TRUE(i >= 0);
    return &sim_log()->tx[i].f;
}

/* 最後の TELEMETRY */
static const link_frame_t *last_telemetry(void)
{
    const sim_log_t *l = sim_log();
    for (size_t i = l->n_tx; i > 0u; i--) {
        if (l->tx[i - 1u].f.type == (uint8_t)FRAME_TELEMETRY) {
            return &l->tx[i - 1u].f;
        }
    }
    TEST_FAIL_MESSAGE("TELEMETRY がない");
    return NULL;
}

/* ---- NFR-01：crit を超えてから PWM 1000 まで 2500ms 以内 ---- */

static int64_t nfr01(const char *file, const char *label, uint32_t until)
{
    load(file);
    sim_run_until(until);
    int64_t t_cross = sim_first_input_at_least(&S, 60.0, until);
    TEST_ASSERT_TRUE(t_cross >= 0);
    TEST_ASSERT_EQUAL_INT64(-1, sim_first_state(ST_SENSOR_FAULT)); /* 変化率の上限より遅いので故障にならない */
    int64_t t_full = sim_first_pwm_at_least((uint32_t)t_cross, 1000u);
    TEST_ASSERT_TRUE_MESSAGE(t_full >= 0, "PWM が 1000 にならない");
    int64_t dt = t_full - t_cross;
    int64_t t_crit = sim_first_state(ST_CRITICAL);
    int64_t t_warn = sim_first_state(ST_WARNING);
    printf("NFR-01 %-7s: 60℃ 到達 %lld ms → WARNING %lld ms → CRITICAL %lld ms → PWM 1000 %lld ms、応答 %lld ms（上限 %d ms）\n",
           label, (long long)t_cross, (long long)t_warn, (long long)t_crit, (long long)t_full, (long long)dt,
           NFR01_LIMIT_MS);
    TEST_ASSERT_TRUE_MESSAGE(dt <= NFR01_LIMIT_MS, "NFR-01 の上限 2500ms を超えた");
    TEST_ASSERT_EQUAL_INT64(t_crit, t_full); /* CRITICAL に入ったその周期で 1000 */
    TEST_ASSERT_EQUAL_UINT16(1000, sim_pwm_now());
    return dt;
}

static void test_nfr01_1c_per_s(void)
{
    (void)nfr01("nfr01_ramp_1cps.csv", "1℃/s", 50000u);
}

static void test_nfr01_10c_per_s(void)
{
    (void)nfr01("nfr01_ramp_10cps.csv", "10℃/s", 15000u);
}

static void test_nfr01_40c_per_s(void)
{
    (void)nfr01("nfr01_ramp_40cps.csv", "40℃/s", 12000u);
}

/* ---- 昇温して警報、冷却しても CRITICAL のまま（承認がない） ---- */

static void test_alarm_cycle_stays_critical_without_ack(void)
{
    load("alarm_cycle.csv");
    sim_run_until(100000u);
    const sim_log_t *l = sim_log();
    TEST_ASSERT_EQUAL_size_t(3, l->n_state);
    TEST_ASSERT_EQUAL_INT(ST_NORMAL, l->state[0].to);
    TEST_ASSERT_EQUAL_INT(ST_WARNING, l->state[1].to);
    TEST_ASSERT_EQUAL_INT(ST_CRITICAL, l->state[2].to);
    TEST_ASSERT_EQUAL_INT(ST_CRITICAL, sim_state());
    TEST_ASSERT_EQUAL_UINT16(1000, sim_pwm_now());
    /* EVENT は 1遷移1件：STATE_CHANGED、THRESHOLD_EXCEEDED（warning）、THRESHOLD_EXCEEDED（critical） */
    TEST_ASSERT_EQUAL_size_t(2, sim_count_events(EV_THRESHOLD_EXCEEDED));
    TEST_ASSERT_EQUAL_size_t(0, sim_count_events(EV_THRESHOLD_CLEARED));
    int w = sim_find_tx(0, FRAME_EVENT, EV_THRESHOLD_EXCEEDED);
    TEST_ASSERT_EQUAL_UINT8(SEV_WARNING, tx(w)->u.event.severity);
    TEST_ASSERT_EQUAL_UINT8(ST_NORMAL, tx(w)->u.event.from);
    TEST_ASSERT_EQUAL_UINT8(ST_WARNING, tx(w)->u.event.to);
    int c = sim_find_tx((size_t)w + 1u, FRAME_EVENT, EV_THRESHOLD_EXCEEDED);
    TEST_ASSERT_EQUAL_UINT8(SEV_CRITICAL, tx(c)->u.event.severity);
    TEST_ASSERT_EQUAL_UINT8(ST_WARNING, tx(c)->u.event.from);
    TEST_ASSERT_EQUAL_UINT8(ST_CRITICAL, tx(c)->u.event.to);
    /* 最後は 25℃ まで冷えている */
    const link_frame_t *t = last_telemetry();
    TEST_ASSERT_INT16_WITHIN(20, 2500, t->u.telemetry.temp_c100);
    TEST_ASSERT_EQUAL_UINT8(ST_CRITICAL, t->u.telemetry.state);
    TEST_ASSERT_EQUAL_UINT16(1000, t->u.telemetry.fan_pm);
}

/* ---- WARNING → NORMAL はヒステリシスどおり ---- */

static void test_warning_hysteresis(void)
{
    load("warning_hysteresis.csv");
    sim_run_until(55000u); /* 42.5℃ で保持（warn − hyst = 42.00℃ 以上） */
    TEST_ASSERT_EQUAL_INT(ST_WARNING, sim_state());
    TEST_ASSERT_EQUAL_UINT16(700, sim_pwm_now()); /* WARNING の下限 */
    sim_run_until(65000u);
    TEST_ASSERT_EQUAL_INT(ST_NORMAL, sim_state());
    /* 入力が 42.00℃ を下回るのは 55000 + (42.5−42.0)/(1.5/1500) = 55500ms。そこから遅れ＋1秒以内に NORMAL */
    int64_t t_normal_after = -1;
    for (size_t i = 0; i < sim_log()->n_state; i++) {
        if (sim_log()->state[i].from == ST_WARNING && sim_log()->state[i].to == ST_NORMAL) {
            t_normal_after = (int64_t)sim_log()->state[i].t_ms;
        }
    }
    TEST_ASSERT_TRUE(t_normal_after > 55500);
    TEST_ASSERT_TRUE(t_normal_after < 55500 + 2500);
    TEST_ASSERT_EQUAL_size_t(1, sim_count_events(EV_THRESHOLD_CLEARED));
    TEST_ASSERT_EQUAL_INT64(-1, sim_first_state(ST_CRITICAL));
    TEST_ASSERT_EQUAL_UINT16(240, sim_pwm_now()); /* NORMAL・41℃ の auto：(4100−3500)×400÷1000 */
}

/* ---- 故障：断線・短絡・ADC 無応答 ---- */

static void check_fault(const char *file, uint32_t start_ms, uint32_t max_detect_ms)
{
    load(file);
    sim_run_until(start_ms - 1u);
    TEST_ASSERT_EQUAL_INT(ST_NORMAL, sim_state());
    sim_run_until(start_ms + 2000u);
    int64_t t_fault = sim_first_state(ST_SENSOR_FAULT);
    TEST_ASSERT_TRUE(t_fault >= (int64_t)start_ms);
    TEST_ASSERT_TRUE(t_fault <= (int64_t)(start_ms + max_detect_ms));
    TEST_ASSERT_EQUAL_UINT16(1000, sim_pwm_now());
    TEST_ASSERT_EQUAL_size_t(1, sim_count_events(EV_SENSOR_FAULT));
    int e = sim_find_tx(0, FRAME_EVENT, EV_SENSOR_FAULT);
    TEST_ASSERT_EQUAL_UINT8(SEV_CRITICAL, tx(e)->u.event.severity);
    TEST_ASSERT_EQUAL_UINT8(ST_NORMAL, tx(e)->u.event.from);
    TEST_ASSERT_EQUAL_UINT8(ST_SENSOR_FAULT, tx(e)->u.event.to);
    TEST_ASSERT_EQUAL_INT16(-32768, tx(e)->u.event.temp_c100);
    const link_frame_t *t = last_telemetry();
    TEST_ASSERT_EQUAL_INT16(-32768, t->u.telemetry.temp_c100);
    TEST_ASSERT_EQUAL_UINT8(2, t->u.telemetry.quality);
    TEST_ASSERT_EQUAL_UINT8(ST_SENSOR_FAULT, t->u.telemetry.state);
    TEST_ASSERT_EQUAL_UINT16(1000, t->u.telemetry.fan_pm);
    printf("故障 %-22s: 発生 %u ms → SENSOR_FAULT %lld ms（%lld ms）\n", file, start_ms, (long long)t_fault,
           (long long)(t_fault - (int64_t)start_ms));
    /* 原因が消えると温度は戻るが、承認がないので SENSOR_FAULT のまま（ボタンは P3） */
    sim_run_until(25000u);
    TEST_ASSERT_EQUAL_INT(ST_SENSOR_FAULT, sim_state());
    t = last_telemetry();
    TEST_ASSERT_EQUAL_INT16(2500, t->u.telemetry.temp_c100);
    TEST_ASSERT_EQUAL_UINT8(0, t->u.telemetry.quality);
    TEST_ASSERT_EQUAL_UINT16(1000, sim_pwm_now());
    TEST_ASSERT_EQUAL_size_t(1, sim_count_events(EV_SENSOR_FAULT));
    TEST_ASSERT_EQUAL_size_t(0, sim_count_events(EV_SENSOR_RECOVERED));
}

static void test_fault_sensor_open(void)
{
    check_fault("fault_open.csv", 5000u, 300u); /* 3サンプル */
}

static void test_fault_sensor_short(void)
{
    check_fault("fault_short.csv", 5000u, 300u);
}

static void test_fault_adc_stall(void)
{
    check_fault("fault_adc_stall.csv", 5000u, 400u); /* 最後の値から 300ms */
}

/* ---- 急な跳び：25℃ の段差で3回棄却したあと SENSOR_FAULT ---- */

static void test_jump_rejected_three_times_then_fault(void)
{
    load("jump_25c.csv");
    sim_run_until(4999u);
    fw_diag_t d;
    firmware_diag(&d);
    TEST_ASSERT_EQUAL_UINT16(0, d.temp_rejected);
    sim_run_until(7000u);
    firmware_diag(&d);
    TEST_ASSERT_EQUAL_UINT16(3, d.temp_rejected);
    TEST_ASSERT_EQUAL_INT(ST_SENSOR_FAULT, sim_state());
    TEST_ASSERT_EQUAL_INT64(-1, sim_first_state(ST_WARNING)); /* 跳んだ値は移動平均に入っていない */
    TEST_ASSERT_EQUAL_size_t(1, sim_count_events(EV_SENSOR_FAULT));
    int64_t t_fault = sim_first_state(ST_SENSOR_FAULT);
    TEST_ASSERT_TRUE(t_fault >= 5000 && t_fault <= 5600);
    printf("急な跳び 25℃→50℃     : 段差 5000 ms → 棄却 %u 回 → SENSOR_FAULT %lld ms\n", d.temp_rejected,
           (long long)t_fault);
}

/* ---- INIT：ADC が最初から無応答 → 2000ms で SENSOR_FAULT、STATE → BOOT → SENSOR_FAULT ---- */

static void test_init_adc_stall(void)
{
    load("init_adc_stall.csv");
    sim_run_until(1999u);
    TEST_ASSERT_EQUAL_size_t(0, sim_log()->n_tx);
    TEST_ASSERT_EQUAL_INT(ST_INIT, sim_state());
    sim_run_until(2100u);
    TEST_ASSERT_EQUAL_INT64(2000, sim_first_state(ST_SENSOR_FAULT));
    const sim_log_t *l = sim_log();
    TEST_ASSERT_TRUE(l->n_tx >= 3u);
    TEST_ASSERT_EQUAL_UINT8(FRAME_STATE, l->tx[0].f.type);
    TEST_ASSERT_EQUAL_UINT8(FRAME_EVENT, l->tx[1].f.type);
    TEST_ASSERT_EQUAL_UINT8(EV_BOOT, l->tx[1].f.u.event.code);
    TEST_ASSERT_EQUAL_UINT8(FRAME_EVENT, l->tx[2].f.type);
    TEST_ASSERT_EQUAL_UINT8(EV_SENSOR_FAULT, l->tx[2].f.u.event.code);
    TEST_ASSERT_EQUAL_UINT32(2000, l->tx[2].f.u.event.uptime_ms); /* send_event の引数の取り違え（#41）も検出する */
    TEST_ASSERT_EQUAL_UINT8(SEV_CRITICAL, l->tx[2].f.u.event.severity);
    TEST_ASSERT_EQUAL_UINT8(ST_INIT, l->tx[2].f.u.event.from);
    TEST_ASSERT_EQUAL_UINT8(ST_SENSOR_FAULT, l->tx[2].f.u.event.to);
    TEST_ASSERT_EQUAL_INT16(-32768, l->tx[2].f.u.event.temp_c100);
    TEST_ASSERT_EQUAL_INT64(-1, sim_first_pwm_at_least(0, 0)); /* ファンは起動時の 100% のまま一度も変えない */
    TEST_ASSERT_EQUAL_UINT16(1000, sim_pwm_now());
}

/* ---- SET_THRESHOLD：warn を今の温度より下に変えると、次のステップから10サンプル後に WARNING ---- */

static void test_set_threshold_takes_effect_next_step(void)
{
    load("constant_25c.csv");
    sim_run_until(5000u);
    TEST_ASSERT_EQUAL_INT(ST_NORMAL, sim_state());
    link_frame_t cmd = {.type = (uint8_t)FRAME_CMD};
    cmd.u.cmd.cmd_ref = 0x0301u;
    cmd.u.cmd.op = 0x03u;
    cmd.u.cmd.args_len = 6u;
    put_i16le(&cmd.u.cmd.args[0], 2000);
    put_i16le(&cmd.u.cmd.args[2], 6000);
    put_i16le(&cmd.u.cmd.args[4], 300);
    sim_send(cmd);
    sim_run_until(8000u);
    int a = sim_find_tx(0, FRAME_ACK, -1);
    TEST_ASSERT_EQUAL_UINT8(1, tx(a)->u.ack.status);
    uint32_t t_ack = sim_log()->tx[a].t_ms;
    uint32_t first_step = ((t_ack / 100u) + 1u) * 100u; /* 受信のあとの最初の task_control */
    int64_t t_warn = sim_first_state(ST_WARNING);
    printf("SET_THRESHOLD          : ACK %u ms → 数え始め %u ms → WARNING %lld ms\n", t_ack, first_step,
           (long long)t_warn);
    TEST_ASSERT_EQUAL_INT64((int64_t)first_step + 900, t_warn); /* 1回目から数えて10回目 */
    TEST_ASSERT_EQUAL_UINT16(700, sim_pwm_now());
}

/* ---- P3：ボタン・警報・ヘルス ---- */

/* t_from から t_to まで（含む）にブザーを鳴らした記録があるか */
static bool buzzer_on_between(uint32_t t_from, uint32_t t_to)
{
    const sim_log_t *l = sim_log();
    for (size_t i = 0; i < l->n_buzzer; i++) {
        if (l->buzzer[i].on && l->buzzer[i].t_ms >= t_from && l->buzzer[i].t_ms <= t_to) {
            return true;
        }
    }
    return false;
}

/* 最初の CRITICAL まで進め、ブザーが鳴っていることを確かめる */
static void reach_critical(uint32_t until)
{
    sim_run_until(until);
    TEST_ASSERT_TRUE(sim_first_state(ST_CRITICAL) >= 0);
    TEST_ASSERT_EQUAL_INT(ST_CRITICAL, sim_state());
}

static void test_ack_flow_silence_then_ack(void)
{
    load("ack_flow.csv");
    reach_critical(34990u);
    TEST_ASSERT_TRUE(buzzer_on_between(34000u, 34990u)); /* 冷えても鳴り続けている */
    sim_run_until(35300u);                               /* 1回目の短押し（35200 に離す） */
    TEST_ASSERT_FALSE(sim_buzzer());
    sim_run_until(36990u);
    TEST_ASSERT_EQUAL_INT(ST_CRITICAL, sim_state()); /* 消音だけで、承認ではない */
    TEST_ASSERT_FALSE(buzzer_on_between(35220u, 36990u));
    sim_run_until(42000u); /* 2回目の短押し（37200 に離す）で承認 */
    int64_t t_back = sim_first_state(ST_NORMAL);
    const sim_log_t *l = sim_log();
    int64_t t_back_after = -1;
    for (size_t i = 0; i < l->n_state; i++) {
        if (l->state[i].from == ST_CRITICAL) {
            t_back_after = (int64_t)l->state[i].t_ms;
            TEST_ASSERT_EQUAL_INT(ST_NORMAL, l->state[i].to); /* 30℃ は warn 未満なので NORMAL */
        }
    }
    printf("承認の流れ             : CRITICAL %lld ms、消音 35200 ms に離す、承認 37200 ms に離す → NORMAL %lld ms\n",
           (long long)sim_first_state(ST_CRITICAL), (long long)t_back_after);
    TEST_ASSERT_TRUE(t_back >= 0);
    TEST_ASSERT_TRUE(t_back_after > 37200 && t_back_after <= 37300); /* 離しの確定のあとの最初の task_control */
    TEST_ASSERT_EQUAL_size_t(1, sim_count_events(EV_THRESHOLD_CLEARED));
    TEST_ASSERT_FALSE(sim_buzzer());
    TEST_ASSERT_TRUE(sim_led(LED_GREEN));
}

static void test_ack_not_met_stays_critical(void)
{
    load("ack_not_met.csv");
    reach_critical(29990u);
    sim_run_until(31000u);
    TEST_ASSERT_FALSE(sim_buzzer()); /* 1回目は消音 */
    sim_run_until(34000u);           /* 2回目は承認だが、58℃ ≧ crit−hyst（57℃）なので行8に当たらない */
    TEST_ASSERT_EQUAL_INT(ST_CRITICAL, sim_state());
    sim_run_until(50000u); /* 30℃ まで冷えても、承認は持ち越さないので CRITICAL のまま */
    TEST_ASSERT_EQUAL_INT(ST_CRITICAL, sim_state());
    TEST_ASSERT_EQUAL_size_t(0, sim_count_events(EV_THRESHOLD_CLEARED));
    TEST_ASSERT_EQUAL_UINT16(1000, sim_pwm_now());
    printf("条件未達の承認         : 58℃ で承認（32200 ms に離す）→ CRITICAL のまま、30℃ に冷えても戻らない\n");
}

static void test_sensor_fault_early_ack_is_invalid(void)
{
    load("fault_recover_early_ack.csv");
    sim_run_until(8250u);
    TEST_ASSERT_EQUAL_INT(ST_SENSOR_FAULT, sim_state());
    TEST_ASSERT_FALSE(sim_buzzer()); /* 1回目（8200 に離し、8210 に確定）は消音 */
    sim_run_until(8499u);            /* 2回目（8400 に離す）の承認は 8500 の task_control で渡る */
    temp_reading_t r;
    temp_get(&r);
    TEST_ASSERT_TRUE(r.valid_run < 10u);
    sim_run_until(15000u);
    temp_get(&r);
    TEST_ASSERT_TRUE(r.valid_run >= 10u); /* 条件はそろったが、承認は持ち越さない */
    TEST_ASSERT_EQUAL_INT(ST_SENSOR_FAULT, sim_state());
    TEST_ASSERT_EQUAL_size_t(0, sim_count_events(EV_SENSOR_RECOVERED));
    printf("SENSOR_FAULT（早い承認）: valid_run < 10 で承認 → SENSOR_FAULT のまま\n");
}

static void test_sensor_fault_recover_silence_then_ack(void)
{
    load("fault_recover_ack.csv");
    sim_run_until(10990u);
    TEST_ASSERT_EQUAL_INT(ST_SENSOR_FAULT, sim_state());
    TEST_ASSERT_TRUE(sim_buzzer()); /* 連続音 */
    temp_reading_t r;
    temp_get(&r);
    TEST_ASSERT_TRUE(r.ready && r.valid_run >= 10u);
    sim_run_until(11300u);
    TEST_ASSERT_FALSE(sim_buzzer()); /* 1回目は消音 */
    TEST_ASSERT_EQUAL_INT(ST_SENSOR_FAULT, sim_state());
    sim_run_until(15000u);
    const sim_log_t *l = sim_log();
    int64_t t_rec = -1;
    for (size_t i = 0; i < l->n_state; i++) {
        if (l->state[i].from == ST_SENSOR_FAULT) {
            t_rec = (int64_t)l->state[i].t_ms;
            TEST_ASSERT_EQUAL_INT(ST_NORMAL, l->state[i].to); /* 25℃ の分類先 */
        }
    }
    printf("SENSOR_FAULT からの復帰 : 消音 11200 ms に離す、承認 12200 ms に離す → NORMAL %lld ms\n", (long long)t_rec);
    TEST_ASSERT_TRUE(t_rec > 12200 && t_rec <= 12300);
    TEST_ASSERT_EQUAL_size_t(1, sim_count_events(EV_SENSOR_RECOVERED));
    int e = sim_find_tx(0, FRAME_EVENT, EV_SENSOR_RECOVERED);
    TEST_ASSERT_EQUAL_UINT8(ST_SENSOR_FAULT, tx(e)->u.event.from);
    TEST_ASSERT_EQUAL_UINT8(ST_NORMAL, tx(e)->u.event.to);
    TEST_ASSERT_EQUAL_UINT16(0, sim_pwm_now());
}

/* t_from から t_to まで（含む）の緑の点灯の切り替わりを数える */
static int green_toggles(uint32_t t_from, uint32_t t_to)
{
    int n = 0;
    bool prev = sim_led(LED_GREEN);
    for (uint32_t t = t_from; t <= t_to; t += 10u) {
        sim_run_until(t);
        if (sim_led(LED_GREEN) != prev) {
            n++;
            prev = sim_led(LED_GREEN);
        }
    }
    return n;
}

static void test_long_press_toggles_force_full(void)
{
    load("long_press.csv");
    sim_run_until(4990u);
    TEST_ASSERT_EQUAL_INT(ST_NORMAL, sim_state());
    TEST_ASSERT_EQUAL_UINT16(0, sim_pwm_now());
    int64_t t_on = -1;
    sim_run_until(8500u);
    t_on = sim_first_pwm_at_least(5000u, 1000u);
    TEST_ASSERT_TRUE(t_on >= 0);
    TEST_ASSERT_EQUAL_INT64(8010, t_on); /* 押しの確定（5010）から3000ms。離すのを待たない */
    int toggles = green_toggles(8600u, 10590u); /* 2Hz：2秒で8回切り替わる */
    TEST_ASSERT_EQUAL_INT(8, toggles);
    sim_run_until(11990u);
    TEST_ASSERT_EQUAL_UINT16(1000, sim_pwm_now()); /* 離しても戻らない（短押しにもならない） */
    TEST_ASSERT_EQUAL_INT(ST_NORMAL, sim_state());
    sim_run_until(20000u);
    const sim_log_t *l = sim_log();
    int64_t t_off = -1;
    for (size_t i = 0; i < l->n_pwm; i++) {
        if (l->pwm[i].t_ms > 12000u && l->pwm[i].pm == 0u) {
            t_off = (int64_t)l->pwm[i].t_ms;
            break;
        }
    }
    printf("長押し                 : 5000 ms に押す → 強制全開 %lld ms、12000 ms に押す → 解除 %lld ms\n",
           (long long)t_on, (long long)t_off);
    TEST_ASSERT_EQUAL_INT64(15010, t_off);
    TEST_ASSERT_TRUE(sim_led(LED_GREEN)); /* 点灯に戻る */
    TEST_ASSERT_EQUAL_INT(0, green_toggles(19000u, 20000u));
    TEST_ASSERT_EQUAL_INT(1, (int)sim_log()->n_state); /* INIT → NORMAL だけ */
}

static void test_long_press_neither_silences_nor_acks(void)
{
    load("long_press_critical.csv");
    reach_critical(29990u);
    sim_run_until(40000u);
    TEST_ASSERT_TRUE(buzzer_on_between(33600u, 40000u)); /* 長押しのあとも鳴っている */
    TEST_ASSERT_EQUAL_INT(ST_CRITICAL, sim_state());     /* 30℃ でも承認にならない */
    TEST_ASSERT_TRUE(firmware_force_full());
    TEST_ASSERT_EQUAL_UINT16(1000, sim_pwm_now());
}

static void test_hang_watchdog_reboot(void)
{
    load("hang_reboot.csv");
    sim_run_until(6000u);
    const sim_log_t *l = sim_log();
    TEST_ASSERT_EQUAL_size_t(2, l->n_boot);
    TEST_ASSERT_EQUAL_INT(RESET_POWER_ON, l->boot[0].reason);
    TEST_ASSERT_EQUAL_UINT16(0, l->boot[0].count);
    TEST_ASSERT_EQUAL_INT(RESET_WATCHDOG, l->boot[1].reason);
    TEST_ASSERT_EQUAL_UINT16(1, l->boot[1].count);
    TEST_ASSERT_NOT_EQUAL(l->boot[0].boot_id, l->boot[1].boot_id);
    uint32_t t_reset = l->boot[1].t_ms;
    /* 3000ms のループは hang で止まるので、最後のリフレッシュは 2900ms の task_health。そこから500ms */
    TEST_ASSERT_EQUAL_UINT32(3400u, t_reset);
    for (size_t i = 0; i < l->n_tx; i++) {
        TEST_ASSERT_FALSE(l->tx[i].t_ms >= 3000u && l->tx[i].t_ms < t_reset); /* hang の間は何も送らない */
    }
    int s = -1;
    for (size_t i = 0; i < l->n_tx; i++) {
        if (l->tx[i].t_ms >= t_reset && l->tx[i].f.type == (uint8_t)FRAME_STATE) {
            s = (int)i;
            break;
        }
    }
    TEST_ASSERT_TRUE(s >= 0);
    TEST_ASSERT_EQUAL_UINT16(0, tx(s)->seq);
    TEST_ASSERT_EQUAL_UINT32(l->boot[1].boot_id, tx(s)->u.state.boot_id);
    TEST_ASSERT_EQUAL_UINT8(FRAME_EVENT, tx(s + 1)->type);
    TEST_ASSERT_EQUAL_UINT8(EV_BOOT, tx(s + 1)->u.event.code);
    TEST_ASSERT_EQUAL_UINT16(0, tx(s + 1)->seq);
    TEST_ASSERT_TRUE(tx(s + 1)->u.event.uptime_ms < 2000u); /* uptime_ms は 0 から数え直す */
    printf("hang（仮想時間）        : hang 3000 ms → 再起動 %u ms → STATE・BOOT %u ms（reset_reason watchdog、回数 1）\n",
           t_reset, sim_log()->tx[s].t_ms);
    TEST_ASSERT_EQUAL_INT(ST_NORMAL, sim_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nfr01_1c_per_s);
    RUN_TEST(test_nfr01_10c_per_s);
    RUN_TEST(test_nfr01_40c_per_s);
    RUN_TEST(test_alarm_cycle_stays_critical_without_ack);
    RUN_TEST(test_warning_hysteresis);
    RUN_TEST(test_fault_sensor_open);
    RUN_TEST(test_fault_sensor_short);
    RUN_TEST(test_fault_adc_stall);
    RUN_TEST(test_jump_rejected_three_times_then_fault);
    RUN_TEST(test_init_adc_stall);
    RUN_TEST(test_set_threshold_takes_effect_next_step);
    RUN_TEST(test_ack_flow_silence_then_ack);
    RUN_TEST(test_ack_not_met_stays_critical);
    RUN_TEST(test_sensor_fault_early_ack_is_invalid);
    RUN_TEST(test_sensor_fault_recover_silence_then_ack);
    RUN_TEST(test_long_press_toggles_force_full);
    RUN_TEST(test_long_press_neither_silences_nor_acks);
    RUN_TEST(test_hang_watchdog_reboot);
    return UNITY_END();
}
