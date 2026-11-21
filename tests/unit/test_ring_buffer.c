#include "ring_buffer.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_capacity_must_be_power_of_two(void)
{
    uint8_t s[16];
    ring_buffer_t rb;
    TEST_ASSERT_FALSE(rb_init(&rb, s, 0));
    TEST_ASSERT_FALSE(rb_init(&rb, s, 3));
    TEST_ASSERT_FALSE(rb_init(&rb, s, 12));
    TEST_ASSERT_FALSE(rb_init(&rb, NULL, 16));
    TEST_ASSERT_TRUE(rb_init(&rb, s, 1));
    TEST_ASSERT_TRUE(rb_init(&rb, s, 16));
}

static void test_fill_to_capacity_then_reject(void)
{
    uint8_t s[8];
    ring_buffer_t rb;
    TEST_ASSERT_TRUE(rb_init(&rb, s, 8));
    TEST_ASSERT_EQUAL_UINT32(8, rb_free(&rb));
    for (uint8_t i = 0; i < 8u; i++) {
        TEST_ASSERT_TRUE(rb_push(&rb, i));
    }
    TEST_ASSERT_EQUAL_UINT32(8, rb_count(&rb));
    TEST_ASSERT_EQUAL_UINT32(0, rb_free(&rb));
    TEST_ASSERT_FALSE(rb_push(&rb, 99));
    uint8_t v = 0u;
    for (uint8_t i = 0; i < 8u; i++) {
        TEST_ASSERT_TRUE(rb_pop(&rb, &v));
        TEST_ASSERT_EQUAL_UINT8(i, v);
    }
    TEST_ASSERT_FALSE(rb_pop(&rb, &v));
}

static void test_fifo_across_wrap(void)
{
    uint8_t s[4];
    ring_buffer_t rb;
    TEST_ASSERT_TRUE(rb_init(&rb, s, 4));
    uint8_t next_in = 0u;
    uint8_t next_out = 0u;
    for (int round = 0; round < 1000; round++) {
        int n = (round % 4) + 1;
        for (int k = 0; k < n; k++) {
            TEST_ASSERT_TRUE(rb_push(&rb, next_in++));
        }
        for (int k = 0; k < n; k++) {
            uint8_t v = 0u;
            TEST_ASSERT_TRUE(rb_pop(&rb, &v));
            TEST_ASSERT_EQUAL_UINT8(next_out++, v);
        }
    }
}

static void test_index_wraps_u32(void)
{
    uint8_t s[4];
    ring_buffer_t rb;
    TEST_ASSERT_TRUE(rb_init(&rb, s, 4));
    rb.head = 0xFFFFFFFEu; /* 添字が u32 で一周する直前 */
    rb.tail = 0xFFFFFFFEu;
    for (uint8_t i = 0; i < 4u; i++) {
        TEST_ASSERT_TRUE(rb_push(&rb, i));
    }
    TEST_ASSERT_FALSE(rb_push(&rb, 9));
    TEST_ASSERT_EQUAL_UINT32(4, rb_count(&rb));
    uint8_t v = 0u;
    for (uint8_t i = 0; i < 4u; i++) {
        TEST_ASSERT_TRUE(rb_pop(&rb, &v));
        TEST_ASSERT_EQUAL_UINT8(i, v);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_capacity_must_be_power_of_two);
    RUN_TEST(test_fill_to_capacity_then_reject);
    RUN_TEST(test_fifo_across_wrap);
    RUN_TEST(test_index_wraps_u32);
    return UNITY_END();
}
