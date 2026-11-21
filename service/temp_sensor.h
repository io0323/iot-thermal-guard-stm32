/**
 * @file temp_sensor.h
 * @brief 温度計測（詳細設計書 第3章、open-questions #20〜#25・#27）。
 *
 * 100ms ごとに1値：故障判定（生値）→ 範囲外の丸め → 変換 → メディアン5点 → 変化率の検査 → 移動平均8点。
 * すべて整数で、温度は 0.01℃ 単位の int16_t。
 */
#ifndef SERVICE_TEMP_SENSOR_H
#define SERVICE_TEMP_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ports.h"

#define TEMP_FAULT_C100 (-32768) /**< 故障中・ready でないときの温度 */
#define TEMP_QUALITY_GOOD 0U /**< 品質：良好 */
#define TEMP_QUALITY_FAULT 2U /**< 品質：故障 */

#define TEMP_RAW_SHORT_BELOW 50U   /**< 生値 < 50 は短絡 */
#define TEMP_RAW_OPEN_ABOVE 4045U  /**< 生値 > 4045 は断線 */
#define TEMP_FAULT_RUN 3U          /**< 短絡・断線、変化率の棄却がこの回数続いたら故障 */
#define TEMP_RATE_MAX_C100 500     /**< 前回採用した値との差がこれを超えたら棄却 */
#define TEMP_ADC_TIMEOUT_MS 300U   /**< 新しい値がこの時間来なければ故障 */
#define TEMP_ADC_RESTART_MS 1000U  /**< 無応答が続く間、この周期で adc->restart() */
#define TEMP_CLAMP_HOT_C100 12000 /**< 表の高温側の端に丸めた温度（120.00℃） */
#define TEMP_CLAMP_COLD_C100 (-2000) /**< 表の低温側の端に丸めた温度（−20.00℃） */
#define TEMP_VALID_RUN_MAX 255U /**< valid_run の上限（飽和） */

/** 故障の原因。 */
typedef enum { TEMP_FAULT_NONE, TEMP_FAULT_SHORT, TEMP_FAULT_OPEN, TEMP_FAULT_RATE, TEMP_FAULT_ADC_TIMEOUT } temp_fault_t;

/** 温度の読み。 */
typedef struct {
    int16_t temp_c100;  /**< 故障中・ready でないときは −32768 */
    uint8_t quality;    /**< 0 good、2 fault（1 stale は使わない） */
    bool ready;         /**< メディアンと移動平均が満たされた（12サンプル目から） */
    temp_fault_t fault; /**< 故障の原因。原因が消えても ready に戻るまでは残す（#23） */
    uint8_t valid_run;  /**< ready で故障条件のないサンプルの連続回数（255 で止める） */
} temp_reading_t;

/**
 * @brief 起動時に初期化する（フィルタを空にし、無応答の起点を now_ms にする）。
 * @param adc    ADC のポート（無応答のときの restart に使う）
 * @param now_ms 起動時刻
 */
void temp_init(const adc_port_t *adc, uint32_t now_ms);

/**
 * @brief ADC の値が来たときに呼ぶ。
 * @param raw    ADC の生値（12ビット）
 * @param now_ms 時刻
 */
void temp_update(uint16_t raw, uint32_t now_ms);

/**
 * @brief 無応答の検出。task_sense が毎回呼ぶ。
 * @param now_ms 時刻
 */
void temp_tick(uint32_t now_ms);

/**
 * @brief いまの読みを返す。
 * @param out 読み
 */
void temp_get(temp_reading_t *out);

/**
 * @brief 診断値（飽和する）。
 * @param clamped  範囲外で丸めた件数
 * @param rejected 変化率で棄却した件数
 */
void temp_counters(uint16_t *clamped, uint16_t *rejected);

#endif /* SERVICE_TEMP_SENSOR_H */
