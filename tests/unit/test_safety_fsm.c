/*
 * safety_fsm：遷移表の全行、状態ごとに「どの行にも当たらない」入力、故障がほかの条件より優先されること、
 * カウンタが状態変化で戻らないこと、しきい値の変更が次のステップから効くこと
 */
#include "safety_fsm.h"
#include "unity.h"

static fsm_ctx_t C;
static fsm_output_t O;

#define WARN 4500
#define CRIT 6000
#define HYST 300

void setUp(void)
{
    fsm_init(&C);
}
void tearDown(void) {}

static fsm_input_t good(int16_t t)
{
    fsm_input_t in = {.init_done = true, .ack = false, .warn_c100 = WARN, .crit_c100 = CRIT, .hyst_c100 = HYST};
    in.temp = (temp_reading_t){.temp_c100 = t, .quality = 0, .ready = true, .fault = TEMP_FAULT_NONE, .valid_run = 50};
    return in;
}

static fsm_input_t faulty(temp_fault_t cause)
{
    fsm_input_t in = good(0);
    in.temp = (temp_reading_t){.temp_c100 = TEMP_FAULT_C100, .quality = 2, .ready = false, .fault = cause, .valid_run = 0};
    return in;
}

static void step(const fsm_input_t *in)
{
    fsm_step(&C, in, &O);
}

static void steps(const fsm_input_t *in, int n)
{
    for (int i = 0; i < n; i++) {
        step(in);
    }
}

static void expect(fsm_state_t from, fsm_state_t to, uint8_t row, event_code_t code, severity_t sev)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(from, O.from, "from");
    TEST_ASSERT_EQUAL_INT_MESSAGE(to, O.to, "to");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(row, O.row, "row");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(code, O.event_code, "code");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(sev, O.severity, "severity");
    TEST_ASSERT_EQUAL(from != to, O.changed);
    TEST_ASSERT_EQUAL_INT(to, C.state);
}

static void expect_none(fsm_state_t s)
{
    expect(s, s, 9u, EV_NONE, SEV_INFO);
}

/* 各状態に入れる（カウンタは 0 から） */
static void go(fsm_state_t s)
{
    fsm_init(&C);
    switch (s) {
    case ST_INIT:
        break;
    case ST_NORMAL:
    case ST_WARNING:
    case ST_CRITICAL: {
        int16_t t = (s == ST_NORMAL) ? 2500 : (s == ST_WARNING) ? 5000 : 6500;
        fsm_input_t in = good(t);
        step(&in);
        break;
    }
    case ST_SENSOR_FAULT: {
        fsm_input_t in = faulty(TEMP_FAULT_OPEN);
        step(&in);
        break;
    }
    default:
        break;
    }
    TEST_ASSERT_EQUAL_INT(s, C.state);
    C.warn_run = 0u;
    C.crit_run = 0u;
    C.clear_run = 0u;
}

/* ---- 行1・2（INIT） ---- */

static void test_row1_init_not_ready_at_deadline(void)
{
    fsm_input_t in = faulty(TEMP_FAULT_NONE); /* 2秒で ready にならない */
    step(&in);
    expect(ST_INIT, ST_SENSOR_FAULT, 1u, EV_SENSOR_FAULT, SEV_CRITICAL);
    go(ST_INIT);
    in = faulty(TEMP_FAULT_ADC_TIMEOUT);
    step(&in);
    expect(ST_INIT, ST_SENSOR_FAULT, 1u, EV_SENSOR_FAULT, SEV_CRITICAL);
}

static void test_row2_init_classifies(void)
{
    const struct {
        int16_t t;
        fsm_state_t to;
        severity_t sev;
    } c[] = {{2500, ST_NORMAL, SEV_INFO},       {4499, ST_NORMAL, SEV_INFO},       {4500, ST_WARNING, SEV_WARNING},
             {5999, ST_WARNING, SEV_WARNING}, {6000, ST_CRITICAL, SEV_CRITICAL}, {12000, ST_CRITICAL, SEV_CRITICAL},
             {-2000, ST_NORMAL, SEV_INFO}};
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        go(ST_INIT);
        fsm_input_t in = good(c[i].t);
        step(&in);
        expect(ST_INIT, c[i].to, 2u, EV_STATE_CHANGED, c[i].sev);
    }
}

