/* event_map：基本設計書 第8章の表の全行と、表にない遷移 */
#include "event_map.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void check(fsm_state_t from, fsm_state_t to, event_code_t code, severity_t sev)
{
    uint8_t c = 99u;
    uint8_t s = 99u;
    TEST_ASSERT_TRUE(event_map(from, to, &c, &s));
    TEST_ASSERT_EQUAL_UINT8(code, c);
    TEST_ASSERT_EQUAL_UINT8(sev, s);
}

static void test_threshold_exceeded(void)
{
    check(ST_NORMAL, ST_WARNING, EV_THRESHOLD_EXCEEDED, SEV_WARNING);
    check(ST_NORMAL, ST_CRITICAL, EV_THRESHOLD_EXCEEDED, SEV_CRITICAL);
    check(ST_WARNING, ST_CRITICAL, EV_THRESHOLD_EXCEEDED, SEV_CRITICAL);
}

static void test_threshold_cleared(void)
{
    check(ST_WARNING, ST_NORMAL, EV_THRESHOLD_CLEARED, SEV_INFO);
    check(ST_CRITICAL, ST_WARNING, EV_THRESHOLD_CLEARED, SEV_INFO);
    check(ST_CRITICAL, ST_NORMAL, EV_THRESHOLD_CLEARED, SEV_INFO);
}

static void test_sensor_fault_from_any_state(void)
{
    check(ST_INIT, ST_SENSOR_FAULT, EV_SENSOR_FAULT, SEV_CRITICAL);
    check(ST_NORMAL, ST_SENSOR_FAULT, EV_SENSOR_FAULT, SEV_CRITICAL);
    check(ST_WARNING, ST_SENSOR_FAULT, EV_SENSOR_FAULT, SEV_CRITICAL);
    check(ST_CRITICAL, ST_SENSOR_FAULT, EV_SENSOR_FAULT, SEV_CRITICAL);
}

static void test_sensor_recovered(void)
{
    check(ST_SENSOR_FAULT, ST_NORMAL, EV_SENSOR_RECOVERED, SEV_INFO);
    check(ST_SENSOR_FAULT, ST_WARNING, EV_SENSOR_RECOVERED, SEV_INFO);
    check(ST_SENSOR_FAULT, ST_CRITICAL, EV_SENSOR_RECOVERED, SEV_INFO);
}

static void test_init_state_changed_follows_destination(void)
{
    check(ST_INIT, ST_NORMAL, EV_STATE_CHANGED, SEV_INFO);
    check(ST_INIT, ST_WARNING, EV_STATE_CHANGED, SEV_WARNING);
    check(ST_INIT, ST_CRITICAL, EV_STATE_CHANGED, SEV_CRITICAL);
}

static void test_not_in_table(void)
{
    const fsm_state_t all[5] = {ST_INIT, ST_NORMAL, ST_WARNING, ST_CRITICAL, ST_SENSOR_FAULT};
    for (int i = 0; i < 5; i++) {
        uint8_t c = 99u;
        uint8_t s = 99u;
        TEST_ASSERT_FALSE(event_map(all[i], all[i], &c, &s)); /* 変化なし */
        TEST_ASSERT_EQUAL_UINT8(EV_NONE, c);
        TEST_ASSERT_FALSE(event_map(all[i], ST_INIT, &c, &s)); /* INIT へは戻らない */
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_threshold_exceeded);
    RUN_TEST(test_threshold_cleared);
    RUN_TEST(test_sensor_fault_from_any_state);
    RUN_TEST(test_sensor_recovered);
    RUN_TEST(test_init_state_changed_follows_destination);
    RUN_TEST(test_not_in_table);
    return UNITY_END();
}
