#include "tick.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* 差 (now − deadline) の境界：0 と 0x7FFFFFFF は達した、0x80000000 と 0xFFFFFFFF は達していない */
static void test_boundaries_of_unsigned_difference(void)
{
    const uint32_t d = 1000U;
    TEST_ASSERT_TRUE(tick_reached(d + 0x00000000U, d));
    TEST_ASSERT_TRUE(tick_reached(d + 0x7FFFFFFFU, d));
    TEST_ASSERT_FALSE(tick_reached(d + 0x80000000U, d));
    TEST_ASSERT_FALSE(tick_reached(d + 0xFFFFFFFFU, d)); /* 1ms 手前 */
}

/* 引数の取り違えの検出：now と deadline を入れ替えると差が 1 と 0xFFFFFFFF で入れ替わり、結果が逆になる（#41） */
static void test_argument_order(void)
{
    TEST_ASSERT_TRUE(tick_reached(1001U, 1000U));
    TEST_ASSERT_FALSE(tick_reached(1000U, 1001U));
}

/* uptime_ms の一周をまたぐ：期限が一周の手前、現在が一周の後 */
static void test_across_uptime_wrap(void)
{
    const uint32_t deadline = 0xFFFFFFF0U + 100U; /* 一周して 0x00000054 */
    TEST_ASSERT_EQUAL_HEX32(0x00000054U, deadline);
    TEST_ASSERT_FALSE(tick_reached(0xFFFFFFF0U, deadline)); /* まだ100ms 前 */
    TEST_ASSERT_FALSE(tick_reached(0xFFFFFFFFU, deadline));
    TEST_ASSERT_FALSE(tick_reached(0x00000053U, deadline)); /* 1ms 手前（一周の後） */
    TEST_ASSERT_TRUE(tick_reached(0x00000054U, deadline));  /* ちょうど */
    TEST_ASSERT_TRUE(tick_reached(0x00000100U, deadline));
}

/* 期限が一周の後、現在が一周の手前のまま進まない場合は、期限の前と読む */
static void test_deadline_just_after_wrap(void)
{
    TEST_ASSERT_FALSE(tick_reached(0xFFFFFFFEU, 0x00000001U));
    TEST_ASSERT_TRUE(tick_reached(0x00000001U, 0xFFFFFFFEU));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boundaries_of_unsigned_difference);
    RUN_TEST(test_argument_order);
    RUN_TEST(test_across_uptime_wrap);
    RUN_TEST(test_deadline_just_after_wrap);
    return UNITY_END();
}
