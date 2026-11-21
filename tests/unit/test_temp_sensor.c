/*
 * temp_sensor：ready が12サンプル目、短絡・断線の3回連続（49・50・4045・4046）、範囲外の丸め（237・238・3627・3628）、
 * 変化率の棄却と3回連続での故障、ADC 無応答の300ms と1秒ごとの restart、故障でフィルタが空になること、valid_run
 */
#include "fake_ports.h"
#include "ntc_table.h"
#include "temp_sensor.h"
#include "unity.h"

static ports_t P;
static uint32_t s_t;

#define RAW_25C 2048u

void setUp(void)
{
    fake_ports_init(&P);
    s_t = 0u;
    temp_init(P.adc, 0u);
}
void tearDown(void) {}

/* 100ms ごとに raw を n 回入れる（task_sense と同じく update → tick） */
static void feed(uint16_t raw, int n)
{
    for (int i = 0; i < n; i++) {
        temp_update(raw, s_t);
        temp_tick(s_t);
        s_t += 100u;
    }
}

static temp_reading_t get(void)
{
    temp_reading_t r;
    temp_get(&r);
    return r;
}

static uint16_t clamped(void)
{
    uint16_t c = 0u;
    uint16_t r = 0u;
    temp_counters(&c, &r);
    return c;
}

static uint16_t rejected(void)
{
    uint16_t c = 0u;
    uint16_t r = 0u;
    temp_counters(&c, &r);
    return r;
}

/* ---- ready ---- */

static void test_ready_at_12th_sample(void)
{
    for (int i = 1; i <= 11; i++) {
        feed(RAW_25C, 1);
        temp_reading_t r = get();
        TEST_ASSERT_FALSE(r.ready);
        TEST_ASSERT_EQUAL_INT16(TEMP_FAULT_C100, r.temp_c100); /* ready まではまだ温度を出さない */
        TEST_ASSERT_EQUAL_UINT8(TEMP_QUALITY_FAULT, r.quality);
        TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, r.fault);
    }
    feed(RAW_25C, 1);
    temp_reading_t r = get();
    TEST_ASSERT_TRUE(r.ready);
    TEST_ASSERT_EQUAL_INT16(2500, r.temp_c100);
    TEST_ASSERT_EQUAL_UINT8(TEMP_QUALITY_GOOD, r.quality);
}

/* ---- 短絡・断線（生値の故障の境界 49・50・4045・4046） ---- */

static void test_short_needs_three_consecutive_below_50(void)
{
    feed(RAW_25C, 12);
    feed(49, 2);
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, get().fault);
    TEST_ASSERT_TRUE(get().ready);
    feed(49, 1);
    temp_reading_t r = get();
    TEST_ASSERT_EQUAL(TEMP_FAULT_SHORT, r.fault);
    TEST_ASSERT_FALSE(r.ready); /* フィルタを空にした */
    TEST_ASSERT_EQUAL_INT16(TEMP_FAULT_C100, r.temp_c100);
    TEST_ASSERT_EQUAL_UINT8(TEMP_QUALITY_FAULT, r.quality);
    TEST_ASSERT_EQUAL_UINT8(0, r.valid_run);
}

static void test_open_needs_three_consecutive_above_4045(void)
{
    feed(RAW_25C, 12);
    feed(4046, 2);
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, get().fault);
    feed(4046, 1);
    TEST_ASSERT_EQUAL(TEMP_FAULT_OPEN, get().fault);
}

static void test_50_and_4045_are_not_faults(void)
{
    feed(50, 12);
    temp_reading_t r = get();
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, r.fault);
    TEST_ASSERT_TRUE(r.ready);
    TEST_ASSERT_EQUAL_INT16(12000, r.temp_c100); /* 範囲外：120.00℃ に丸める */
    temp_init(P.adc, s_t);
    feed(4045, 12);
    r = get();
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, r.fault);
    TEST_ASSERT_EQUAL_INT16(-2000, r.temp_c100);
}

static void test_interrupted_run_does_not_fault(void)
{
    feed(RAW_25C, 12);
    feed(49, 2);
    feed(RAW_25C, 1);
    feed(4046, 2);
    feed(RAW_25C, 1);
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, get().fault);
}

static void test_mixed_directions_count_together_cause_from_third(void)
{
    feed(RAW_25C, 12);
    feed(49, 1);
    feed(4046, 1);
    feed(4095, 1); /* 3回目は断線側 */
    TEST_ASSERT_EQUAL(TEMP_FAULT_OPEN, get().fault);
}

static void test_discarded_samples_do_not_count_toward_ready(void)
{
    feed(RAW_25C, 11);
    feed(49, 2); /* 捨てる */
    TEST_ASSERT_FALSE(get().ready);
    feed(RAW_25C, 1);
    TEST_ASSERT_TRUE(get().ready);
}

static void test_discarded_samples_keep_adc_alive(void)
{
    feed(RAW_25C, 12);
    feed(49, 2);  /* 捨てた生値も「値が来た」に数える（#21） */
    feed(RAW_25C, 1);
    feed(49, 2);
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, get().fault);
    TEST_ASSERT_EQUAL_UINT32(0, g_fake.adc_restarts);
}

