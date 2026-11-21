/**
 * @file byteorder.h
 * @brief リトルエンディアンの読み書き。フレームは構造体を memcpy せず、この関数で1項目ずつ組み立てる。
 */
#ifndef LIB_BYTEORDER_H
#define LIB_BYTEORDER_H

#include <stdint.h>

/**
 * @brief u16 をリトルエンディアンで書く。
 * @param p 書き込み先（2バイト）
 * @param v 値
 */
static inline void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/**
 * @brief i16 をリトルエンディアンで書く。
 * @param p 書き込み先（2バイト）
 * @param v 値
 */
static inline void put_i16le(uint8_t *p, int16_t v)
{
    put_u16le(p, (uint16_t)v);
}

/**
 * @brief u32 をリトルエンディアンで書く。
 * @param p 書き込み先（4バイト）
 * @param v 値
 */
static inline void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

/**
 * @brief リトルエンディアンの u16 を読む。
 * @param p 読み出し元（2バイト）
 * @return 値
 */
static inline uint16_t get_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

/**
 * @brief リトルエンディアンの i16 を読む。
 * @param p 読み出し元（2バイト）
 * @return 値
 */
static inline int16_t get_i16le(const uint8_t *p)
{
    return (int16_t)get_u16le(p);
}

/**
 * @brief リトルエンディアンの u32 を読む。
 * @param p 読み出し元（4バイト）
 * @return 値
 */
static inline uint32_t get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#endif /* LIB_BYTEORDER_H */
