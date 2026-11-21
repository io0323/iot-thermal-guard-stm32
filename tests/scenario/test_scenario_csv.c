/* シナリオ CSV（open-questions #28・#33）：ヘッダ、注釈、CRLF、補間、event（button・hang を含む）、不正な CSV、生値のモデル */
#include <stdio.h>
#include <string.h>

#include "ntc_model.h"
#include "scenario.h"
#include "unity.h"

static scenario_t S;
static char E[160];

void setUp(void)
{
    E[0] = '\0';
}
void tearDown(void) {}

static void test_header_comments_blank_and_crlf(void)
{
    const char *text = "t_ms,temp_c,event\r\n# 注釈\r\n\r\n0,25,\r\n1000,35\r\n2000, 45 ,sensor_open\r\n";
    TEST_ASSERT_TRUE_MESSAGE(scenario_parse(&S, text, E, sizeof E), E);
    TEST_ASSERT_EQUAL_size_t(3, S.n);
    TEST_ASSERT_EQUAL_UINT32(2000, S.rows[2].t_ms);
    TEST_ASSERT_EQUAL(SC_EV_SENSOR_OPEN, S.rows[2].ev);
    TEST_ASSERT_EQUAL(SC_EV_NONE, S.rows[1].ev);
}

static void test_no_header_and_no_trailing_newline(void)
{
    TEST_ASSERT_TRUE_MESSAGE(scenario_parse(&S, "0,25\n500,30", E, sizeof E), E);
    TEST_ASSERT_EQUAL_size_t(2, S.n);
}

static void test_linear_interpolation_and_hold(void)
{
    TEST_ASSERT_TRUE(scenario_parse(&S, "1000,20\n2000,30\n4000,10\n", E, sizeof E));
    TEST_ASSERT_TRUE(scenario_temp_at(&S, 0) == 20.0);    /* 最初の行より前は最初の温度 */
    TEST_ASSERT_TRUE(scenario_temp_at(&S, 1500) == 25.0);
    TEST_ASSERT_TRUE(scenario_temp_at(&S, 3000) == 20.0);
    TEST_ASSERT_TRUE(scenario_temp_at(&S, 99999) == 10.0); /* 最後の行のあとは最後の温度 */
}

static void check_bad(const char *text, const char *needle)
{
    TEST_ASSERT_FALSE(scenario_parse(&S, text, E, sizeof E));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(E, needle), E);
}

static void test_errors(void)
{
    check_bad("0,25\n1000,,sensor_open\n", "temp_c");      /* temp_c は全行に必須 */
    check_bad("0,25\n0,26\n", "増えていない");              /* 同じ t_ms */
    check_bad("1000,25\n500,26\n", "増えていない");
    check_bad("0,25,melt\n", "知らない event");
    check_bad("-5,25\n", "t_ms");
    check_bad("abc,25\n", "t_ms");
    check_bad("0\n", "t_ms,temp_c");
    check_bad("0,25,,x\n", "列が多い");
    check_bad("# 注釈だけ\n", "行がない");
    check_bad("", "行がない");
}

static void test_player_events(void)
{
    TEST_ASSERT_TRUE(scenario_parse(&S,
                                    "0,25\n100,25,sensor_open\n200,25,sensor_short\n300,25,sensor_ok\n"
                                    "400,25,adc_stall\n500,25,adc_resume\n",
                                    E, sizeof E));
    scenario_player_t p;
    scenario_player_init(&p, &S);
    uint16_t raw = 0u;
    scenario_player_advance(&p, 0);
    TEST_ASSERT_TRUE(scenario_player_sample(&p, 0, &raw));
    TEST_ASSERT_EQUAL_UINT16(2048, raw);
    scenario_player_advance(&p, 100);
    TEST_ASSERT_TRUE(scenario_player_sample(&p, 100, &raw));
    TEST_ASSERT_EQUAL_UINT16(4095, raw);
    scenario_player_advance(&p, 250);
    TEST_ASSERT_TRUE(scenario_player_sample(&p, 250, &raw));
    TEST_ASSERT_EQUAL_UINT16(0, raw);
    scenario_player_advance(&p, 300);
    TEST_ASSERT_TRUE(scenario_player_sample(&p, 300, &raw));
    TEST_ASSERT_EQUAL_UINT16(2048, raw);
    scenario_player_advance(&p, 450);
    TEST_ASSERT_FALSE(scenario_player_sample(&p, 450, &raw));
    scenario_player_advance(&p, 500);
    TEST_ASSERT_TRUE(scenario_player_sample(&p, 500, &raw));
}