/* ---- 範囲外の丸め（237・238・3627・3628） ---- */

static void check_clamp(uint16_t raw, int16_t expect_c100, uint16_t expect_clamped)
{
    temp_init(P.adc, s_t);
    feed(raw, 12);
    temp_reading_t r = get();
    TEST_ASSERT_TRUE(r.ready);
    TEST_ASSERT_EQUAL_INT16(expect_c100, r.temp_c100);
    TEST_ASSERT_EQUAL_UINT16(expect_clamped, clamped());
}

static void test_clamp_boundaries(void)
{
    check_clamp(237, 12000, 12);   /* 50〜237 は丸めて数える */
    check_clamp(238, 12000, 0);    /* 表の端（120℃）ちょうど */
    check_clamp(3627, -2000, 0);   /* 表の端（−20℃）ちょうど */
    check_clamp(3628, -2000, 12);  /* 3628〜4045 は丸めて数える */
    check_clamp(239, ntc_raw_to_c100(239), 0);
    check_clamp(3626, ntc_raw_to_c100(3626), 0);
}

/* ---- 変化率 ---- */

static void test_rate_rejects_three_then_fault(void)
{
    feed(RAW_25C, 12);
    uint16_t raw31 = ntc_table_raw_at(31); /* 25℃ から 6℃ 上 */
    feed(raw31, 2);                         /* メディアンはまだ 25℃ */
    TEST_ASSERT_EQUAL_UINT16(0, rejected());
    feed(raw31, 1);                         /* メディアンが 31℃ → 棄却 1 */
    TEST_ASSERT_EQUAL_UINT16(1, rejected());
    temp_reading_t r = get();
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, r.fault);
    TEST_ASSERT_EQUAL_INT16(2500, r.temp_c100); /* 前回採用した値を移動平均に入れた */
    TEST_ASSERT_EQUAL_UINT8(0, r.valid_run);
    feed(raw31, 1);
    TEST_ASSERT_EQUAL_UINT16(2, rejected());
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, get().fault);
    feed(raw31, 1);
    TEST_ASSERT_EQUAL_UINT16(3, rejected());
    r = get();
    TEST_ASSERT_EQUAL(TEMP_FAULT_RATE, r.fault);
    TEST_ASSERT_FALSE(r.ready);
}

static void test_rate_exactly_5c_is_accepted(void)
{
    feed(RAW_25C, 12);
    feed(ntc_table_raw_at(30), 5); /* ちょうど 5.00℃ */
    TEST_ASSERT_EQUAL_UINT16(0, rejected());
    temp_init(P.adc, s_t);
    feed(RAW_25C, 12);
    feed((uint16_t)(ntc_table_raw_at(30) - 1u), 3); /* 5.00℃ を少し超える */
    TEST_ASSERT_EQUAL_UINT16(1, rejected());
}

static void test_rate_run_resets_when_back(void)
{
    feed(RAW_25C, 12);
    uint16_t raw31 = ntc_table_raw_at(31);
    /* 31,25,25,31,31,25 を繰り返すと、メディアンは「棄却・棄却・採用」をくり返す（連続は2回まで） */
    const uint16_t pattern[6] = {raw31, RAW_25C, RAW_25C, raw31, raw31, RAW_25C};
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 6; i++) {
            feed(pattern[i], 1);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(10, rejected());
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, get().fault);
}

static void test_slow_ramp_passes(void)
{
    feed(RAW_25C, 12);
    for (int16_t t = 25; t <= 70; t++) { /* 1℃/100ms（10℃/s） */
        feed(ntc_table_raw_at(t), 1);
    }
    feed(ntc_table_raw_at(70), 12);
    TEST_ASSERT_EQUAL_UINT16(0, rejected());
    TEST_ASSERT_EQUAL_INT16(7000, get().temp_c100);
}

/* ---- ADC 無応答 ---- */

static void test_adc_timeout_300ms_and_restart_every_second(void)
{
    feed(RAW_25C, 12); /* 最後の値は 1100ms */
    uint32_t last = s_t - 100u;
    for (uint32_t t = last + 10u; t < last + 300u; t += 10u) {
        temp_tick(t);
    }
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, get().fault);
    TEST_ASSERT_EQUAL_UINT32(0, g_fake.adc_restarts);
    temp_tick(last + 300u);
    temp_reading_t r = get();
    TEST_ASSERT_EQUAL(TEMP_FAULT_ADC_TIMEOUT, r.fault);
    TEST_ASSERT_FALSE(r.ready);
    TEST_ASSERT_EQUAL_UINT32(1, g_fake.adc_restarts); /* 検出時に1回 */
    for (uint32_t t = last + 310u; t < last + 1300u; t += 10u) {
        temp_tick(t);
    }
    TEST_ASSERT_EQUAL_UINT32(1, g_fake.adc_restarts);
    temp_tick(last + 1300u);
    TEST_ASSERT_EQUAL_UINT32(2, g_fake.adc_restarts);
    for (uint32_t t = last + 1310u; t <= last + 2300u; t += 10u) {
        temp_tick(t);
    }
    TEST_ASSERT_EQUAL_UINT32(3, g_fake.adc_restarts);
    /* 再開：12サンプルで ready に戻るまでは故障として読む（#23） */
    s_t = last + 2400u;
    feed(RAW_25C, 11);
    r = get();
    TEST_ASSERT_EQUAL(TEMP_FAULT_ADC_TIMEOUT, r.fault);
    TEST_ASSERT_EQUAL_INT16(TEMP_FAULT_C100, r.temp_c100);
    feed(RAW_25C, 1);
    r = get();
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, r.fault);
    TEST_ASSERT_TRUE(r.ready);
    TEST_ASSERT_EQUAL_INT16(2500, r.temp_c100);
    TEST_ASSERT_EQUAL_UINT32(3, g_fake.adc_restarts);
}

