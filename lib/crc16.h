/**
 * @file crc16.h
 * @brief CRC-16/CCITT-FALSE（多項式 0x1021、初期値 0xFFFF、反転なし、XOR なし）。
 */
#ifndef LIB_CRC16_H
#define LIB_CRC16_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief CRC-16/CCITT-FALSE を計算する。
 * @param data 対象のバイト列（len が 0 なら NULL でもよい）
 * @param len  バイト数
 * @return CRC 値（"123456789" で 0x29B1）
 */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len);

#endif /* LIB_CRC16_H */
