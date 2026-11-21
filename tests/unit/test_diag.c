/* diag：集める内容と飽和（第7章） */
#include "diag.h"
#include "sat.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_collect_copies_all(void)
{
    link_counters_t lc = {1u, 2u, 3u, 4u, 5u, 6u};
    sched_task_t tasks[DIAG_TASK_COUNT] = {0};
    for (size_t i = 0; i < DIAG_TASK_COUNT; i++) {
        tasks[i].max_cycles = (uint32_t)(100u + i);
    }
    sched_t s = {.tasks = tasks, .count = DIAG_TASK_COUNT, .tick = NULL, .overruns = 7u, .max_loop_cycles = 999u};
    diag_sources_t src = {RESET_WATCHDOG, 3u, &lc, &s, 8u, 9u, 10u, 11u};
    diag_t d;
    diag_collect(&d, &src);
    TEST_ASSERT_EQUAL_UINT8(RESET_WATCHDOG, d.reset_reason);
    TEST_ASSERT_EQUAL_UINT16(3u, d.reset_count);
    TEST_ASSERT_EQUAL_UINT16(1u, d.rx_cobs_errors);
    TEST_ASSERT_EQUAL_UINT16(2u, d.rx_len_errors);
    TEST_ASSERT_EQUAL_UINT16(3u, d.rx_crc_errors);
    TEST_ASSERT_EQUAL_UINT16(4u, d.rx_unexpected);
    TEST_ASSERT_EQUAL_UINT16(5u, d.rx_overflow);
    TEST_ASSERT_EQUAL_UINT16(6u, d.tx_dropped);
    TEST_ASSERT_EQUAL_UINT16(8u, d.cmd_dropped_init);
    TEST_ASSERT_EQUAL_UINT16(9u, d.cmd_duplicates);
    TEST_ASSERT_EQUAL_UINT16(10u, d.temp_clamped);
    TEST_ASSERT_EQUAL_UINT16(11u, d.temp_rejected);
    TEST_ASSERT_EQUAL_UINT32(999u, d.max_loop_cycles);
    TEST_ASSERT_EQUAL_UINT32(105u, d.max_task_cycles[5]);
    TEST_ASSERT_EQUAL_UINT16(7u, d.overruns);
}

static void test_counters_saturate(void)
{
    uint16_t c = UINT16_MAX - 1u;
    sat_inc_u16(&c);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, c);
    sat_inc_u16(&c);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, c); /* 一周させない */

    link_counters_t lc = {UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX};
    sched_task_t tasks[1] = {{NULL, 1u, 0u, false, UINT32_MAX}};
    sched_t s = {.tasks = tasks, .count = 1u, .tick = NULL, .overruns = UINT16_MAX, .max_loop_cycles = UINT32_MAX};
    diag_sources_t src = {RESET_POWER_ON, UINT16_MAX, &lc, &s, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX};
    diag_t d;
    diag_collect(&d, &src);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, d.rx_crc_errors);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, d.overruns);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, d.max_task_cycles[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, d.max_task_cycles[1]); /* 表にない行は 0 */
}

static void test_next_reset_count_saturates(void)
{
    TEST_ASSERT_EQUAL_UINT16(1u, diag_next_reset_count(0u));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, diag_next_reset_count(UINT16_MAX - 1u));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, diag_next_reset_count(UINT16_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_collect_copies_all);
    RUN_TEST(test_counters_saturate);
    RUN_TEST(test_next_reset_count_saturates);
    return UNITY_END();
}
