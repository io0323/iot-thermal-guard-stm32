#include "median.h"
#include "unity.h"

static median5_t M;

void setUp(void)
{
    median5_init(&M);
}
void tearDown(void) {}

static void test_full_after_five(void)
{
    for (int16_t i = 0; i < 4; i++) {
        median5_push(&M, i);
        TEST_ASSERT_FALSE(median5_full(&M));
    }
    median5_push(&M, 4);
    TEST_ASSERT_TRUE(median5_full(&M));
}

static void test_removes_outlier(void)
{
    const int16_t v[5] = {2500, 2510, 9000, 2490, 2505};
    for (int i = 0; i < 5; i++) {
        median5_push(&M, v[i]);
    }
    TEST_ASSERT_EQUAL_INT16(2505, median5_value(&M));
}

static void test_sliding_window(void)
{
    const int16_t v[7] = {1, 2, 3, 4, 5, 100, 100};
    for (int i = 0; i < 7; i++) {
        median5_push(&M, v[i]);
    }
    /* 窓は {3,4,5,100,100} */
    TEST_ASSERT_EQUAL_INT16(5, median5_value(&M));
    median5_push(&M, 100);
    TEST_ASSERT_EQUAL_INT16(100, median5_value(&M));
}

static void test_negative_and_extremes(void)
{
    const int16_t v[5] = {-32768, 32767, -2000, -1999, 0};
    for (int i = 0; i < 5; i++) {
        median5_push(&M, v[i]);
    }
    TEST_ASSERT_EQUAL_INT16(-1999, median5_value(&M));
}

static void test_partial_and_empty(void)
{
    TEST_ASSERT_EQUAL_INT16(0, median5_value(&M));
    median5_push(&M, 30);
    median5_push(&M, 10);
    TEST_ASSERT_EQUAL_INT16(10, median5_value(&M)); /* 偶数個は小さい側 */
    median5_push(&M, 20);
    TEST_ASSERT_EQUAL_INT16(20, median5_value(&M));
    median5_init(&M);
    TEST_ASSERT_FALSE(median5_full(&M));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_after_five);
    RUN_TEST(test_removes_outlier);
    RUN_TEST(test_sliding_window);
    RUN_TEST(test_negative_and_extremes);
    RUN_TEST(test_partial_and_empty);
    return UNITY_END();
}
