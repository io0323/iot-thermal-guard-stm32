/**
 * @file median.h
 * @brief 5点のメディアン（外れ値を除く。詳細設計書 第3章）。
 */
#ifndef LIB_MEDIAN_H
#define LIB_MEDIAN_H

#include <stdbool.h>
#include <stdint.h>

#define MEDIAN_N 5U

/** メディアンの窓。 */
typedef struct {
    int16_t buf[MEDIAN_N];
    uint8_t count;
    uint8_t next;
} median5_t;

/**
 * @brief 空にする。
 * @param m 対象
 */
void median5_init(median5_t *m);

/**
 * @brief 1値入れる（窓が満杯なら最も古い値を捨てる）。
 * @param m 対象
 * @param v 値
 */
void median5_push(median5_t *m, int16_t v);

/**
 * @brief 窓が満たされたか（5値そろったか）。
 * @param m 対象
 * @return 満たされたら true
 */
bool median5_full(const median5_t *m);

/**
 * @brief メディアン。満たされる前は入っている値のメディアン（偶数個なら小さい側）。
 * @param m 対象
 * @return 値（空なら 0）
 */
int16_t median5_value(const median5_t *m);

#endif /* LIB_MEDIAN_H */
