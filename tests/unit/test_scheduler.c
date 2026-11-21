/* scheduler：周期のずれがないこと、オーバーラン、表の順、実行時間の最大値 */
#include "fake_ports.h"
#include "scheduler.h"
#include "unity.h"

#define MAX_LOG 64

static uint32_t s_log_t[MAX_LOG];
static int s_log_id[MAX_LOG];
static int s_log_n;
static uint32_t s_extra_cycles;

static void record(int id, uint32_t now)
{
    if (s_log_n < MAX_LOG) {
        s_log_t[s_log_n] = now;
        s_log_id[s_log_n] = id;
        s_log_n++;
    }
}

static void task_a(uint32_t now)
{
    record(0, now);
    g_fake.cycles += s_extra_cycles;
}

static void task_b(uint32_t now)
{
    record(1, now);
}

static sched_task_t T[2];
static sched_t S;

void setUp(void)
{
    fake_ports_init(NULL);
    s_log_n = 0;
    s_extra_cycles = 0u;
    T[0] = (sched_task_t){.fn = task_a, .period_ms = 100u};
    T[1] = (sched_task_t){.fn = task_b, .period_ms = 100u};
    sched_init(&S, T, 2, fake_tick_port());
}
void tearDown(void) {}

static void test_period_does_not_drift(void)
{
    sched_start(&S, 0, 0u);
    /* ループが毎回 7ms 遅れて回っても、next は 0,100,200… のまま */
    for (uint32_t now = 0; now <= 1006u; now += 7u) { /* 最後は 1001ms */
        g_fake.now_ms = now;
        sched_run(&S);
    }
    TEST_ASSERT_EQUAL_INT(11, s_log_n);
    TEST_ASSERT_EQUAL_UINT32(1100u, T[0].next_ms);
    TEST_ASSERT_EQUAL_UINT16(0, S.overruns);
    for (int i = 0; i < s_log_n; i++) {
        uint32_t due = (uint32_t)i * 100u;
        TEST_ASSERT_TRUE(s_log_t[i] >= due && s_log_t[i] < due + 7u);
    }
}

static void test_overrun_runs_once_and_keeps_phase(void)
{
    sched_start(&S, 0, 0u);
    g_fake.now_ms = 0u;
    sched_run(&S);
    g_fake.now_ms = 350u; /* 100・200・300 を逃した */
    sched_run(&S);
    TEST_ASSERT_EQUAL_INT(2, s_log_n);
    TEST_ASSERT_EQUAL_UINT16(1, S.overruns);
    TEST_ASSERT_EQUAL_UINT32(400u, T[0].next_ms);
    g_fake.now_ms = 399u;
    sched_run(&S);
    TEST_ASSERT_EQUAL_INT(2, s_log_n);
    g_fake.now_ms = 400u;
    sched_run(&S);
    TEST_ASSERT_EQUAL_INT(3, s_log_n);
}

static void test_late_but_within_period_is_not_overrun(void)
{
    sched_start(&S, 0, 0u);
    g_fake.now_ms = 99u;
    sched_run(&S);
    TEST_ASSERT_EQUAL_UINT16(0, S.overruns);
    TEST_ASSERT_EQUAL_UINT32(100u, T[0].next_ms);
    g_fake.now_ms = 100u;
    sched_run(&S);
    g_fake.now_ms = 200u;
    sched_run(&S);
    TEST_ASSERT_EQUAL_UINT16(0, S.overruns);
}

static void test_same_ms_runs_in_table_order(void)
{
    sched_start(&S, 1, 0u);
    sched_start(&S, 0, 0u);
    g_fake.now_ms = 0u;
    sched_run(&S);
    TEST_ASSERT_EQUAL_INT(2, s_log_n);
    TEST_ASSERT_EQUAL_INT(0, s_log_id[0]);
    TEST_ASSERT_EQUAL_INT(1, s_log_id[1]);
}

static void test_disabled_task_does_not_run(void)
{
    sched_start(&S, 0, 0u);
    g_fake.now_ms = 0u;
    sched_run(&S);
    TEST_ASSERT_EQUAL_INT(1, s_log_n);
    TEST_ASSERT_EQUAL_INT(0, s_log_id[0]);
}

static void test_measures_max_cycles(void)
{
    sched_start(&S, 0, 0u);
    s_extra_cycles = 50u;
    g_fake.now_ms = 0u;
    sched_run(&S);
    s_extra_cycles = 500u;
    g_fake.now_ms = 100u;
    sched_run(&S);
    s_extra_cycles = 10u;
    g_fake.now_ms = 200u;
    sched_run(&S);
    TEST_ASSERT_EQUAL_UINT32(501u, T[0].max_cycles); /* cycles() 1回あたり +1 */
    TEST_ASSERT_TRUE(S.max_loop_cycles >= 501u);
}

static void test_wraps_uptime(void)
{
    sched_start(&S, 0, 0xFFFFFF9Cu); /* 一周の 100ms 前 */
    g_fake.now_ms = 0xFFFFFF9Cu;
    sched_run(&S);
    g_fake.now_ms = 0x00000000u;
    sched_run(&S);
    g_fake.now_ms = 0x00000064u;
    sched_run(&S);
    TEST_ASSERT_EQUAL_INT(3, s_log_n);
    TEST_ASSERT_EQUAL_UINT16(0, S.overruns);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_period_does_not_drift);
    RUN_TEST(test_overrun_runs_once_and_keeps_phase);
    RUN_TEST(test_late_but_within_period_is_not_overrun);
    RUN_TEST(test_same_ms_runs_in_table_order);
    RUN_TEST(test_disabled_task_does_not_run);
    RUN_TEST(test_measures_max_cycles);
    RUN_TEST(test_wraps_uptime);
    return UNITY_END();
}
