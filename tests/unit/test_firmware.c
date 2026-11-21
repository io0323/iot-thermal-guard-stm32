/*
 * main/firmware.c の配線をフェイクのポートで確かめる（契約テストが見る挙動を手元で先に確かめる）。
 * 起動の順序、INIT 中の扱い、周期送信の位相、GET_STATUS、手動指定とリース、上位断・復帰。
 */
#include <string.h>

#include "byteorder.h"
#include "crc32.h"
#include "fake_ports.h"
#include "firmware.h"
#include "link_frame.h"
#include "tick.h"
#include "unity.h"

#define MAX_FRAMES 512

static ports_t P;
static uint32_t s_now;
static link_frame_t s_frames[MAX_FRAMES];
static uint32_t s_frame_t[MAX_FRAMES];
static size_t s_nframes;
static size_t s_parsed; /* g_fake.tx の解析済み位置 */
static uint16_t s_hb_seq;
static uint16_t s_cmd_seq;

static void parse_tx(void)
{
    size_t start = s_parsed;
    for (size_t i = s_parsed; i < g_fake.tx_len; i++) {
        if (g_fake.tx[i] == 0u) {
            link_frame_t f;
            TEST_ASSERT_EQUAL(LINK_RX_OK, link_frame_decode(&g_fake.tx[start], i - start, &f));
            TEST_ASSERT_TRUE(s_nframes < MAX_FRAMES);
            s_frame_t[s_nframes] = s_now;
            s_frames[s_nframes++] = f;
            start = i + 1u;
        }
    }
    s_parsed = start;
}

static void run_until(uint32_t t)
{
    while (tick_reached(t, s_now)) {
        g_fake.now_ms = s_now;
        firmware_poll();
        parse_tx();
        s_now++;
    }
}

static void send(const link_frame_t *f)
{
    uint8_t enc[LINK_ENCODED_MAX];
    size_t n = link_frame_encode(f, enc, sizeof enc);
    fake_uart_feed(enc, n);
}

static void send_heartbeat(void)
{
    link_frame_t f = {.type = FRAME_HEARTBEAT, .seq = s_hb_seq++};
    f.u.heartbeat.uptime_ms = s_now;
    send(&f);
}

static void send_cmd(uint16_t ref, uint8_t op, uint16_t ttl, const uint8_t *args, uint8_t n)
{
    link_frame_t f = {.type = FRAME_CMD, .seq = s_cmd_seq++};
    f.u.cmd = (link_cmd_t){.cmd_ref = ref, .op = op, .ttl_s = ttl, .args_len = n};
    for (uint8_t i = 0; i < n; i++) {
        f.u.cmd.args[i] = args[i];
    }
    send(&f);
}

static void send_fan(uint16_t ref, uint16_t duty, uint16_t ttl)
{
    uint8_t a[2];
    put_u16le(a, duty);
    send_cmd(ref, 0x01, ttl, a, 2);
}

/* since 以降で最初に pred に合うフレームの添字。なければ -1 */
static int find_from(size_t since, uint8_t type, int code)
{
    for (size_t i = since; i < s_nframes; i++) {
        if (s_frames[i].type == type && (code < 0 || (type == FRAME_EVENT && s_frames[i].u.event.code == code))) {
            return (int)i;
        }
    }
    return -1;
}

static int find_ack(size_t since, uint16_t ref)
{
    for (size_t i = since; i < s_nframes; i++) {
        if (s_frames[i].type == FRAME_ACK && s_frames[i].u.ack.cmd_ref == ref) {
            return (int)i;
        }
    }
    return -1;
}

void setUp(void)
{
    fake_ports_init(&P);
    s_now = 0u;
    s_nframes = 0u;
    s_parsed = 0u;
    s_hb_seq = 0u;
    s_cmd_seq = 0u;
    firmware_init(&P, NULL);
}
void tearDown(void) {}

/* INIT を抜けるまで回す（12サンプル目 = 1100ms） */
static void boot(void)
{
    run_until(1100u);
}

