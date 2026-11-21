/*
 * command_handler：表の全検証条件、重複キャッシュの押し出し、rejected もキャッシュ、リースの期限ちょうど、
 * INIT 中は捨てる、状態の下限での limited、GET_STATUS は STATE → ACK（重複でも STATE を送り直す）
 */
#include "byteorder.h"
#include "command_handler.h"
#include "unity.h"

static cmd_handler_t H;
static cmd_env_t ENV;
static cmd_result_t RES;

void setUp(void)
{
    cmd_init(&H);
    ENV = (cmd_env_t){.state = ST_NORMAL, .temp_c100 = 2500, .force_full = false};
}
void tearDown(void) {}

static link_cmd_t mk(uint16_t ref, uint8_t op, uint16_t ttl, const uint8_t *args, uint8_t n)
{
    link_cmd_t c = {.cmd_ref = ref, .op = op, .ttl_s = ttl, .args_len = n};
    for (uint8_t i = 0; i < n; i++) {
        c.args[i] = args[i];
    }
    return c;
}

static link_cmd_t fan(uint16_t ref, uint16_t duty, uint16_t ttl)
{
    uint8_t a[2];
    put_u16le(a, duty);
    return mk(ref, OP_SET_FAN_OVERRIDE, ttl, a, 2);
}

static link_cmd_t thr(uint16_t ref, int16_t w, int16_t c, int16_t h)
{
    uint8_t a[6];
    put_i16le(&a[0], w);
    put_i16le(&a[2], c);
    put_i16le(&a[4], h);
    return mk(ref, OP_SET_THRESHOLD, 0, a, 6);
}

static void handle(const link_cmd_t *c, uint32_t now)
{
    cmd_handle(&H, c, now, &ENV, &RES);
}

static void expect_ack(uint16_t ref, uint8_t st, ack_reason_t reason, uint8_t limited, uint16_t applied)
{
    TEST_ASSERT_TRUE(RES.has_ack);
    TEST_ASSERT_EQUAL_UINT16(ref, RES.ack.cmd_ref);
    TEST_ASSERT_EQUAL_UINT8(st, RES.ack.status);
    TEST_ASSERT_EQUAL_UINT8(reason, RES.ack.reason);
    TEST_ASSERT_EQUAL_UINT8(limited, RES.ack.limited);
    TEST_ASSERT_EQUAL_UINT16(applied, RES.ack.applied);
}

/* ---- ① INIT 中は捨てる ---- */

static void test_init_drops_without_ack(void)
{
    ENV.state = ST_INIT;
    link_cmd_t c = fan(1, 400, 30);
    handle(&c, 100);
    TEST_ASSERT_FALSE(RES.has_ack);
    TEST_ASSERT_FALSE(RES.send_state);
    TEST_ASSERT_FALSE(H.override_active);
    TEST_ASSERT_EQUAL_UINT16(1, H.cmd_dropped_init);
    /* キャッシュにも入らない：INIT を抜けたあとの同じ cmd_ref は新しい指令として実行する */
    ENV.state = ST_NORMAL;
    handle(&c, 2000);
    expect_ack(1, ACK_DONE, REASON_NONE, 0, 400);
    TEST_ASSERT_EQUAL_UINT16(0, H.cmd_duplicates);
}

/* ---- SET_FAN_OVERRIDE ---- */

static void test_fan_override_done(void)
{
    link_cmd_t c = fan(10, 400, 30);
    handle(&c, 5000);
    expect_ack(10, ACK_DONE, REASON_NONE, 0, 400);
    TEST_ASSERT_TRUE(RES.output_changed);
    TEST_ASSERT_TRUE(H.override_active);
    TEST_ASSERT_EQUAL_UINT16(400, H.override_pm);
    TEST_ASSERT_EQUAL_UINT32(35000, H.override_expires_ms);
}

