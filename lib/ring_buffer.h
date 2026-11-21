/**
 * @file ring_buffer.h
 * @brief バイトのリングバッファ。単一生産者・単一消費者でロックなし。容量は2の累乗。
 *
 * head は生産者だけが、tail は消費者だけが書く。添字は一周する u32 のまま持ち、
 * 容量ちょうどまで入れられる（満杯と空を区別するための空き1枠は要らない）。
 */
#ifndef LIB_RING_BUFFER_H
#define LIB_RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

/** リングバッファ。中身は直接さわらず、rb_* の関数を使う。 */
typedef struct {
    uint8_t *buf;            /**< 呼び出し側が用意する領域 */
    uint32_t mask;           /**< 容量 − 1 */
    volatile uint32_t head;  /**< 生産者が書く（次に入れる位置） */
    volatile uint32_t tail;  /**< 消費者が書く（次に取り出す位置） */
} ring_buffer_t;

/**
 * @brief 初期化する。
 * @param rb       対象
 * @param storage  領域（capacity バイト）
 * @param capacity 容量。2の累乗（1〜2^31）
 * @return 容量が2の累乗でなければ false
 */
bool rb_init(ring_buffer_t *rb, uint8_t *storage, uint32_t capacity);

/**
 * @brief 1バイト入れる（生産者側）。
 * @param rb 対象
 * @param b  値
 * @return 満杯なら false（入れない）
 */
bool rb_push(ring_buffer_t *rb, uint8_t b);

/**
 * @brief 1バイト取り出す（消費者側）。
 * @param rb  対象
 * @param out 取り出した値
 * @return 空なら false
 */
bool rb_pop(ring_buffer_t *rb, uint8_t *out);

/**
 * @brief 入っているバイト数。
 * @param rb 対象
 * @return バイト数
 */
uint32_t rb_count(const ring_buffer_t *rb);

/**
 * @brief 空きのバイト数。
 * @param rb 対象
 * @return バイト数
 */
uint32_t rb_free(const ring_buffer_t *rb);

#endif /* LIB_RING_BUFFER_H */
