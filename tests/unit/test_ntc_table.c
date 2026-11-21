/* 変換表：両端の値、単調性、表の点ちょうど、補間と四捨五入、表の外 */
#include "ntc_table.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_endpoints_match_design_approximation(void)
{
    TEST_ASSERT_EQUAL_UINT32(141, NTC_POINTS);
    TEST_ASSERT_EQUAL_UINT16(3627, NTC_RAW_COLD); /* −20℃（第3章の概算 3627） */
    TEST_ASSERT_EQUAL_UINT16(238, NTC_RAW_HOT);   /* 120℃（第3章の概算 238） */
    TEST_ASSERT_EQUAL_UINT16(3627, ntc_table_raw_at(-20));
    TEST_ASSERT_EQUAL_UINT16(2048, ntc_table_raw_at(25)); /* 25℃（第3章の概算 2048） */
    TEST_ASSERT_EQUAL_UINT16(238, ntc_table_raw_at(120));
    TEST_ASSERT_EQUAL_UINT16(0, ntc_table_raw_at(-21));
    TEST_ASSERT_EQUAL_UINT16(0, ntc_table_raw_at(121));
}

static void test_strictly_decreasing(void)
{
    for (int16_t t = -19; t <= 120; t++) {
        TEST_ASSERT_TRUE(ntc_table_raw_at(t) < ntc_table_raw_at((int16_t)(t - 1)));
    }
}

static void test_table_points_convert_exactly(void)
{
    for (int16_t t = -20; t <= 120; t++) {
        TEST_ASSERT_EQUAL_INT16((int16_t)(t * 100), ntc_raw_to_c100(ntc_table_raw_at(t)));
    }
}

static void test_ends_and_outside(void)
{
    TEST_ASSERT_EQUAL_INT16(-2000, ntc_raw_to_c100(3627));
    TEST_ASSERT_EQUAL_INT16(-2000, ntc_raw_to_c100(3628)); /* 表の外は端の温度（丸めと数えるのは temp_sensor） */
    TEST_ASSERT_EQUAL_INT16(-2000, ntc_raw_to_c100(4095));
    TEST_ASSERT_EQUAL_INT16(12000, ntc_raw_to_c100(238));
    TEST_ASSERT_EQUAL_INT16(12000, ntc_raw_to_c100(237));
    TEST_ASSERT_EQUAL_INT16(12000, ntc_raw_to_c100(0));
}

static void test_interpolation_rounds_half_up(void)
{
    /* 隣り合う2点のちょうど間（差が偶数の区間を探す）は x.50℃ */
    int checked = 0;
    for (int16_t t = -20; t < 120 && checked < 5; t++) {
        uint16_t a = ntc_table_raw_at(t);
        uint16_t b = ntc_table_raw_at((int16_t)(t + 1));
        uint16_t d = (uint16_t)(a - b);
        if ((d % 2u) == 0u) {
            TEST_ASSERT_EQUAL_INT16((int16_t)(t * 100 + 50), ntc_raw_to_c100((uint16_t)(a - d / 2u)));
            checked++;
        }
    }
    TEST_ASSERT_TRUE(checked > 0);
    /* 1生値だけ下：100 ÷ 差 を四捨五入 */
    uint16_t a = ntc_table_raw_at(25);
    uint16_t b = ntc_table_raw_at(26);
    uint32_t d = (uint32_t)(a - b);
    int16_t expect = (int16_t)(2500 + (int16_t)((100u + d / 2u) / d));
    TEST_ASSERT_EQUAL_INT16(expect, ntc_raw_to_c100((uint16_t)(a - 1u)));
}

static void test_monotonic_conversion(void)
{
    int16_t prev = ntc_raw_to_c100(3627);
    for (uint16_t raw = 3626; raw >= 238; raw--) {
        int16_t t = ntc_raw_to_c100(raw);
        TEST_ASSERT_TRUE(t >= prev);
        prev = t;
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_endpoints_match_design_approximation);
    RUN_TEST(test_strictly_decreasing);
    RUN_TEST(test_table_points_convert_exactly);
    RUN_TEST(test_ends_and_outside);
    RUN_TEST(test_interpolation_rounds_half_up);
    RUN_TEST(test_monotonic_conversion);
    return UNITY_END();
}
