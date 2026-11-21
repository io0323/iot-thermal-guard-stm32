/* upstream：3状態の全遷移、起動から10秒、最初の HEARTBEAT で RESTORED を出さない */
#include "unity.h"
#include "upstream.h"

static upstream_t U;

void setUp(void)
{
    upstream_init(&U, 1000u);
}
void tearDown(void) {}

static void test_starts_unknown(void)
{
    TEST_ASSERT_EQUAL(UPSTREAM_UNKNOWN, upstream_state(&U));
}

static void test_unknown_heartbeat_to_alive_without_event(void)
{
    TEST_ASSERT_EQUAL(UPSTREAM_EV_NONE, upstream_on_heartbeat(&U, 1500u));
    TEST_ASSERT_EQUAL(UPSTREAM_ALIVE, upstream_state(&U));
}

static void test_unknown_lost_10s_after_boot(void)
{
    TEST_ASSERT_EQUAL(UPSTREAM_EV_NONE, upstream_tick(&U, 1000u + 9999u));
    TEST_ASSERT_EQUAL(UPSTREAM_UNKNOWN, upstream_state(&U));
    TEST_ASSERT_EQUAL(UPSTREAM_EV_LOST, upstream_tick(&U, 1000u + 10000u));
    TEST_ASSERT_EQUAL(UPSTREAM_LOST, upstream_state(&U));
}

static void test_alive_updates_time(void)
{
    (void)upstream_on_heartbeat(&U, 2000u);
    TEST_ASSERT_EQUAL(UPSTREAM_EV_NONE, upstream_on_heartbeat(&U, 9000u));
    TEST_ASSERT_EQUAL(UPSTREAM_EV_NONE, upstream_tick(&U, 18999u));
    TEST_ASSERT_EQUAL(UPSTREAM_ALIVE, upstream_state(&U));
    TEST_ASSERT_EQUAL(UPSTREAM_EV_LOST, upstream_tick(&U, 19000u));
}

static void test_lost_tick_does_nothing(void)
{
    (void)upstream_tick(&U, 11000u);
    TEST_ASSERT_EQUAL(UPSTREAM_EV_NONE, upstream_tick(&U, 12000u));
    TEST_ASSERT_EQUAL(UPSTREAM_EV_NONE, upstream_tick(&U, 50000u));
    TEST_ASSERT_EQUAL(UPSTREAM_LOST, upstream_state(&U));
}

static void test_lost_heartbeat_restored(void)
{
    (void)upstream_on_heartbeat(&U, 2000u);
    TEST_ASSERT_EQUAL(UPSTREAM_EV_LOST, upstream_tick(&U, 12000u));
    TEST_ASSERT_EQUAL(UPSTREAM_EV_RESTORED, upstream_on_heartbeat(&U, 15000u));
    TEST_ASSERT_EQUAL(UPSTREAM_ALIVE, upstream_state(&U));
    /* 戻った時刻から改めて10秒 */
    TEST_ASSERT_EQUAL(UPSTREAM_EV_NONE, upstream_tick(&U, 24999u));
    TEST_ASSERT_EQUAL(UPSTREAM_EV_LOST, upstream_tick(&U, 25000u));
}

static void test_wraps_uptime(void)
{
    upstream_init(&U, 0xFFFFF000u);
    TEST_ASSERT_EQUAL(UPSTREAM_EV_NONE, upstream_tick(&U, 0x00000100u)); /* 一周をまたいで約4.3秒 */
    TEST_ASSERT_EQUAL(UPSTREAM_EV_LOST, upstream_tick(&U, 0xFFFFF000u + 10000u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_unknown);
    RUN_TEST(test_unknown_heartbeat_to_alive_without_event);
    RUN_TEST(test_unknown_lost_10s_after_boot);
    RUN_TEST(test_alive_updates_time);
    RUN_TEST(test_lost_tick_does_nothing);
    RUN_TEST(test_lost_heartbeat_restored);
    RUN_TEST(test_wraps_uptime);
    return UNITY_END();
}
