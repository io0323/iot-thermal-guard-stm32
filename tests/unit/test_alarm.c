/* alarm：状態ごとのパターンと周期、位相の起点、消音と解除、INIT のパターン、強制全開の表示（第5章、#37） */
#include <stdio.h>

#include "alarm.h"
#include "unity.h"

static alarm_t A;

void setUp(void)
{
    alarm_init(&A, 0u);
}
void tearDown(void) {}

static alarm_out_t at(uint32_t t, bool force)
{
    alarm_out_t o;
    alarm_output(&A, t, force, &o);
    return o;
}

static void expect(uint32_t t, bool force, bool g, bool y, bool r, bool bz)
{
    alarm_out_t o = at(t, force);
    char msg[32];
    (void)snprintf(msg, sizeof msg, "t=%u", (unsigned)t);
    TEST_ASSERT_EQUAL_MESSAGE(g, o.led[LED_GREEN], msg);
    TEST_ASSERT_EQUAL_MESSAGE(y, o.led[LED_YELLOW], msg);
    TEST_ASSERT_EQUAL_MESSAGE(r, o.led[LED_RED], msg);
    TEST_ASSERT_EQUAL_MESSAGE(bz, o.buzzer, msg);
}

static void test_init_pattern(void)
{
    alarm_init(&A, 1000u); /* 起点は起動した時刻 */
    expect(1000u, false, false, false, true, true);
    expect(1099u, false, false, false, true, true);
    expect(1100u, false, false, true, false, false);
    expect(1199u, false, false, true, false, false);
    expect(1200u, false, true, false, false, false);
    expect(1299u, false, true, false, false, false);
    expect(1300u, false, false, false, false, false);
    expect(2000u, false, false, false, false, false);
}

static void test_normal_solid_green(void)
{
    alarm_set_state(&A, ST_NORMAL, 1100u);
    expect(1100u, false, true, false, false, false);
    expect(9999u, false, true, false, false, false);
}

static void test_normal_force_full_blinks_2hz_from_state_entry(void)
{
    alarm_set_state(&A, ST_NORMAL, 1103u);
    expect(1103u, true, true, false, false, false);
    expect(1352u, true, true, false, false, false); /* 250ms 点灯 */
    expect(1353u, true, false, false, false, false);
    expect(1602u, true, false, false, false, false); /* 250ms 消灯 */
    expect(1603u, true, true, false, false, false);
}

static void test_force_full_display_only_in_normal(void)
{
    alarm_set_state(&A, ST_WARNING, 0u);
    TEST_ASSERT_FALSE(at(250u, true).led[LED_GREEN]);
    alarm_set_state(&A, ST_CRITICAL, 0u);
    TEST_ASSERT_FALSE(at(0u, true).led[LED_GREEN]);
}

static void test_warning_yellow_1hz(void)
{
    alarm_set_state(&A, ST_WARNING, 5007u);
    expect(5007u, false, false, true, false, false);
    expect(5506u, false, false, true, false, false);
    expect(5507u, false, false, false, false, false);
    expect(6006u, false, false, false, false, false);
    expect(6007u, false, false, true, false, false);
}

static void test_critical_red_4hz_buzzer_250(void)
{
    alarm_set_state(&A, ST_CRITICAL, 2000u);
    expect(2000u, false, false, false, true, true);
    expect(2124u, false, false, false, true, true);
    expect(2125u, false, false, false, false, true);
    expect(2249u, false, false, false, false, true);
    expect(2250u, false, false, false, true, false); /* ブザーは 250ms で止まる */
    expect(2499u, false, false, false, false, false);
    expect(2500u, false, false, false, true, true);
}

static void test_sensor_fault_red_solid_continuous(void)
{
    alarm_set_state(&A, ST_SENSOR_FAULT, 0u);
    expect(0u, false, false, false, true, true);
    expect(12345u, false, false, false, true, true);
}

static void test_sounding_definition(void)
{
    const fsm_state_t quiet[3] = {ST_INIT, ST_NORMAL, ST_WARNING};
    for (int i = 0; i < 3; i++) {
        alarm_set_state(&A, quiet[i], 0u);
        TEST_ASSERT_FALSE(alarm_sounding(&A)); /* INIT の最初の100ms の音も「鳴っている」に入れない */
    }
    alarm_set_state(&A, ST_CRITICAL, 0u);
    TEST_ASSERT_TRUE(alarm_sounding(&A));
    TEST_ASSERT_FALSE(at(300u, false).buzzer); /* 断続の音が止まっている瞬間 */
    TEST_ASSERT_TRUE(alarm_sounding(&A));      /* それでも「鳴っている」 */
}

static void test_silence_and_release(void)
{
    alarm_set_state(&A, ST_CRITICAL, 0u);
    alarm_silence(&A);
    TEST_ASSERT_FALSE(alarm_sounding(&A));
    TEST_ASSERT_FALSE(at(0u, false).buzzer);
    TEST_ASSERT_TRUE(at(0u, false).led[LED_RED]); /* LED は続ける */
    alarm_set_state(&A, ST_SENSOR_FAULT, 100u);   /* SENSOR_FAULT に新しく入った：解除 */
    TEST_ASSERT_TRUE(alarm_sounding(&A));
    alarm_silence(&A);
    alarm_set_state(&A, ST_SENSOR_FAULT, 200u); /* 同じ状態のままなら解除しない */
    TEST_ASSERT_FALSE(alarm_sounding(&A));
    alarm_set_state(&A, ST_WARNING, 300u);
    alarm_set_state(&A, ST_CRITICAL, 400u); /* CRITICAL に新しく入った：解除 */
    TEST_ASSERT_TRUE(alarm_sounding(&A));
    TEST_ASSERT_TRUE(at(400u, false).buzzer);
}

static void test_same_state_keeps_phase(void)
{
    alarm_set_state(&A, ST_WARNING, 0u);
    alarm_set_state(&A, ST_WARNING, 300u); /* 変化なし：起点は動かない */
    TEST_ASSERT_FALSE(at(600u, false).led[LED_YELLOW]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_pattern);
    RUN_TEST(test_normal_solid_green);
    RUN_TEST(test_normal_force_full_blinks_2hz_from_state_entry);
    RUN_TEST(test_force_full_display_only_in_normal);
    RUN_TEST(test_warning_yellow_1hz);
    RUN_TEST(test_critical_red_4hz_buzzer_250);
    RUN_TEST(test_sensor_fault_red_solid_continuous);
    RUN_TEST(test_sounding_definition);
    RUN_TEST(test_silence_and_release);
    RUN_TEST(test_same_state_keeps_phase);
    return UNITY_END();
}
