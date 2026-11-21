/**
 * @file crc32.h
 * @brief CRC-32（IEEE 802.3、反射多項式 0xEDB88320、初期値・最終 XOR 0xFFFFFFFF）。
 */
#ifndef LIB_CRC32_H
#define LIB_CRC32_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief CRC-32（IEEE）を計算する。表を持たないビット単位の実装（Flash を節約する）。
 * @param data 対象のバイト列（len が 0 なら NULL でもよい）
 * @param len  バイト数
 * @return CRC 値（"123456789" で 0xCBF43926）
 */
uint32_t crc32_ieee(const uint8_t *data, size_t len);

#endif /* LIB_CRC32_H */