static void test_adc_never_responds_from_boot(void)
{
    temp_tick(299u);
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, get().fault);
    temp_tick(300u);
    TEST_ASSERT_EQUAL(TEMP_FAULT_ADC_TIMEOUT, get().fault);
    TEST_ASSERT_EQUAL_UINT32(1, g_fake.adc_restarts);
}

/* ---- 故障でフィルタが空になる・原因が消えても ready まで故障のまま ---- */

static void test_fault_empties_filters_and_holds_until_ready(void)
{
    feed(RAW_25C, 12);
    feed(4095, 3);
    TEST_ASSERT_EQUAL(TEMP_FAULT_OPEN, get().fault);
    feed(4095, 20);
    TEST_ASSERT_EQUAL(TEMP_FAULT_OPEN, get().fault);
    feed(RAW_25C, 11);
    temp_reading_t r = get();
    TEST_ASSERT_FALSE(r.ready);
    TEST_ASSERT_EQUAL(TEMP_FAULT_OPEN, r.fault);
    TEST_ASSERT_EQUAL_UINT8(TEMP_QUALITY_FAULT, r.quality);
    feed(RAW_25C, 1);
    r = get();
    TEST_ASSERT_TRUE(r.ready);
    TEST_ASSERT_EQUAL(TEMP_FAULT_NONE, r.fault);
    TEST_ASSERT_EQUAL_INT16(2500, r.temp_c100);
}

/* ---- valid_run ---- */

static void test_valid_run_counts_and_resets(void)
{
    feed(RAW_25C, 12);
    TEST_ASSERT_EQUAL_UINT8(1, get().valid_run); /* ready になったサンプルから数える */
    feed(RAW_25C, 9);
    TEST_ASSERT_EQUAL_UINT8(10, get().valid_run);
    feed(49, 1); /* 捨てたサンプルで連続が切れる */
    TEST_ASSERT_EQUAL_UINT8(0, get().valid_run);
    feed(RAW_25C, 3);
    TEST_ASSERT_EQUAL_UINT8(3, get().valid_run);
    feed(ntc_table_raw_at(31), 3); /* 棄却で切れる */
    TEST_ASSERT_EQUAL_UINT8(0, get().valid_run);
}

static void test_valid_run_saturates(void)
{
    feed(RAW_25C, 12 + 300);
    TEST_ASSERT_EQUAL_UINT8(255, get().valid_run);
}

static void test_valid_run_after_fault_restarts_from_ready(void)
{
    feed(RAW_25C, 30);
    feed(0, 3);
    TEST_ASSERT_EQUAL_UINT8(0, get().valid_run);
    feed(RAW_25C, 12); /* ready に戻ったサンプルでは故障を解くだけ */
    TEST_ASSERT_EQUAL_UINT8(0, get().valid_run);
    feed(RAW_25C, 10);
    TEST_ASSERT_EQUAL_UINT8(10, get().valid_run);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ready_at_12th_sample);
    RUN_TEST(test_short_needs_three_consecutive_below_50);
    RUN_TEST(test_open_needs_three_consecutive_above_4045);
    RUN_TEST(test_50_and_4045_are_not_faults);
    RUN_TEST(test_interrupted_run_does_not_fault);
    RUN_TEST(test_mixed_directions_count_together_cause_from_third);
    RUN_TEST(test_discarded_samples_do_not_count_toward_ready);
    RUN_TEST(test_discarded_samples_keep_adc_alive);
    RUN_TEST(test_clamp_boundaries);
    RUN_TEST(test_rate_rejects_three_then_fault);
    RUN_TEST(test_rate_exactly_5c_is_accepted);
    RUN_TEST(test_rate_run_resets_when_back);
    RUN_TEST(test_slow_ramp_passes);
    RUN_TEST(test_adc_timeout_300ms_and_restart_every_second);
    RUN_TEST(test_adc_never_responds_from_boot);
    RUN_TEST(test_fault_empties_filters_and_holds_until_ready);
    RUN_TEST(test_valid_run_counts_and_resets);
    RUN_TEST(test_valid_run_saturates);
    RUN_TEST(test_valid_run_after_fault_restarts_from_ready);
    return UNITY_END();
}
