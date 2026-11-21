/* temp_sensor（仮版）：25.00℃ 固定、quality 0、12サンプル目で ready。本版のテストは P2 */
#include "temp_sensor.h"
#include "unity.h"

void setUp(void)
{
    temp_init();
}
void tearDown(void) {}

static void test_ready_at_12th_sample(void)
{
    temp_reading_t r;
    for (uint32_t i = 1; i <= 11u; i++) {
        temp_update(1000u, i * 100u);
        temp_get(&r);
        TEST_ASSERT_FALSE(r.ready);
    }
    temp_update(1000u, 1200u);
    temp_get(&r);
    TEST_ASSERT_TRUE(r.ready);
    TEST_ASSERT_EQUAL_INT16(2500, r.temp_c100);
    TEST_ASSERT_EQUAL_UINT8(TEMP_QUALITY_GOOD, r.quality);
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, r.fault);
}

static void test_fixed_temperature_ignores_raw(void)
{
    temp_reading_t r;
    for (uint32_t i = 0; i < 20u; i++) {
        temp_update((uint16_t)(i * 200u), i * 100u);
        temp_tick(i * 100u);
    }
    temp_get(&r);
    TEST_ASSERT_EQUAL_INT16(2500, r.temp_c100);
    TEST_ASSERT_EQUAL_UINT8(9, r.valid_run);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ready_at_12th_sample);
    RUN_TEST(test_fixed_temperature_ignores_raw);
    return UNITY_END();
}
