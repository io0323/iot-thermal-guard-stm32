/* safety_fsm（仮版）：INIT → 分類(t)（行2）と、どれにも当たらない（行9）だけ。全行のテストは P2 */
#include "safety_fsm.h"
#include "unity.h"

static fsm_ctx_t C;

void setUp(void)
{
    fsm_init(&C);
}
void tearDown(void) {}

static fsm_input_t input(int16_t t, bool done)
{
    fsm_input_t in = {.init_done = done, .ack = false, .warn_c100 = 4500, .crit_c100 = 6000, .hyst_c100 = 300};
    in.temp = (temp_reading_t){.temp_c100 = t, .quality = 0, .ready = done, .fault = TEMP_FAULT_NONE};
    return in;
}

static void test_stays_in_init_until_done(void)
{
    fsm_input_t in = input(2500, false);
    fsm_output_t o;
    fsm_step(&C, &in, &o);
    TEST_ASSERT_FALSE(o.changed);
    TEST_ASSERT_EQUAL(ST_INIT, o.to);
    TEST_ASSERT_EQUAL_UINT8(EV_NONE, o.event_code);
}

static void check_exit(int16_t t, fsm_state_t to, severity_t sev)
{
    fsm_init(&C);
    fsm_input_t in = input(t, true);
    fsm_output_t o;
    fsm_step(&C, &in, &o);
    TEST_ASSERT_TRUE(o.changed);
    TEST_ASSERT_EQUAL(ST_INIT, o.from);
    TEST_ASSERT_EQUAL(to, o.to);
    TEST_ASSERT_EQUAL_UINT8(EV_STATE_CHANGED, o.event_code);
    TEST_ASSERT_EQUAL_UINT8(sev, o.severity);
}

static void test_init_exit_classifies(void)
{
    check_exit(2500, ST_NORMAL, SEV_INFO);
    check_exit(4499, ST_NORMAL, SEV_INFO);
    check_exit(4500, ST_WARNING, SEV_WARNING);
    check_exit(5999, ST_WARNING, SEV_WARNING);
    check_exit(6000, ST_CRITICAL, SEV_CRITICAL);
}

static void test_no_row_matches_after_init(void)
{
    fsm_input_t in = input(2500, true);
    fsm_output_t o;
    fsm_step(&C, &in, &o);
    fsm_step(&C, &in, &o);
    TEST_ASSERT_FALSE(o.changed);
    TEST_ASSERT_EQUAL(ST_NORMAL, o.from);
    TEST_ASSERT_EQUAL(ST_NORMAL, o.to);
    TEST_ASSERT_EQUAL_UINT8(EV_NONE, o.event_code);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stays_in_init_until_done);
    RUN_TEST(test_init_exit_classifies);
    RUN_TEST(test_no_row_matches_after_init);
    return UNITY_END();
}