static void test_fan_override_ranges(void)
{
    struct {
        uint16_t duty, ttl;
        bool ok;
    } cases[] = {
        {0, 1, true}, {1000, 600, true}, {1001, 30, false}, {0xFFFF, 30, false},
        {500, 0, false}, {500, 601, false}, {500, 1, true}, {500, 600, true},
    };
    for (uint16_t i = 0; i < (uint16_t)(sizeof cases / sizeof cases[0]); i++) {
        cmd_init(&H);
        link_cmd_t c = fan((uint16_t)(100 + i), cases[i].duty, cases[i].ttl);
        handle(&c, 0);
        if (cases[i].ok) {
            expect_ack((uint16_t)(100 + i), ACK_DONE, REASON_NONE, 0, cases[i].duty);
        } else {
            expect_ack((uint16_t)(100 + i), ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
            TEST_ASSERT_FALSE(H.override_active);
        }
    }
}

static void test_fan_override_args_length(void)
{
    const uint8_t a[3] = {0x90, 0x01, 0x00};
    link_cmd_t c1 = mk(1, OP_SET_FAN_OVERRIDE, 30, a, 1);
    handle(&c1, 0);
    expect_ack(1, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
    link_cmd_t c3 = mk(2, OP_SET_FAN_OVERRIDE, 30, a, 3);
    handle(&c3, 0);
    expect_ack(2, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
    link_cmd_t c0 = mk(3, OP_SET_FAN_OVERRIDE, 30, a, 0);
    handle(&c0, 0);
    expect_ack(3, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
}

static void test_fan_override_limited_by_state_floor(void)
{
    ENV.state = ST_WARNING;
    ENV.temp_c100 = 4600;
    link_cmd_t c = fan(1, 200, 30);
    handle(&c, 0);
    expect_ack(1, ACK_DONE, REASON_NONE, 1, 700); /* 拒否せず下限に丸める */
    TEST_ASSERT_EQUAL_UINT16(200, H.override_pm);
    ENV.state = ST_CRITICAL;
    link_cmd_t c2 = fan(2, 999, 30);
    handle(&c2, 0);
    expect_ack(2, ACK_DONE, REASON_NONE, 1, 1000);
    ENV.state = ST_SENSOR_FAULT;
    link_cmd_t c3 = fan(3, 1000, 30);
    handle(&c3, 0);
    expect_ack(3, ACK_DONE, REASON_NONE, 0, 1000); /* 指定値 = duty なら limited ではない */
    ENV.state = ST_WARNING;
    link_cmd_t c4 = fan(4, 700, 30);
    handle(&c4, 0);
    expect_ack(4, ACK_DONE, REASON_NONE, 0, 700);
}

static void test_fan_override_force_full_limits(void)
{
    ENV.force_full = true;
    link_cmd_t c = fan(1, 300, 30);
    handle(&c, 0);
    expect_ack(1, ACK_DONE, REASON_NONE, 1, 1000);
}

static void test_fan_override_replaces_previous(void)
{
    link_cmd_t a = fan(1, 700, 60);
    link_cmd_t b = fan(2, 200, 10);
    handle(&a, 0);
    handle(&b, 1000);
    TEST_ASSERT_EQUAL_UINT16(200, H.override_pm);
    TEST_ASSERT_EQUAL_UINT32(11000, H.override_expires_ms);
}

/* ---- リース ---- */

static void test_lease_expires_exactly_at_ttl(void)
{
    link_cmd_t c = fan(1, 400, 3);
    handle(&c, 10000);
    TEST_ASSERT_FALSE(cmd_lease_tick(&H, 12999));
    TEST_ASSERT_TRUE(H.override_active);
    TEST_ASSERT_TRUE(cmd_lease_tick(&H, 13000));
    TEST_ASSERT_FALSE(H.override_active);
    TEST_ASSERT_FALSE(cmd_lease_tick(&H, 13010));
}

static void test_lease_across_uptime_wrap(void)
{
    link_cmd_t c = fan(1, 400, 10);
    handle(&c, 0xFFFFF000u);
    TEST_ASSERT_FALSE(cmd_lease_tick(&H, 0x00000100u));
    TEST_ASSERT_TRUE(cmd_lease_tick(&H, 0xFFFFF000u + 10000u));
}

static void test_duplicate_does_not_extend_lease(void)
{
    link_cmd_t c = fan(1, 400, 5);
    handle(&c, 0);
    handle(&c, 4000);
    TEST_ASSERT_EQUAL_UINT32(5000, H.override_expires_ms);
    TEST_ASSERT_TRUE(cmd_lease_tick(&H, 5000));
}

static void test_clear_override_for_upstream_lost(void)
{
    TEST_ASSERT_FALSE(cmd_clear_override(&H));
    link_cmd_t c = fan(1, 400, 30);
    handle(&c, 0);
    TEST_ASSERT_TRUE(cmd_clear_override(&H));
    TEST_ASSERT_FALSE(H.override_active);
}

/* ---- CLEAR_OVERRIDE ---- */

static void test_clear_override(void)
{
    link_cmd_t f = fan(1, 400, 30);
    handle(&f, 0);
    link_cmd_t c = mk(2, OP_CLEAR_OVERRIDE, 0, NULL, 0);
    handle(&c, 100);
    expect_ack(2, ACK_DONE, REASON_NONE, 0, 0);
    TEST_ASSERT_TRUE(RES.output_changed);
    TEST_ASSERT_FALSE(H.override_active);
    link_cmd_t c2 = mk(3, OP_CLEAR_OVERRIDE, 0, NULL, 0);
    handle(&c2, 200); /* なくても成功 */
    expect_ack(3, ACK_DONE, REASON_NONE, 0, 0);
    TEST_ASSERT_FALSE(RES.output_changed);
    const uint8_t a[1] = {0};
    link_cmd_t c3 = mk(4, OP_CLEAR_OVERRIDE, 0, a, 1);
    handle(&c3, 300);
    expect_ack(4, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
}

/* ---- SET_THRESHOLD ---- */

static void test_threshold_done(void)
{
    link_cmd_t c = thr(1, 3500, 4500, 200);
    handle(&c, 0);
    expect_ack(1, ACK_DONE, REASON_NONE, 0, 0);
    TEST_ASSERT_EQUAL_INT16(3500, H.warn_c100);
    TEST_ASSERT_EQUAL_INT16(4500, H.crit_c100);
    TEST_ASSERT_EQUAL_INT16(200, H.hyst_c100);
}

static void test_threshold_defaults(void)
{
    TEST_ASSERT_EQUAL_INT16(4500, H.warn_c100);
    TEST_ASSERT_EQUAL_INT16(6000, H.crit_c100);
    TEST_ASSERT_EQUAL_INT16(300, H.hyst_c100);
}

static void test_threshold_ranges(void)
{
    struct {
        int16_t w, c, h;
        bool ok;
    } cases[] = {
        {-2000, 8000, 50, true},   /* 両端 */
        {-2001, 4500, 300, false}, /* warn < −20.00 */
        {4500, 8001, 300, false},  /* crit > 80.00 */
        {4500, 4500, 0, false},    /* warn = crit */
        {4600, 4500, 50, false},   /* warn > crit */
        {4500, 6000, 49, false},   /* hyst < 0.50 */
        {4500, 6000, 50, true},
        {1000, 3000, 1000, true},  /* hyst = 10.00 */
        {1000, 3000, 1001, false}, /* hyst > 10.00 */
        {4500, 4800, 300, true},   /* crit − warn = hyst */
        {4500, 4799, 300, false},  /* crit − warn < hyst */
        {-2000, -1900, 100, true},
        {4500, 6000, -300, false},
    };
    for (uint16_t i = 0; i < (uint16_t)(sizeof cases / sizeof cases[0]); i++) {
        cmd_init(&H);
        link_cmd_t c = thr((uint16_t)(200 + i), cases[i].w, cases[i].c, cases[i].h);
        handle(&c, 0);
        if (cases[i].ok) {
            expect_ack((uint16_t)(200 + i), ACK_DONE, REASON_NONE, 0, 0);
            TEST_ASSERT_EQUAL_INT16(cases[i].w, H.warn_c100);
        } else {
            expect_ack((uint16_t)(200 + i), ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
            TEST_ASSERT_EQUAL_INT16(4500, H.warn_c100);
            TEST_ASSERT_EQUAL_INT16(6000, H.crit_c100);
            TEST_ASSERT_EQUAL_INT16(300, H.hyst_c100);
        }
    }
}

static void test_threshold_unsafe_in_critical_and_fault(void)
{
    fsm_state_t unsafe[2] = {ST_CRITICAL, ST_SENSOR_FAULT};
    for (int i = 0; i < 2; i++) {
        ENV.state = unsafe[i];
        link_cmd_t c = thr((uint16_t)(1 + i), 3500, 4500, 200);
        handle(&c, 0);
        expect_ack((uint16_t)(1 + i), ACK_REJECTED, REASON_UNSAFE, 0, 0);
        TEST_ASSERT_EQUAL_INT16(4500, H.warn_c100);
    }
    ENV.state = ST_WARNING;
    link_cmd_t c = thr(9, 3500, 4500, 200);
    handle(&c, 0);
    expect_ack(9, ACK_DONE, REASON_NONE, 0, 0);
}

static void test_threshold_args_length(void)
{
    const uint8_t a[7] = {0};
    link_cmd_t c = mk(1, OP_SET_THRESHOLD, 0, a, 4);
    handle(&c, 0);
    expect_ack(1, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
    link_cmd_t c7 = mk(2, OP_SET_THRESHOLD, 0, a, 7);
    handle(&c7, 0);
    expect_ack(2, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
}

/* ---- GET_STATUS・GET_DIAG・知らない op ---- */

static void test_get_status_sends_state_then_ack(void)
{
    link_cmd_t c = mk(7, OP_GET_STATUS, 0, NULL, 0);
    handle(&c, 0);
    expect_ack(7, ACK_DONE, REASON_NONE, 0, 0);
    TEST_ASSERT_TRUE(RES.send_state);
    const uint8_t a[1] = {0};
    link_cmd_t bad = mk(8, OP_GET_STATUS, 0, a, 1);
    handle(&bad, 0);
    expect_ack(8, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
    TEST_ASSERT_FALSE(RES.send_state);
}

static void test_get_diag_ack_only(void)
{
    link_cmd_t c = mk(7, OP_GET_DIAG, 0, NULL, 0);
    handle(&c, 0);
    expect_ack(7, ACK_DONE, REASON_NONE, 0, 0);
    TEST_ASSERT_FALSE(RES.send_state);
    const uint8_t a[2] = {0};
    link_cmd_t bad = mk(8, OP_GET_DIAG, 0, a, 2);
    handle(&bad, 0);
    expect_ack(8, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
}

static void test_unknown_op(void)
{
    const uint8_t ops[] = {0x00, 0x04, 0x0F, 0x12, 0xFF};
    for (uint16_t i = 0; i < sizeof ops; i++) {
        link_cmd_t c = mk((uint16_t)(50 + i), ops[i], 0, NULL, 0);
        handle(&c, 0);
        expect_ack((uint16_t)(50 + i), ACK_REJECTED, REASON_UNKNOWN_OP, 0, 0);
    }
}

static void test_ttl_ignored_for_non_lease_ops(void)
{
    link_cmd_t c = mk(1, OP_GET_STATUS, 9999, NULL, 0);
    handle(&c, 0);
    expect_ack(1, ACK_DONE, REASON_NONE, 0, 0);
}

/* ---- 重複キャッシュ ---- */

static void test_duplicate_returns_previous_ack_without_reexecuting(void)
{
    link_cmd_t first = fan(0x4001, 700, 60);
    handle(&first, 0);
    expect_ack(0x4001, ACK_DONE, REASON_NONE, 0, 700);
    link_cmd_t second = fan(0x4002, 200, 60);
    handle(&second, 100);
    link_cmd_t again = fan(0x4001, 700, 60);
    handle(&again, 200);
    expect_ack(0x4001, ACK_DONE, REASON_NONE, 0, 700);
    TEST_ASSERT_FALSE(RES.output_changed);
    TEST_ASSERT_EQUAL_UINT16(200, H.override_pm); /* 後の指令の値のまま */
    TEST_ASSERT_EQUAL_UINT16(1, H.cmd_duplicates);
}

static void test_duplicate_ignores_new_content(void)
{
    link_cmd_t first = fan(5, 700, 60);
    handle(&first, 0);
    link_cmd_t other = mk(5, OP_GET_STATUS, 0, NULL, 0); /* 同じ cmd_ref・違う内容 */
    handle(&other, 10);
    expect_ack(5, ACK_DONE, REASON_NONE, 0, 700);
    TEST_ASSERT_FALSE(RES.send_state);
}

static void test_rejected_is_cached(void)
{
    link_cmd_t bad = fan(9, 2000, 30);
    handle(&bad, 0);
    expect_ack(9, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
    link_cmd_t fixed = fan(9, 500, 30); /* 同じ cmd_ref で正しい値を送っても前回の結果 */
    handle(&fixed, 10);
    expect_ack(9, ACK_REJECTED, REASON_INVALID_ARG, 0, 0);
    TEST_ASSERT_FALSE(H.override_active);
    TEST_ASSERT_EQUAL_UINT16(1, H.cmd_duplicates);
    link_cmd_t unk = mk(10, 0x77, 0, NULL, 0);
    handle(&unk, 20);
    handle(&unk, 30);
    expect_ack(10, ACK_REJECTED, REASON_UNKNOWN_OP, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(2, H.cmd_duplicates);
}

static void test_duplicate_get_status_resends_state(void)
{
    link_cmd_t c = mk(3, OP_GET_STATUS, 0, NULL, 0);
    handle(&c, 0);
    handle(&c, 10);
    expect_ack(3, ACK_DONE, REASON_NONE, 0, 0);
    TEST_ASSERT_TRUE(RES.send_state); /* open-questions #13 */
    TEST_ASSERT_EQUAL_UINT16(1, H.cmd_duplicates);
}

static void test_cache_holds_16_and_evicts_oldest(void)
{
    for (uint16_t ref = 1; ref <= 16u; ref++) {
        link_cmd_t c = fan(ref, ref, 30);
        handle(&c, 0);
    }
    /* 16件すべてが重複として返る */
    for (uint16_t ref = 1; ref <= 16u; ref++) {
        link_cmd_t c = fan(ref, 999, 30);
        handle(&c, 0);
        expect_ack(ref, ACK_DONE, REASON_NONE, 0, ref);
    }
    TEST_ASSERT_EQUAL_UINT16(16, H.cmd_duplicates);
    /* 17件目で最も古い cmd_ref=1 が押し出される */
    link_cmd_t c17 = fan(17, 17, 30);
    handle(&c17, 0);
    link_cmd_t c1 = fan(1, 900, 30);
    handle(&c1, 0);
    expect_ack(1, ACK_DONE, REASON_NONE, 0, 900); /* 新しい指令として実行 */
    TEST_ASSERT_EQUAL_UINT16(900, H.override_pm);
    link_cmd_t c2 = fan(2, 999, 30); /* 1 を入れたので 2 が押し出されている */
    handle(&c2, 0);
    expect_ack(2, ACK_DONE, REASON_NONE, 0, 999);
    link_cmd_t c3 = fan(3, 999, 30); /* 3 も 2 を入れたときに押し出されている */
    handle(&c3, 0);
    expect_ack(3, ACK_DONE, REASON_NONE, 0, 999);
    link_cmd_t c17b = fan(17, 1, 30);
    handle(&c17b, 0);
    expect_ack(17, ACK_DONE, REASON_NONE, 0, 17); /* 17 は残っている */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_drops_without_ack);
    RUN_TEST(test_fan_override_done);
    RUN_TEST(test_fan_override_ranges);
    RUN_TEST(test_fan_override_args_length);
    RUN_TEST(test_fan_override_limited_by_state_floor);
    RUN_TEST(test_fan_override_force_full_limits);
    RUN_TEST(test_fan_override_replaces_previous);
    RUN_TEST(test_lease_expires_exactly_at_ttl);
    RUN_TEST(test_lease_across_uptime_wrap);
    RUN_TEST(test_duplicate_does_not_extend_lease);
    RUN_TEST(test_clear_override_for_upstream_lost);
    RUN_TEST(test_clear_override);
    RUN_TEST(test_threshold_done);
    RUN_TEST(test_threshold_defaults);
    RUN_TEST(test_threshold_ranges);
    RUN_TEST(test_threshold_unsafe_in_critical_and_fault);
    RUN_TEST(test_threshold_args_length);
    RUN_TEST(test_get_status_sends_state_then_ack);
    RUN_TEST(test_get_diag_ack_only);
    RUN_TEST(test_unknown_op);
    RUN_TEST(test_ttl_ignored_for_non_lease_ops);
    RUN_TEST(test_duplicate_returns_previous_ack_without_reexecuting);
    RUN_TEST(test_duplicate_ignores_new_content);
    RUN_TEST(test_rejected_is_cached);
    RUN_TEST(test_duplicate_get_status_resends_state);
    RUN_TEST(test_cache_holds_16_and_evicts_oldest);
    return UNITY_END();
}
