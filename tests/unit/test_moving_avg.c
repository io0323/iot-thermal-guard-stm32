#include "moving_avg.h"
#include "unity.h"

static moving_avg8_t A;

void setUp(void)
{
    moving_avg8_init(&A);
}
void tearDown(void) {}

static void push_n(int16_t v, int n)
{
    for (int i = 0; i < n; i++) {
        moving_avg8_push(&A, v);
    }
}

static void test_full_after_eight(void)
{
    push_n(100, 7);
    TEST_ASSERT_FALSE(moving_avg8_full(&A));
    push_n(100, 1);
    TEST_ASSERT_TRUE(moving_avg8_full(&A));
    TEST_ASSERT_EQUAL_INT16(100, moving_avg8_value(&A));
}

static void test_sliding(void)
{
    push_n(0, 8);
    push_n(800, 1); /* 和 800 → 100 */
    TEST_ASSERT_EQUAL_INT16(100, moving_avg8_value(&A));
    push_n(800, 7);
    TEST_ASSERT_EQUAL_INT16(800, moving_avg8_value(&A));
}

static void test_rounding_half_away_from_zero(void)
{
    push_n(0, 4);
    push_n(1, 4); /* 4/8 = 0.5 → 1 */
    TEST_ASSERT_EQUAL_INT16(1, moving_avg8_value(&A));
    moving_avg8_init(&A);
    push_n(0, 5);
    push_n(1, 3); /* 3/8 = 0.375 → 0 */
    TEST_ASSERT_EQUAL_INT16(0, moving_avg8_value(&A));
    moving_avg8_init(&A);
    push_n(0, 4);
    push_n(-1, 4); /* −0.5 → −1（0 を中心に対称） */
    TEST_ASSERT_EQUAL_INT16(-1, moving_avg8_value(&A));
    moving_avg8_init(&A);
    push_n(0, 5);
    push_n(-1, 3); /* −0.375 → 0 */
    TEST_ASSERT_EQUAL_INT16(0, moving_avg8_value(&A));
    moving_avg8_init(&A);
    push_n(-1995, 4);
    push_n(-2000, 4); /* −1997.5 → −1998 */
    TEST_ASSERT_EQUAL_INT16(-1998, moving_avg8_value(&A));
}

static void test_extremes_do_not_overflow(void)
{
    push_n(32767, 8);
    TEST_ASSERT_EQUAL_INT16(32767, moving_avg8_value(&A));
    push_n(-32768, 8);
    TEST_ASSERT_EQUAL_INT16(-32768, moving_avg8_value(&A));
}

static void test_partial(void)
{
    TEST_ASSERT_EQUAL_INT16(0, moving_avg8_value(&A));
    moving_avg8_push(&A, 10);
    moving_avg8_push(&A, 21); /* 15.5 → 16 */
    TEST_ASSERT_EQUAL_INT16(16, moving_avg8_value(&A));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_after_eight);
    RUN_TEST(test_sliding);
    RUN_TEST(test_rounding_half_away_from_zero);
    RUN_TEST(test_extremes_do_not_overflow);
    RUN_TEST(test_partial);
    return UNITY_END();
}
