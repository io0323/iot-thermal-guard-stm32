/**
 * @file temp_sensor.h
 * @brief 温度計測（詳細設計書 第3章）。インターフェースは本版と同じ。
 *
 * P1 は仮版（temp_sensor.c の注記を参照）。
 */
#ifndef SERVICE_TEMP_SENSOR_H
#define SERVICE_TEMP_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#define TEMP_FAULT_C100 (-32768)
#define TEMP_QUALITY_GOOD 0u
#define TEMP_QUALITY_FAULT 2u
#define TEMP_READY_SAMPLES 12u

/** 故障の原因。 */
typedef enum { TEMP_FAULT_NONE, TEMP_FAULT_SHORT, TEMP_FAULT_OPEN, TEMP_FAULT_RATE, TEMP_FAULT_ADC_TIMEOUT } temp_fault_t;

/** 温度の読み。 */
typedef struct {
    int16_t temp_c100;
    uint8_t quality;
    bool ready;
    temp_fault_t fault;
    uint8_t valid_run;
} temp_reading_t;

/**
 * @brief 起動時に初期化する（フィルタを空にする）。
 */
void temp_init(void);

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

#endif /* SERVICE_TEMP_SENSOR_H */