static void test_init_no_row_until_done(void)
{
    fsm_input_t in = good(2500);
    in.init_done = false;
    in.temp.ready = false;
    steps(&in, 5);
    expect_none(ST_INIT);
    in = faulty(TEMP_FAULT_OPEN); /* INIT 中に故障しても、準備完了までは抜けない（#26） */
    in.init_done = false;
    step(&in);
    expect_none(ST_INIT);
}

/* ---- 行3：故障 ---- */

static void test_row3_fault_from_each_state(void)
{
    const fsm_state_t from[3] = {ST_NORMAL, ST_WARNING, ST_CRITICAL};
    const temp_fault_t causes[4] = {TEMP_FAULT_SHORT, TEMP_FAULT_OPEN, TEMP_FAULT_RATE, TEMP_FAULT_ADC_TIMEOUT};
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 4; k++) {
            go(from[i]);
            fsm_input_t in = faulty(causes[k]);
            step(&in);
            expect(from[i], ST_SENSOR_FAULT, 3u, EV_SENSOR_FAULT, SEV_CRITICAL);
        }
    }
}

static void test_row3_fault_has_priority(void)
{
    /* WARNING で crit の連続が10回を超えていても、故障が先 */
    go(ST_WARNING);
    C.crit_run = 20u;
    C.warn_run = 20u;
    fsm_input_t in = faulty(TEMP_FAULT_SHORT);
    step(&in);
    expect(ST_WARNING, ST_SENSOR_FAULT, 3u, EV_SENSOR_FAULT, SEV_CRITICAL);
    /* CRITICAL で承認が来ても、故障が先 */
    go(ST_CRITICAL);
    in = faulty(TEMP_FAULT_RATE);
    in.ack = true;
    step(&in);
    expect(ST_CRITICAL, ST_SENSOR_FAULT, 3u, EV_SENSOR_FAULT, SEV_CRITICAL);
    /* 原因が消えても ready に戻るまで（#23）は故障と同じ */
    go(ST_NORMAL);
    in = good(2500);
    in.temp.ready = false;
    step(&in);
    expect(ST_NORMAL, ST_SENSOR_FAULT, 3u, EV_SENSOR_FAULT, SEV_CRITICAL);
}

/* ---- 行4：SENSOR_FAULT からの復帰 ---- */

static void test_row4_recover_with_ack_and_valid_run(void)
{
    const struct {
        int16_t t;
        fsm_state_t to;
    } c[] = {{2500, ST_NORMAL}, {5000, ST_WARNING}, {6500, ST_CRITICAL}};
    for (size_t i = 0; i < 3; i++) {
        go(ST_SENSOR_FAULT);
        fsm_input_t in = good(c[i].t);
        in.ack = true;
        in.temp.valid_run = 10u;
        step(&in);
        expect(ST_SENSOR_FAULT, c[i].to, 4u, EV_SENSOR_RECOVERED, SEV_INFO);
    }
}

static void test_row4_not_without_conditions(void)
{
    go(ST_SENSOR_FAULT);
    fsm_input_t in = good(2500);
    in.ack = true;
    in.temp.valid_run = 9u; /* 正常が10回に足りない */
    step(&in);
    expect_none(ST_SENSOR_FAULT);
    in.ack = false;
    in.temp.valid_run = 200u; /* 承認がない */
    steps(&in, 30);
    expect_none(ST_SENSOR_FAULT);
    in = faulty(TEMP_FAULT_OPEN); /* 故障中の承認 */
    in.ack = true;
    step(&in);
    expect_none(ST_SENSOR_FAULT);
}

/* ---- 行5・6：しきい値の超過 ---- */

static void test_row5_critical_after_10(void)
{
    const fsm_state_t from[2] = {ST_NORMAL, ST_WARNING};
    for (int i = 0; i < 2; i++) {
        go(from[i]);
        fsm_input_t in = good(6000); /* crit ちょうど */
        steps(&in, 9);
        expect_none(from[i]);
        step(&in);
        expect(from[i], ST_CRITICAL, 5u, EV_THRESHOLD_EXCEEDED, SEV_CRITICAL);
    }
    go(ST_WARNING);
    fsm_input_t below = good(5999);
    steps(&below, 30);
    expect_none(ST_WARNING);
}

