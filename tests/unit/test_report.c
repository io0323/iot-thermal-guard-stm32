/* report：1秒・60秒の周期と順序（スケジューラと組み合わせて、INIT を抜けた時刻を起点に確かめる） */
#include "fake_ports.h"
#include "report.h"
#include "scheduler.h"
#include "unity.h"

static report_t R;
static report_snapshot_t SNAP;

void setUp(void)
{
    report_init(&R);
    SNAP = (report_snapshot_t){.temp_c100 = 2500, .quality = 0, .state = 1, .fan_pm = 0, .fw_version = 1,
                               .boot_id = 0x11223344u};
}
void tearDown(void) {}

static size_t tick_at(uint32_t now, link_frame_t out[REPORT_MAX_FRAMES])
{
    SNAP.uptime_ms = now;
    return report_tick(&R, &SNAP, out);
}

static void test_nothing_before_start(void)
{
    link_frame_t out[REPORT_MAX_FRAMES];
    TEST_ASSERT_EQUAL_size_t(0, tick_at(5000u, out));
}

static void test_telemetry_then_heartbeat(void)
{
    link_frame_t out[REPORT_MAX_FRAMES];
    report_start(&R, 1200u);
    TEST_ASSERT_EQUAL_size_t(2, tick_at(2200u, out));
    TEST_ASSERT_EQUAL_UINT8(FRAME_TELEMETRY, out[0].type);
    TEST_ASSERT_EQUAL_UINT8(FRAME_HEARTBEAT, out[1].type);
    TEST_ASSERT_EQUAL_UINT32(2200u, out[0].u.telemetry.uptime_ms);
    TEST_ASSERT_EQUAL_INT16(2500, out[0].u.telemetry.temp_c100);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].u.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(2200u, out[1].u.heartbeat.uptime_ms);
}

static void test_state_every_60s_after_last_state(void)
{
    link_frame_t out[REPORT_MAX_FRAMES];
    uint32_t start = 1200u;
    report_start(&R, start); /* INIT を抜けたときに STATE を送った */
    int states = 0;
    for (uint32_t k = 1; k <= 130u; k++) {
        size_t n = tick_at(start + k * 1000u, out);
        if (k == 60u || k == 120u) {
            TEST_ASSERT_EQUAL_size_t(3, n);
            TEST_ASSERT_EQUAL_UINT8(FRAME_TELEMETRY, out[0].type);
            TEST_ASSERT_EQUAL_UINT8(FRAME_HEARTBEAT, out[1].type);
            TEST_ASSERT_EQUAL_UINT8(FRAME_STATE, out[2].type);
            TEST_ASSERT_EQUAL_UINT16(1, out[2].u.state.fw_version);
            TEST_ASSERT_EQUAL_HEX32(0x11223344u, out[2].u.state.boot_id);
            TEST_ASSERT_EQUAL_UINT16(1000, out[2].u.state.period_ms);
            states++;
        } else {
            TEST_ASSERT_EQUAL_size_t(2, n);
        }
    }
    TEST_ASSERT_EQUAL_INT(2, states);
}

static void test_get_status_state_does_not_move_origin(void)
{
    link_frame_t out[REPORT_MAX_FRAMES];
    link_frame_t st;
    report_start(&R, 0u);
    for (uint32_t k = 1; k < 60u; k++) {
        if (k == 30u) {
            report_make_state(&SNAP, &st); /* GET_STATUS で送った STATE */
            TEST_ASSERT_EQUAL_UINT8(FRAME_STATE, st.type);
        }
        TEST_ASSERT_EQUAL_size_t(2, tick_at(k * 1000u, out));
    }
    TEST_ASSERT_EQUAL_size_t(3, tick_at(60000u, out));
}

static void test_state_period_across_uptime_wrap(void)
{
    link_frame_t out[REPORT_MAX_FRAMES];
    uint32_t start = 0xFFFF0000u;
    report_start(&R, start);
    TEST_ASSERT_EQUAL_size_t(2, tick_at(start + 59000u, out));
    TEST_ASSERT_EQUAL_size_t(3, tick_at(start + 60000u, out)); /* 一周をまたぐ */
}

/* スケジューラの1秒周期と組み合わせる：最初の送信は INIT を抜けた時刻 +1000ms（open-questions #9） */
static report_t *s_r = &R;
static uint32_t s_sent_at[8];
static int s_sent;

static void task_report(uint32_t now)
{
    link_frame_t out[REPORT_MAX_FRAMES];
    SNAP.uptime_ms = now;
    if (report_tick(s_r, &SNAP, out) > 0u && s_sent < 8) {
        s_sent_at[s_sent++] = now;
    }
}

static void test_one_second_from_init_exit(void)
{
    fake_ports_init(NULL);
    sched_task_t t[1] = {{.fn = task_report, .period_ms = REPORT_PERIOD_MS}};
    sched_t s;
    sched_init(&s, t, 1, fake_tick_port());
    s_sent = 0;
    uint32_t exit_ms = 1137u;
    report_start(&R, exit_ms);
    sched_start(&s, 0, exit_ms + REPORT_PERIOD_MS);
    for (uint32_t now = exit_ms; now < exit_ms + 5500u; now++) {
        g_fake.now_ms = now;
        sched_run(&s);
    }
    TEST_ASSERT_EQUAL_INT(5, s_sent);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT32(exit_ms + (uint32_t)(i + 1) * 1000u, s_sent_at[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_before_start);
    RUN_TEST(test_telemetry_then_heartbeat);
    RUN_TEST(test_state_every_60s_after_last_state);
    RUN_TEST(test_get_status_state_does_not_move_origin);
    RUN_TEST(test_state_period_across_uptime_wrap);
    RUN_TEST(test_one_second_from_init_exit);
    return UNITY_END();
}
