/*
 * ゴールデンのテストベクタ（詳細設計書 第9章）。build/generated/golden_vectors.h を使う。
 * ① 01→03 の全件で符号化が hex と一致（expected.discarded が crc_mismatch のものを除く）
 * ② 全件で復号が frame の項目と一致（crc_mismatch のものを除く）
 * ③ crc_mismatch のものは復号が crc_mismatch で失敗
 * ④ 件数が生成時と一致
 */
#include <string.h>

#include "golden_vectors.h"
#include "link.h"
#include "unity.h"

/* iot-common-spec v0.2.1 のゴールデンの件数。タグを上げて件数が変わったらここを見直す。 */
#define GOLDEN_EXPECTED_COUNT 27u

#define N_VECTORS (sizeof golden_vectors / sizeof golden_vectors[0])

void setUp(void) {}
void tearDown(void) {}

static void assert_same_frame(const link_frame_t *e, const link_frame_t *a, const char *name)
{
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->type, a->type, name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(e->seq, a->seq, name);
    switch ((frame_type_t)e->type) {
    case FRAME_TELEMETRY:
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(e->u.telemetry.uptime_ms, a->u.telemetry.uptime_ms, name);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(e->u.telemetry.temp_c100, a->u.telemetry.temp_c100, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.telemetry.quality, a->u.telemetry.quality, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.telemetry.state, a->u.telemetry.state, name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(e->u.telemetry.fan_pm, a->u.telemetry.fan_pm, name);
        break;
    case FRAME_EVENT:
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(e->u.event.uptime_ms, a->u.event.uptime_ms, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.event.code, a->u.event.code, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.event.severity, a->u.event.severity, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.event.from, a->u.event.from, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.event.to, a->u.event.to, name);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(e->u.event.temp_c100, a->u.event.temp_c100, name);
        break;
    case FRAME_HEARTBEAT:
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(e->u.heartbeat.uptime_ms, a->u.heartbeat.uptime_ms, name);
        break;
    case FRAME_STATE:
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(e->u.state.fw_version, a->u.state.fw_version, name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(e->u.state.boot_id, a->u.state.boot_id, name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(e->u.state.period_ms, a->u.state.period_ms, name);
        break;
    case FRAME_CMD:
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(e->u.cmd.cmd_ref, a->u.cmd.cmd_ref, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.cmd.op, a->u.cmd.op, name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(e->u.cmd.ttl_s, a->u.cmd.ttl_s, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.cmd.args_len, a->u.cmd.args_len, name);
        if (e->u.cmd.args_len > 0u) {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(e->u.cmd.args, a->u.cmd.args, e->u.cmd.args_len, name);
        }
        break;
    case FRAME_ACK:
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(e->u.ack.cmd_ref, a->u.ack.cmd_ref, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.ack.status, a->u.ack.status, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.ack.reason, a->u.ack.reason, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->u.ack.limited, a->u.ack.limited, name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(e->u.ack.applied, a->u.ack.applied, name);
        break;
    default:
        TEST_FAIL_MESSAGE(name);
        break;
    }
}

/* ① */
static void test_encode_device_frames_match_hex(void)
{
    size_t checked = 0u;
    for (size_t i = 0; i < N_VECTORS; i++) {
        const golden_vector_t *g = &golden_vectors[i];
        if (!g->from_device || g->crc_mismatch) {
            continue;
        }
        uint8_t enc[LINK_ENCODED_MAX];
        size_t n = link_frame_encode(&g->frame, enc, sizeof enc);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(g->len, n, g->name);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(g->bytes, enc, g->len, g->name);
        checked++;
    }
    TEST_ASSERT_TRUE(checked > 0u);
}

/* ② */
static void test_decode_all_match_frame(void)
{
    size_t checked = 0u;
    for (size_t i = 0; i < N_VECTORS; i++) {
        const golden_vector_t *g = &golden_vectors[i];
        if (g->crc_mismatch) {
            continue;
        }
        link_frame_t f;
        memset(&f, 0, sizeof f);
        TEST_ASSERT_EQUAL_MESSAGE(LINK_RX_OK, link_frame_decode(g->bytes, g->len - 1u, &f), g->name);
        assert_same_frame(&g->frame, &f, g->name);
        checked++;
    }
    TEST_ASSERT_TRUE(checked > 0u);
}

/* ② の続き：03→01 は作品01の受信処理（link の組み立て）でも同じ結果になる */
static void test_receive_path_for_upstream_frames(void)
{
    for (size_t i = 0; i < N_VECTORS; i++) {
        const golden_vector_t *g = &golden_vectors[i];
        if (g->from_device) {
            continue;
        }
        link_t l;
        link_init(&l);
        link_frame_t f;
        link_rx_error_t err = LINK_RX_OK;
        link_rx_status_t st = LINK_RX_NONE;
        for (size_t k = 0; k < g->len; k++) {
            st = link_rx_byte(&l, g->bytes[k], &f, &err);
        }
        TEST_ASSERT_EQUAL_MESSAGE(LINK_RX_FRAME, st, g->name);
        assert_same_frame(&g->frame, &f, g->name);
    }
}

/* ③ */
static void test_crc_mismatch_vectors_fail(void)
{
    size_t checked = 0u;
    for (size_t i = 0; i < N_VECTORS; i++) {
        const golden_vector_t *g = &golden_vectors[i];
        if (!g->crc_mismatch) {
            continue;
        }
        link_frame_t f;
        TEST_ASSERT_EQUAL_MESSAGE(LINK_RX_CRC_MISMATCH, link_frame_decode(g->bytes, g->len - 1u, &f), g->name);
        checked++;
    }
    TEST_ASSERT_EQUAL_size_t(1, checked); /* v0.2.1 ではゴールデン23の1件 */
}

/* ④ */
static void test_count_matches_generation(void)
{
    TEST_ASSERT_EQUAL_size_t(GOLDEN_GENERATED_COUNT, N_VECTORS);
    TEST_ASSERT_EQUAL_UINT32(GOLDEN_EXPECTED_COUNT, GOLDEN_GENERATED_COUNT);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_device_frames_match_hex);
    RUN_TEST(test_decode_all_match_frame);
    RUN_TEST(test_receive_path_for_upstream_frames);
    RUN_TEST(test_crc_mismatch_vectors_fail);
    RUN_TEST(test_count_matches_generation);
    return UNITY_END();
}