static void test_row5_before_row6_when_both_reach_10(void)
{
    go(ST_NORMAL);
    fsm_input_t in = good(6100); /* warn も crit も同時に数え始める */
    steps(&in, 9);
    expect_none(ST_NORMAL);
    step(&in);
    expect(ST_NORMAL, ST_CRITICAL, 5u, EV_THRESHOLD_EXCEEDED, SEV_CRITICAL); /* WARNING を経ない */
}

static void test_row6_warning_after_10(void)
{
    go(ST_NORMAL);
    fsm_input_t in = good(4500);
    steps(&in, 9);
    expect_none(ST_NORMAL);
    step(&in);
    expect(ST_NORMAL, ST_WARNING, 6u, EV_THRESHOLD_EXCEEDED, SEV_WARNING);
}

static void test_row6_broken_run_restarts(void)
{
    go(ST_NORMAL);
    fsm_input_t hi = good(4600);
    fsm_input_t lo = good(4499);
    steps(&hi, 9);
    step(&lo);
    steps(&hi, 9);
    expect_none(ST_NORMAL);
    step(&hi);
    expect(ST_NORMAL, ST_WARNING, 6u, EV_THRESHOLD_EXCEEDED, SEV_WARNING);
}

/* ---- 行7：WARNING → NORMAL ---- */

static void test_row7_clear_after_10_below_warn_minus_hyst(void)
{
    go(ST_WARNING);
    fsm_input_t edge = good(4200); /* warn − hyst ちょうどは数えない */
    steps(&edge, 30);
    expect_none(ST_WARNING);
    fsm_input_t in = good(4199);
    steps(&in, 9);
    expect_none(ST_WARNING);
    step(&in);
    expect(ST_WARNING, ST_NORMAL, 7u, EV_THRESHOLD_CLEARED, SEV_INFO);
}

/* ---- 行8：CRITICAL → 承認で戻る ---- */

static void test_row8_ack_below_crit_minus_hyst(void)
{
    go(ST_CRITICAL);
    fsm_input_t in = good(5699);
    in.ack = true;
    step(&in);
    expect(ST_CRITICAL, ST_WARNING, 8u, EV_THRESHOLD_CLEARED, SEV_INFO); /* 5699 ≧ 4200 → WARNING */
    go(ST_CRITICAL);
    in = good(4199);
    in.ack = true;
    step(&in);
    expect(ST_CRITICAL, ST_NORMAL, 8u, EV_THRESHOLD_CLEARED, SEV_INFO);
    go(ST_CRITICAL);
    in = good(4200);
    in.ack = true;
    step(&in);
    expect(ST_CRITICAL, ST_WARNING, 8u, EV_THRESHOLD_CLEARED, SEV_INFO);
}

static void test_row8_not_without_conditions(void)
{
    go(ST_CRITICAL);
    fsm_input_t in = good(5700); /* crit − hyst ちょうどは戻れない */
    in.ack = true;
    step(&in);
    expect_none(ST_CRITICAL);
    in = good(2500); /* 冷えても承認がなければ戻らない */
    steps(&in, 50);
    expect_none(ST_CRITICAL);
}

static void test_ack_is_consumed_each_step(void)
{
    go(ST_CRITICAL);
    fsm_input_t in = good(6500);
    in.ack = true; /* 条件未達の承認は消える */
    step(&in);
    expect_none(ST_CRITICAL);
    in = good(5000);
    step(&in); /* 承認なしで条件だけそろっても戻らない */
    expect_none(ST_CRITICAL);
}

/* ---- 行9：状態ごとに「どの行にも当たらない」 ---- */

static void test_row9_each_state(void)
{
    go(ST_INIT);
    fsm_input_t in = good(2500);
    in.init_done = false;
    steps(&in, 20);
    expect_none(ST_INIT);

    go(ST_NORMAL);
    in = good(4499);
    steps(&in, 50);
    expect_none(ST_NORMAL);

    go(ST_WARNING);
    in = good(5999); /* crit 未満、warn − hyst 以上 */
    steps(&in, 50);
    expect_none(ST_WARNING);
    in = good(4300);
    steps(&in, 50);
    expect_none(ST_WARNING);

    go(ST_CRITICAL);
    in = good(6500);
    steps(&in, 50);
    expect_none(ST_CRITICAL);

    go(ST_SENSOR_FAULT);
    in = good(2500);
    steps(&in, 50);
    expect_none(ST_SENSOR_FAULT);
    in = faulty(TEMP_FAULT_OPEN);
    steps(&in, 5);
    expect_none(ST_SENSOR_FAULT); /* 故障中に故障が続いても EVENT は出さない */
}