static void test_no_frames_during_init(void)
{
    run_until(1099u);
    TEST_ASSERT_EQUAL_size_t(0, g_fake.tx_len);
    TEST_ASSERT_EQUAL_UINT16(1000, g_fake.pwm_pm);
}

static void test_boot_order_state_boot_state_changed(void)
{
    boot();
    run_until(1150u);
    TEST_ASSERT_EQUAL_size_t(3, s_nframes);
    TEST_ASSERT_EQUAL_UINT8(FRAME_STATE, s_frames[0].type);
    TEST_ASSERT_EQUAL_UINT16(0, s_frames[0].seq);
    TEST_ASSERT_EQUAL_HEX16(0x0001, s_frames[0].u.state.fw_version);
    const uint8_t e[4] = {0x12, 0x34, 0x56, 0x78};
    TEST_ASSERT_EQUAL_HEX32(crc32_ieee(e, 4), s_frames[0].u.state.boot_id);
    TEST_ASSERT_EQUAL_UINT16(1000, s_frames[0].u.state.period_ms);

    TEST_ASSERT_EQUAL_UINT8(FRAME_EVENT, s_frames[1].type);
    TEST_ASSERT_EQUAL_UINT8(1, s_frames[1].u.event.code); /* BOOT */
    TEST_ASSERT_EQUAL_UINT8(0, s_frames[1].u.event.severity);
    TEST_ASSERT_EQUAL_UINT8(0, s_frames[1].u.event.from);
    TEST_ASSERT_EQUAL_UINT8(0, s_frames[1].u.event.to);
    TEST_ASSERT_EQUAL_INT16(2500, s_frames[1].u.event.temp_c100);
    TEST_ASSERT_EQUAL_UINT16(0, s_frames[1].seq);

    TEST_ASSERT_EQUAL_UINT8(FRAME_EVENT, s_frames[2].type);
    TEST_ASSERT_EQUAL_UINT8(2, s_frames[2].u.event.code); /* STATE_CHANGED */
    TEST_ASSERT_EQUAL_UINT8(0, s_frames[2].u.event.severity);
    TEST_ASSERT_EQUAL_UINT8(0, s_frames[2].u.event.from);
    TEST_ASSERT_EQUAL_UINT8(1, s_frames[2].u.event.to);
    TEST_ASSERT_EQUAL_UINT16(1, s_frames[2].seq);
    TEST_ASSERT_EQUAL_UINT16(0, g_fake.pwm_pm); /* NORMAL・25℃ は auto 0 */
}

static void test_cmd_during_init_dropped_heartbeat_accepted(void)
{
    run_until(300u);
    send_heartbeat();
    send_cmd(1, 0x10, 0, NULL, 0);
    run_until(1099u);
    TEST_ASSERT_EQUAL_size_t(0, g_fake.tx_len);
    fw_diag_t d;
    firmware_diag(&d);
    TEST_ASSERT_EQUAL_UINT16(1, d.cmd_dropped_init);
    /* HEARTBEAT を INIT 中に受けたので、起動から10秒では LOST にならない（300ms + 10s で LOST） */
    boot();
    for (uint32_t t = 2000u; t <= 10000u; t += 1000u) {
        run_until(t);
    }
    TEST_ASSERT_EQUAL_INT(-1, find_from(0, FRAME_EVENT, 7));
    run_until(10310u);
    TEST_ASSERT_TRUE(find_from(0, FRAME_EVENT, 7) >= 0);
    TEST_ASSERT_EQUAL_INT(-1, find_from(0, FRAME_EVENT, 8)); /* 最初の HEARTBEAT で RESTORED を出さない */
}

