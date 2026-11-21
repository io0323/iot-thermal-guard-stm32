/*
 * 温度計測（詳細設計書 第3章）。
 *
 * - 故障（短絡・断線）：生値 < 50 または > 4045 が3回連続。1〜2回目の生値は捨て、フィルタにも ready までの数にも
 *   入れない。カウンタは両方向で共通、原因は3回目の向き（#21）
 * - 範囲外（50〜237、3628〜4045）：120.00／−20.00℃ に丸めて使い、temp_clamped を数える
 * - 変化率：メディアンの出力が前回採用した値から 5.00℃ を超えて離れたら棄却し、前回採用した値を移動平均に入れる（#22）。
 *   3回連続で故障
 * - ADC 無応答：最後の値（捨てた生値も含む、#21）から300ms で故障。検出時に1回、その後は続く間1000ms ごとに
 *   adc->restart()（#25）
 * - 故障に入ったらフィルタを空にする。原因が消えても、改めて12サンプルで ready に戻るまでは故障として読む（#23）
 */
#include "temp_sensor.h"

#include <stdint.h>

#include "median.h"
#include "moving_avg.h"
#include "ntc_table.h"
#include "ntc_table_data.h"
#include "ports.h"
#include "sat.h"
#include "tick.h"

static const adc_port_t *s_adc;
static median5_t s_median;
static moving_avg8_t s_avg;
static bool s_has_last;
static uint8_t s_oor_run;  /* 短絡・断線の生値の連続回数 */
static uint8_t s_rate_run; /* 変化率の棄却の連続回数 */
static temp_fault_t s_fault;
static uint8_t s_valid_run;
static uint32_t s_last_sample_ms;
static bool s_stalled;
static uint32_t s_next_restart_ms;
static uint16_t s_clamped;
static uint16_t s_rejected;

static bool is_ready(void)
{
    return moving_avg8_full(&s_avg);
}

static void clear_filters(void)
{
    median5_init(&s_median);
    moving_avg8_init(&s_avg);
    s_has_last = false;
    s_rate_run = 0U;
    s_valid_run = 0U;
}

static void enter_fault(temp_fault_t cause)
{
    s_fault = cause;
    s_oor_run = 0U;
    clear_filters();
}

void temp_init(const adc_port_t *adc, uint32_t now_ms)
{
    s_adc = adc;
    clear_filters();
    s_oor_run = 0U;
    s_fault = TEMP_FAULT_NONE;
    s_last_sample_ms = now_ms;
    s_stalled = false;
    s_next_restart_ms = 0U;
    s_clamped = 0U;
    s_rejected = 0U;
}

/* 生値を温度にする（範囲外は丸める）。短絡・断線の範囲は呼ぶ前に除く。 */
static int16_t convert(uint16_t raw)
{
    if (raw < NTC_RAW_HOT) {
        sat_inc_u16(&s_clamped);
        return (int16_t)TEMP_CLAMP_HOT_C100;
    }
    if (raw > NTC_RAW_COLD) {
        sat_inc_u16(&s_clamped);
        return (int16_t)TEMP_CLAMP_COLD_C100;
    }
    return ntc_raw_to_c100(raw);
}

/* 短絡・断線なら true（そのサンプルは捨てる） */
static bool check_short_open(uint16_t raw)
{
    if ((raw >= TEMP_RAW_SHORT_BELOW) && (raw <= TEMP_RAW_OPEN_ABOVE)) {
        s_oor_run = 0U;
        return false;
    }
    s_valid_run = 0U;
    if (s_oor_run < UINT8_MAX) {
        s_oor_run++;
    }
    if (s_oor_run >= TEMP_FAULT_RUN) {
        enter_fault((raw < TEMP_RAW_SHORT_BELOW) ? TEMP_FAULT_SHORT : TEMP_FAULT_OPEN);
    }
    return true;
}

/* 変化率の検査。採用したら true。棄却したら前回採用した値を移動平均に入れる。 */
static bool rate_check_and_average(int16_t m)
{
    static int16_t s_last_accepted;

    int32_t diff = (int32_t)m - (int32_t)s_last_accepted;
    if (s_has_last && ((diff > TEMP_RATE_MAX_C100) || (diff < -TEMP_RATE_MAX_C100))) {
        sat_inc_u16(&s_rejected);
        s_rate_run++;
        s_valid_run = 0U;
        if (s_rate_run >= TEMP_FAULT_RUN) {
            enter_fault(TEMP_FAULT_RATE);
            return false;
        }
        moving_avg8_push(&s_avg, s_last_accepted);
        return false;
    }
    s_rate_run = 0U;
    s_last_accepted = m;
    s_has_last = true;
    moving_avg8_push(&s_avg, m);
    return true;
}

/* 取り違えの検出：raw と now_ms の取り違えは test_temp_sensor の test_ready_at_12th_sample（時刻の値が短絡として読まれる）で落ちる（open-questions #41） */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
void temp_update(uint16_t raw, uint32_t now_ms)
{
    s_last_sample_ms = now_ms;
    s_stalled = false;
    if (check_short_open(raw)) {
        return;
    }
    median5_push(&s_median, convert(raw));
    if (!median5_full(&s_median)) {
        return;
    }
    bool accepted = rate_check_and_average(median5_value(&s_median));
    if (!is_ready()) {
        return;
    }
    if (s_fault != TEMP_FAULT_NONE) {
        s_fault = TEMP_FAULT_NONE; /* 原因が消え、改めて ready になった */
        return;
    }
    if (accepted && (s_valid_run < TEMP_VALID_RUN_MAX)) {
        s_valid_run++;
    }
}

void temp_tick(uint32_t now_ms)
{
    if (!s_stalled) {
        if (tick_reached(now_ms, s_last_sample_ms + TEMP_ADC_TIMEOUT_MS)) {
            s_stalled = true;
            if ((s_fault == TEMP_FAULT_NONE) || is_ready()) {
                enter_fault(TEMP_FAULT_ADC_TIMEOUT);
            } else {
                clear_filters(); /* すでに故障中なら原因はそのまま */
            }
            s_adc->restart();
            s_next_restart_ms = now_ms + TEMP_ADC_RESTART_MS;
        }
        return;
    }
    if (tick_reached(now_ms, s_next_restart_ms)) {
        s_adc->restart();
        s_next_restart_ms += TEMP_ADC_RESTART_MS;
    }
}

void temp_get(temp_reading_t *out)
{
    bool ready = is_ready();
    bool good = ready && (s_fault == TEMP_FAULT_NONE);
    if (good) {
        out->temp_c100 = moving_avg8_value(&s_avg);
    } else {
        out->temp_c100 = (int16_t)TEMP_FAULT_C100;
    }
    out->quality = good ? (uint8_t)TEMP_QUALITY_GOOD : (uint8_t)TEMP_QUALITY_FAULT;
    out->ready = ready;
    out->fault = s_fault;
    out->valid_run = s_valid_run;
}

/* 取り違えの検出：clamped と rejected の取り違えは test_temp_sensor の test_clamp_boundaries・test_rate_rejects_three_then_fault で落ちる（open-questions #41） */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
void temp_counters(uint16_t *clamped, uint16_t *rejected)
{
    *clamped = s_clamped;
    *rejected = s_rejected;
}