/* ---- カウンタの決まり（状態が変わっても戻さない） ---- */

static void test_counters_survive_state_change(void)
{
    go(ST_NORMAL);
    fsm_input_t warm = good(5000);
    fsm_input_t hot = good(6100);
    steps(&warm, 5); /* warn 5 */
    for (int k = 1; k <= 5; k++) {
        step(&hot); /* warn 6..10、crit 1..5 */
    }
    expect(ST_NORMAL, ST_WARNING, 6u, EV_THRESHOLD_EXCEEDED, SEV_WARNING);
    TEST_ASSERT_EQUAL_UINT8(5, C.crit_run); /* 状態が変わっても戻さない */
    steps(&hot, 4);
    expect_none(ST_WARNING);
    step(&hot); /* crit 10：WARNING に入ってから5ステップで CRITICAL（戻していたら10ステップ） */
    expect(ST_WARNING, ST_CRITICAL, 5u, EV_THRESHOLD_EXCEEDED, SEV_CRITICAL);
}

static void test_counters_reset_on_fault_and_not_ready(void)
{
    go(ST_SENSOR_FAULT);
    C.warn_run = 7u;
    C.crit_run = 7u;
    C.clear_run = 7u;
    fsm_input_t in = faulty(TEMP_FAULT_OPEN);
    step(&in);
    TEST_ASSERT_EQUAL_UINT8(0, C.warn_run);
    TEST_ASSERT_EQUAL_UINT8(0, C.crit_run);
    TEST_ASSERT_EQUAL_UINT8(0, C.clear_run);
    C.warn_run = 7u;
    in = good(5000);
    in.temp.ready = false; /* ready でないときも数えない（#24） */
    step(&in);
    TEST_ASSERT_EQUAL_UINT8(0, C.warn_run);
}

static void test_counters_saturate(void)
{
    go(ST_CRITICAL);
    fsm_input_t in = good(6500);
    steps(&in, 400);
    TEST_ASSERT_EQUAL_UINT8(255, C.crit_run);
    TEST_ASSERT_EQUAL_UINT8(255, C.warn_run);
}

static void test_threshold_change_next_step_keeps_counter(void)
{
    go(ST_NORMAL);
    fsm_input_t in = good(3000);
    steps(&in, 3);
    in.warn_c100 = 2500; /* 今の温度より下へ */
    steps(&in, 9);
    expect_none(ST_NORMAL);
    step(&in);
    expect(ST_NORMAL, ST_WARNING, 6u, EV_THRESHOLD_EXCEEDED, SEV_WARNING);
    /* 数えている途中でしきい値が変わってもカウンタはそのまま */
    go(ST_NORMAL);
    in = good(4600);
    steps(&in, 5);
    in.warn_c100 = 4550;
    in.crit_c100 = 6000;
    steps(&in, 4);
    expect_none(ST_NORMAL);
    step(&in);
    expect(ST_NORMAL, ST_WARNING, 6u, EV_THRESHOLD_EXCEEDED, SEV_WARNING);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_row1_init_not_ready_at_deadline);
    RUN_TEST(test_row2_init_classifies);
    RUN_TEST(test_init_no_row_until_done);
    RUN_TEST(test_row3_fault_from_each_state);
    RUN_TEST(test_row3_fault_has_priority);
    RUN_TEST(test_row4_recover_with_ack_and_valid_run);
    RUN_TEST(test_row4_not_without_conditions);
    RUN_TEST(test_row5_critical_after_10);
    RUN_TEST(test_row5_before_row6_when_both_reach_10);
    RUN_TEST(test_row6_warning_after_10);
    RUN_TEST(test_row6_broken_run_restarts);
    RUN_TEST(test_row7_clear_after_10_below_warn_minus_hyst);
    RUN_TEST(test_row8_ack_below_crit_minus_hyst);
    RUN_TEST(test_row8_not_without_conditions);
    RUN_TEST(test_ack_is_consumed_each_step);
    RUN_TEST(test_row9_each_state);
    RUN_TEST(test_counters_survive_state_change);
    RUN_TEST(test_counters_reset_on_fault_and_not_ready);
    RUN_TEST(test_counters_saturate);
    RUN_TEST(test_threshold_change_next_step_keeps_counter);
    return UNITY_END();
}
