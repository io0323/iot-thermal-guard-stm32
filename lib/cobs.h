/**
 * @file cobs.h
 * @brief COBS（Consistent Overhead Byte Stuffing）の符号化・復号。区切りの 0x00 は含めない。
 */
#ifndef LIB_COBS_H
#define LIB_COBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 長さ n のデータを符号化したときの最大の長さ（区切りなし）。 */
#define COBS_MAX_ENCODED(n) ((n) + ((n) / 254U) + 1U)

/**
 * @brief COBS で符号化する。出力に 0x00 は含まれず、区切りの 0x00 は付けない。
 * @param in  入力
 * @param len 入力の長さ
 * @param out 出力先
 * @param cap 出力先の大きさ
 * @return 符号化後の長さ。出力先が足りなければ 0
 */
size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

/**
 * @brief COBS を復号する。入力は区切りの 0x00 を含まないこと。
 * @param in      入力（符号化済み）
 * @param len     入力の長さ
 * @param out     出力先
 * @param cap     出力先の大きさ
 * @param out_len 復号後の長さ
 * @return 成功なら true。入力に 0x00 がある、コードが入力の末尾を越える、出力先が足りないときは false
 */
bool cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *out_len);

#endif /* LIB_COBS_H */
