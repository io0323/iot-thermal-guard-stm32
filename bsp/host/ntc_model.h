/**
 * @file ntc_model.h
 * @brief 温度（℃）→ ADC の生値（ホスト版と sim_harness が使う回路のモデル。浮動小数点は bsp・tests だけで使う）。
 *
 * 生値 = round(4095 × R ÷ (R + 10kΩ))、R = 10kΩ × exp(3435 × (1/T − 1/298.15))（詳細設計書 第3章、open-questions #20）。
 * tools/gen_ntc_table.py と同じ式。
 */
#ifndef BSP_HOST_NTC_MODEL_H
#define BSP_HOST_NTC_MODEL_H

#include <stdint.h>

/**
 * @brief 温度を ADC の生値にする。
 * @param temp_c 温度（℃）
 * @return 生値（0〜4095）
 */
uint16_t ntc_model_raw(double temp_c);

#endif /* BSP_HOST_NTC_MODEL_H */
