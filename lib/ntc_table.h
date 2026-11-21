/**
 * @file ntc_table.h
 * @brief NTC の生値 → 温度（0.01℃）の変換（詳細設計書 第3章）。表は tools/gen_ntc_table.py の生成物。
 *
 * 表は1℃刻み（−20〜120℃、141点）。二分探索して隣り合う2点で線形補間し、四捨五入する（open-questions #27）。
 */
#ifndef LIB_NTC_TABLE_H
#define LIB_NTC_TABLE_H

#include <stdint.h>

#include "ntc_table_data.h"

/**
 * @brief 生値を温度に変換する。
 * @param raw ADC の生値。NTC_RAW_HOT（120℃）〜 NTC_RAW_COLD（−20℃）の外は端の温度にする
 * @return 温度（0.01℃）。−2000〜12000
 */
int16_t ntc_raw_to_c100(uint16_t raw);

/**
 * @brief 表の値（テスト用）。
 * @param temp_c 温度（℃、NTC_T_MIN_C〜NTC_T_MAX_C）
 * @return その温度の生値。範囲外なら 0
 */
uint16_t ntc_table_raw_at(int16_t temp_c);

#endif /* LIB_NTC_TABLE_H */