static void test_periodic_from_init_exit(void)
{
    boot();
    run_until(4200u);
    int tel[3];
    size_t k = 0u;
    for (size_t i = 0; i < s_nframes && k < 3u; i++) {
        if (s_frames[i].type == FRAME_TELEMETRY) {
            tel[k++] = (int)i;
        }
    }
    TEST_ASSERT_EQUAL_size_t(3, k);
    for (size_t j = 0; j < 3u; j++) {
        const link_frame_t *t = &s_frames[tel[j]];
        TEST_ASSERT_EQUAL_UINT32(1100u + 1000u * (uint32_t)(j + 1u), t->u.telemetry.uptime_ms);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)j, t->seq);
        TEST_ASSERT_EQUAL_INT16(2500, t->u.telemetry.temp_c100);
        TEST_ASSERT_EQUAL_UINT8(0, t->u.telemetry.quality);
        TEST_ASSERT_EQUAL_UINT8(1, t->u.telemetry.state);
        TEST_ASSERT_EQUAL_UINT16(0, t->u.telemetry.fan_pm);
        TEST_ASSERT_EQUAL_UINT8(FRAME_HEARTBEAT, s_frames[tel[j] + 1].type); /* TELEMETRY → HEARTBEAT */
    }
}

static void test_state_every_60s(void)
{
    boot();
    for (uint32_t t = 2000u; t <= 122000u; t += 1000u) {
        send_heartbeat();
        run_until(t);
    }
    int states = 0;
    for (size_t i = 0; i < s_nframes; i++) {
        if (s_frames[i].type == FRAME_STATE) {
            TEST_ASSERT_EQUAL_UINT16((uint16_t)states, s_frames[i].seq);
            states++;
        }
    }
    TEST_ASSERT_EQUAL_INT(3, states); /* 起動時・+60s・+120s */
}

static void test_get_status_state_then_ack(void)
{
    boot();
    run_until(1500u);
    size_t since = s_nframes;
    send_cmd(0x4001, 0x10, 0, NULL, 0);
    run_until(1520u);
    int st = find_from(since, FRAME_STATE, -1);
    int ack = find_ack(since, 0x4001);
    TEST_ASSERT_TRUE(st >= 0 && ack >= 0);
    TEST_ASSERT_EQUAL_INT(st + 1, ack);
    TEST_ASSERT_EQUAL_UINT8(1, s_frames[ack].u.ack.status);
    /* 重複した GET_STATUS にも STATE を送り直してから前回の ACK */
    since = s_nframes;
    send_cmd(0x4001, 0x10, 0, NULL, 0);
    run_until(1540u);
    st = find_from(since, FRAME_STATE, -1);
    ack = find_ack(since, 0x4001);
    TEST_ASSERT_TRUE(st >= 0);
    TEST_ASSERT_EQUAL_INT(st + 1, ack);
    TEST_ASSERT_EQUAL_UINT16(1, s_frames[ack].seq); /* 新しい seq */
}

static void test_fan_override_and_lease(void)
{
    boot();
    run_until(1500u);
    size_t since = s_nframes;
    send_fan(0x10, 400, 3);
    run_until(1510u);
    int ack = find_ack(since, 0x10);
    TEST_ASSERT_TRUE(ack >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, s_frames[ack].u.ack.status);
    TEST_ASSERT_EQUAL_UINT16(400, s_frames[ack].u.ack.applied);
    TEST_ASSERT_EQUAL_UINT8(0, s_frames[ack].u.ack.limited);
    TEST_ASSERT_EQUAL_UINT16(400, g_fake.pwm_pm);
    run_until(2200u);
    int tel = find_from(since, FRAME_TELEMETRY, -1);
    TEST_ASSERT_TRUE(tel >= 0);
    TEST_ASSERT_EQUAL_UINT16(400, s_frames[tel].u.telemetry.fan_pm);
    /* 1501ms に届いた CMD は 1510ms の task_link が取り出す（受信時刻）→ 期限は 4510ms ちょうど */
    run_until(4509u);
    TEST_ASSERT_EQUAL_UINT16(400, g_fake.pwm_pm);
    run_until(4510u);
    TEST_ASSERT_EQUAL_UINT16(0, g_fake.pwm_pm);
}

