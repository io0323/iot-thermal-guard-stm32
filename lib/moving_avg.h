/**
 * @file moving_avg.h
 * @brief 8点の移動平均（整数、四捨五入。負の値は 0 を中心に対称。open-questions #27）。
 */
#ifndef LIB_MOVING_AVG_H
#define LIB_MOVING_AVG_H

#include <stdbool.h>
#include <stdint.h>

#define MOVING_AVG_N 8U /**< 移動平均の窓の大きさ */

/** 移動平均の窓。 */
typedef struct {
    int16_t buf[MOVING_AVG_N]; /**< 直近の値（リング） */
    int32_t sum; /**< buf の合計 */
    uint8_t count; /**< 入っている数（最大 MOVING_AVG_N） */
    uint8_t next; /**< 次に書く位置 */
} moving_avg8_t;

/**
 * @brief 空にする。
 * @param a 対象
 */
void moving_avg8_init(moving_avg8_t *a);

/**
 * @brief 1値入れる（窓が満杯なら最も古い値を捨てる）。
 * @param a 対象
 * @param v 値
 */
void moving_avg8_push(moving_avg8_t *a, int16_t v);

/**
 * @brief 窓が満たされたか（8値そろったか）。
 * @param a 対象
 * @return 満たされたら true
 */
bool moving_avg8_full(const moving_avg8_t *a);

/**
 * @brief 平均。満たされる前は入っている値の平均。
 * @param a 対象
 * @return 値（空なら 0）
 */
int16_t moving_avg8_value(const moving_avg8_t *a);

#endif /* LIB_MOVING_AVG_H */