/* P3：button_down・button_up・hang（P2 の間は未対応にしていた。#33） */
static void test_player_button_and_hang(void)
{
    TEST_ASSERT_TRUE_MESSAGE(scenario_parse(&S, "0,25\n100,25,button_down\n3200,25,button_up\n5000,25,hang\n", E, sizeof E),
                             E);
    scenario_player_t p;
    scenario_player_init(&p, &S);
    scenario_player_advance(&p, 99);
    TEST_ASSERT_FALSE(p.button);
    scenario_player_advance(&p, 100);
    TEST_ASSERT_TRUE(p.button);
    scenario_player_advance(&p, 3199);
    TEST_ASSERT_TRUE(p.button);
    scenario_player_advance(&p, 3200);
    TEST_ASSERT_FALSE(p.button);
    TEST_ASSERT_FALSE(p.hang);
    scenario_player_advance(&p, 5000);
    TEST_ASSERT_TRUE(p.hang);
    uint16_t raw = 0u;
    TEST_ASSERT_TRUE(scenario_player_sample(&p, 5000, &raw)); /* hang は ADC の値を変えない */
    TEST_ASSERT_EQUAL_UINT16(2048, raw);
}

static void test_model_matches_design(void)
{
    TEST_ASSERT_EQUAL_UINT16(3627, ntc_model_raw(-20.0));
    TEST_ASSERT_EQUAL_UINT16(2048, ntc_model_raw(25.0));
    TEST_ASSERT_EQUAL_UINT16(238, ntc_model_raw(120.0));
    TEST_ASSERT_TRUE(ntc_model_raw(-40.0) > 3627u); /* 範囲外は表の外に出る */
    TEST_ASSERT_TRUE(ntc_model_raw(150.0) < 238u);
}

static void test_all_data_files_load(void)
{
    const char *files[] = {"nfr01_ramp_1cps.csv", "nfr01_ramp_10cps.csv", "nfr01_ramp_40cps.csv",
                           "alarm_cycle.csv",     "warning_hysteresis.csv", "fault_open.csv",
                           "fault_short.csv",     "fault_adc_stall.csv",  "jump_25c.csv",
                           "init_adc_stall.csv",  "constant_25c.csv",       "ack_flow.csv",
                           "ack_not_met.csv",     "fault_recover_early_ack.csv", "fault_recover_ack.csv",
                           "long_press.csv",      "long_press_critical.csv", "hang_reboot.csv"};
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        char path[512];
        (void)snprintf(path, sizeof path, "%s/%s", SCENARIO_DIR, files[i]);
        TEST_ASSERT_TRUE_MESSAGE(scenario_load(&S, path, E, sizeof E), E);
    }
    TEST_ASSERT_FALSE(scenario_load(&S, SCENARIO_DIR "/no_such_file.csv", E, sizeof E));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_comments_blank_and_crlf);
    RUN_TEST(test_no_header_and_no_trailing_newline);
    RUN_TEST(test_linear_interpolation_and_hold);
    RUN_TEST(test_errors);
    RUN_TEST(test_player_events);
    RUN_TEST(test_player_button_and_hang);
    RUN_TEST(test_model_matches_design);
    RUN_TEST(test_all_data_files_load);
    return UNITY_END();
}
