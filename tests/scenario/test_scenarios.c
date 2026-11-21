/*
 * 仮想時間のシナリオテスト（sim_harness と tests/scenario/data の CSV）。
 * NFR-01、昇温と冷却の一連、故障（断線・短絡・ADC 無応答）、急な跳び、INIT の無応答、SET_THRESHOLD。
 * 承認による復帰はボタンが P3 なので、ここでは確かめない（単体テストで確かめる）。
 */
#include <stdio.h>
#include <string.h>

#include "byteorder.h"
#include "sim_harness.h"
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
    return UNITY_END();
}
