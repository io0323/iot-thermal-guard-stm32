#include <string.h>

#include "cobs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void roundtrip(const uint8_t *in, size_t len)
{
    uint8_t enc[600];
    uint8_t dec[600];
    size_t n = cobs_encode(in, len, enc, sizeof enc);
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_TRUE(n <= COBS_MAX_ENCODED(len));
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_NOT_EQUAL(0, enc[i]);
    }
    size_t m = 0u;
    TEST_ASSERT_TRUE(cobs_decode(enc, n, dec, sizeof dec, &m));
    TEST_ASSERT_EQUAL_size_t(len, m);
    if (len > 0u) {
        TEST_ASSERT_EQUAL_MEMORY(in, dec, len);
    }
}

static void test_known_vectors(void)
{
    const uint8_t a[] = {0x00};
    const uint8_t a_enc[] = {0x01, 0x01};
    const uint8_t b[] = {0x11, 0x22, 0x00, 0x33};
    const uint8_t b_enc[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    const uint8_t c[] = {0x11, 0x00, 0x00, 0x00};
    const uint8_t c_enc[] = {0x02, 0x11, 0x01, 0x01, 0x01};
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(2, cobs_encode(a, sizeof a, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY(a_enc, out, 2);
    TEST_ASSERT_EQUAL_size_t(5, cobs_encode(b, sizeof b, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY(b_enc, out, 5);
    TEST_ASSERT_EQUAL_size_t(5, cobs_encode(c, sizeof c, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY(c_enc, out, 5);
}

static void test_empty(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_size_t(1, cobs_encode(NULL, 0, out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8(0x01, out[0]);
    size_t m = 99u;
    TEST_ASSERT_TRUE(cobs_decode(out, 1, out, sizeof out, &m));
    TEST_ASSERT_EQUAL_size_t(0, m);
}

static void test_roundtrip_patterns(void)
{
    uint8_t buf[520];
    for (size_t len = 0; len <= 520; len++) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)((i * 7u) % 5u == 0u ? 0u : (i & 0xFFu) | 1u);
        }
        roundtrip(buf, len);
    }
    /* 0 を含まない 254・255 バイト（コード 0xFF の境界） */
    memset(buf, 0xAB, sizeof buf);
    roundtrip(buf, 253);
    roundtrip(buf, 254);
    roundtrip(buf, 255);
    roundtrip(buf, 508);
    memset(buf, 0, sizeof buf);
    roundtrip(buf, 300);
}

static void test_encode_overflow(void)
{
    uint8_t in[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint8_t out[12];
    TEST_ASSERT_EQUAL_size_t(0, cobs_encode(in, sizeof in, out, 10));
    TEST_ASSERT_EQUAL_size_t(11, cobs_encode(in, sizeof in, out, 11));
    TEST_ASSERT_EQUAL_size_t(0, cobs_encode(in, 1, out, 0));
    const uint8_t zeros[3] = {0, 0, 0};
    TEST_ASSERT_EQUAL_size_t(0, cobs_encode(zeros, sizeof zeros, out, 3));
    TEST_ASSERT_EQUAL_size_t(4, cobs_encode(zeros, sizeof zeros, out, 4));
}

static void test_decode_errors(void)
{
    uint8_t out[16];
    size_t m = 0u;
    const uint8_t zero_inside[] = {0x03, 0x11, 0x00};
    const uint8_t past_end[] = {0x05, 0x11, 0x22};
    const uint8_t code_zero[] = {0x00};
    TEST_ASSERT_FALSE(cobs_decode(zero_inside, sizeof zero_inside, out, sizeof out, &m));
    TEST_ASSERT_FALSE(cobs_decode(past_end, sizeof past_end, out, sizeof out, &m));
    TEST_ASSERT_FALSE(cobs_decode(code_zero, sizeof code_zero, out, sizeof out, &m));
    const uint8_t ok[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    TEST_ASSERT_FALSE(cobs_decode(ok, sizeof ok, out, 3, &m)); /* 出力先が足りない */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_vectors);
    RUN_TEST(test_empty);
    RUN_TEST(test_roundtrip_patterns);
    RUN_TEST(test_encode_overflow);
    RUN_TEST(test_decode_errors);
    return UNITY_END();
}