static void test_duplicate_fan_override_not_reexecuted(void)
{
    boot();
    run_until(1500u);
    send_fan(0x20, 700, 60);
    run_until(1510u);
    send_fan(0x21, 200, 60);
    run_until(1520u);
    size_t since = s_nframes;
    send_fan(0x20, 700, 60);
    run_until(1530u);
    int ack = find_ack(since, 0x20);
    TEST_ASSERT_TRUE(ack >= 0);
    TEST_ASSERT_EQUAL_UINT16(700, s_frames[ack].u.ack.applied);
    TEST_ASSERT_EQUAL_UINT16(200, g_fake.pwm_pm);
}

static void test_upstream_lost_and_restored(void)
{
    boot();
    for (uint32_t t = 1200u; t <= 5000u; t += 1000u) {
        send_heartbeat();
        run_until(t);
    }
    /* 最後の HEARTBEAT は 3201ms に届き、3210ms の task_link で受信 → 13210ms に LOST */
    send_fan(0x30, 900, 600);
    run_until(5010u);
    TEST_ASSERT_EQUAL_UINT16(900, g_fake.pwm_pm);
    run_until(13209u);
    TEST_ASSERT_EQUAL_INT(-1, find_from(0, FRAME_EVENT, 7));
    run_until(13210u);
    int lost = find_from(0, FRAME_EVENT, 7);
    TEST_ASSERT_TRUE(lost >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, s_frames[lost].u.event.severity);
    TEST_ASSERT_EQUAL_UINT8(1, s_frames[lost].u.event.from);
    TEST_ASSERT_EQUAL_UINT8(1, s_frames[lost].u.event.to);
    TEST_ASSERT_EQUAL_UINT16(0, g_fake.pwm_pm); /* 手動指定を解除 */
    run_until(20000u);
    TEST_ASSERT_EQUAL_INT(-1, find_from(0, FRAME_EVENT, 8));
    send_heartbeat();
    run_until(20010u);
    int restored = find_from(0, FRAME_EVENT, 8);
    TEST_ASSERT_TRUE(restored > lost);
    TEST_ASSERT_EQUAL_UINT8(0, s_frames[restored].u.event.severity);
    TEST_ASSERT_EQUAL_INT16(2500, s_frames[restored].u.event.temp_c100);
}

static void test_tx_blocked_keeps_frames_and_seq(void)
{
    g_fake.tx_full = true;
    boot();
    run_until(1200u);
    TEST_ASSERT_EQUAL_size_t(0, g_fake.tx_len);
    g_fake.tx_full = false;
    run_until(1220u);
    TEST_ASSERT_EQUAL_size_t(3, s_nframes);
    TEST_ASSERT_EQUAL_UINT8(FRAME_STATE, s_frames[0].type);
    TEST_ASSERT_EQUAL_UINT16(0, s_frames[0].seq);
}

static void test_bad_frames_counted(void)
{
    boot();
    const uint8_t junk[] = {0x05, 0x11, 0x00, 0x00};
    fake_uart_feed(junk, sizeof junk);
    run_until(1200u);
    fw_diag_t d;
    firmware_diag(&d);
    TEST_ASSERT_EQUAL_UINT16(1, d.rx_cobs_errors);
    TEST_ASSERT_EQUAL_UINT16(1, d.rx_len_errors);
    TEST_ASSERT_EQUAL_UINT8(RESET_POWER_ON, d.reset_reason);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_frames_during_init);
    RUN_TEST(test_boot_order_state_boot_state_changed);
    RUN_TEST(test_cmd_during_init_dropped_heartbeat_accepted);
    RUN_TEST(test_periodic_from_init_exit);
    RUN_TEST(test_state_every_60s);
    RUN_TEST(test_get_status_state_then_ack);
    RUN_TEST(test_fan_override_and_lease);
    RUN_TEST(test_duplicate_fan_override_not_reexecuted);
    RUN_TEST(test_upstream_lost_and_restored);
    RUN_TEST(test_tx_blocked_keeps_frames_and_seq);
    RUN_TEST(test_bad_frames_counted);
    return UNITY_END();
}
