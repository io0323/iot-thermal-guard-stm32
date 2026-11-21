#include "ntc_table.h"

#include <stdint.h>

#include "ntc_table_data.h"

static const uint16_t k_table[NTC_POINTS] = NTC_TABLE_INIT;

uint16_t ntc_table_raw_at(int16_t temp_c)
{
    if (temp_c < NTC_T_MIN_C || temp_c > NTC_T_MAX_C) {
        return 0U;
    }
    return k_table[(uint32_t)((int32_t)temp_c - NTC_T_MIN_C)];
}

int16_t ntc_raw_to_c100(uint16_t raw)
{
    if (raw >= k_table[0]) {
        return (int16_t)(NTC_T_MIN_C * 100);
    }
    if (raw <= k_table[NTC_POINTS - 1U]) {
        return (int16_t)(NTC_T_MAX_C * 100);
    }
    /* k_table[lo] > raw > k_table[hi]、hi = lo + 1 になるまで狭める（表は単調に減る） */
    uint32_t lo = 0U;
    uint32_t hi = NTC_POINTS - 1U;
    while ((hi - lo) > 1U) {
        uint32_t mid = (lo + hi) / 2U;
        if (k_table[mid] > raw) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    uint32_t num = (uint32_t)(k_table[lo] - raw) * 100U;
    uint32_t den = (uint32_t)(k_table[lo] - k_table[hi]);
    int32_t frac = (int32_t)((num + (den / 2U)) / den); /* 四捨五入（0〜100、非負） */
    int32_t t = ((int32_t)lo + NTC_T_MIN_C) * 100 + frac;
    return (int16_t)t;
}
