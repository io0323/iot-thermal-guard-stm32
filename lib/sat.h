/**
 * @file sat.h
 * @brief 飽和カウンタ。診断値は一周させない（詳細設計書 第7章）。
 */
#ifndef LIB_SAT_H
#define LIB_SAT_H

#include <stdint.h>

/**
 * @brief u16 のカウンタを 0xFFFF で止めて1つ増やす。
 * @param c カウンタ
 */
static inline void sat_inc_u16(uint16_t *c)
{
    if (*c != UINT16_MAX) {
        (*c)++;
    }
}

#endif /* LIB_SAT_H */
