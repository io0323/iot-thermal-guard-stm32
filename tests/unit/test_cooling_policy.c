/* cooling_policy：auto の境界 3499・3500・4500、状態の下限、手動指定、limited の元になる値、強制全開 */
#include "cooling_policy.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_auto_boundaries(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, cooling_auto(-32768));
    TEST_ASSERT_EQUAL_UINT16(0, cooling_auto(2500));
    TEST_ASSERT_EQUAL_UINT16(0, cooling_auto(3499));
    TEST_ASSERT_EQUAL_UINT16(0, cooling_auto(3500));
    TEST_ASSERT_EQUAL_UINT16(0, cooling_auto(3502));   /* 2×400÷1000 = 0.8 → 切り捨て */
    TEST_ASSERT_EQUAL_UINT16(1, cooling_auto(3503));   /* 1.2 → 1 */
    TEST_ASSERT_EQUAL_UINT16(200, cooling_auto(4000));
    TEST_ASSERT_EQUAL_UINT16(399, cooling_auto(4499)); /* 999×400÷1000 = 399.6 → 399 */
    TEST_ASSERT_EQUAL_UINT16(400, cooling_auto(4500));
    TEST_ASSERT_EQUAL_UINT16(400, cooling_auto(12000));
    TEST_ASSERT_EQUAL_UINT16(400, cooling_auto(32767));
}

static void test_floor_per_state(void)
{
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_floor(ST_INIT));
    TEST_ASSERT_EQUAL_UINT16(0, cooling_floor(ST_NORMAL));
    TEST_ASSERT_EQUAL_UINT16(700, cooling_floor(ST_WARNING));
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_floor(ST_CRITICAL));
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_floor(ST_SENSOR_FAULT));
}

static void test_auto_vs_floor(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, cooling_duty(ST_NORMAL, 2500, false, 0, false));
    TEST_ASSERT_EQUAL_UINT16(200, cooling_duty(ST_NORMAL, 4000, false, 0, false));
    TEST_ASSERT_EQUAL_UINT16(700, cooling_duty(ST_WARNING, 4600, false, 0, false));
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_duty(ST_INIT, 2500, false, 0, false));
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_duty(ST_CRITICAL, 6100, false, 0, false));
}

static void test_manual_replaces_auto(void)
{
    TEST_ASSERT_EQUAL_UINT16(400, cooling_duty(ST_NORMAL, 2500, true, 400, false));
    TEST_ASSERT_EQUAL_UINT16(200, cooling_duty(ST_NORMAL, 2500, true, 200, false));
    TEST_ASSERT_EQUAL_UINT16(0, cooling_duty(ST_NORMAL, 4400, true, 0, false)); /* auto より小さくても指定値 */
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_duty(ST_NORMAL, 2500, true, 1000, false));
}

static void test_manual_limited_by_floor(void)
{
    TEST_ASSERT_EQUAL_UINT16(700, cooling_duty(ST_WARNING, 4600, true, 200, false));
    TEST_ASSERT_EQUAL_UINT16(900, cooling_duty(ST_WARNING, 4600, true, 900, false));
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_duty(ST_CRITICAL, 6100, true, 0, false));
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_duty(ST_SENSOR_FAULT, -32768, true, 500, false));
}

static void test_force_full(void)
{
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_duty(ST_NORMAL, 2500, false, 0, true));
    TEST_ASSERT_EQUAL_UINT16(1000, cooling_duty(ST_NORMAL, 2500, true, 100, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_auto_boundaries);
    RUN_TEST(test_floor_per_state);
    RUN_TEST(test_auto_vs_floor);
    RUN_TEST(test_manual_replaces_auto);
    RUN_TEST(test_manual_limited_by_floor);
    RUN_TEST(test_force_full);
    return UNITY_END();
}
