/**
 * @file diag.h
 * @brief 診断値を1か所に集める（詳細設計書 第7章の表）。
 *
 * カウンタは発生元のモジュール（link・command_handler・temp_sensor・scheduler）が `sat_inc_u16` で飽和させて持ち、
 * diag_collect() がそれを1つの構造体に写す。どのカウンタも一周させない。
 * 送り出す手段は CR-1（GET_DIAG の応答）が決まるまでなく、ホスト版のログとデバッガで読む。
 */
#ifndef SERVICE_DIAG_H
#define SERVICE_DIAG_H

#include <stdint.h>

#include "link.h"
#include "ports.h"
#include "scheduler.h"

#define DIAG_TASK_COUNT 6U /**< sense, control, link, ui, report, health */

/** 診断値。 */
typedef struct {
    uint8_t reset_reason; /**< リセット要因（reset_reason_t の値） */
    uint16_t reset_count; /**< リセット回数 */
    uint16_t rx_cobs_errors; /**< COBS の復号で捨てた件数 */
    uint16_t rx_len_errors; /**< 長さが合わずに捨てた件数 */
    uint16_t rx_crc_errors; /**< CRC が合わずに捨てた件数 */
    uint16_t rx_unexpected; /**< 受け付けない種別で捨てた件数 */
    uint16_t rx_overflow; /**< 受信バッファがあふれて捨てた件数 */
    uint16_t cmd_dropped_init; /**< INIT 中に捨てた CMD の件数 */
    uint16_t cmd_duplicates; /**< 重複として前回の ACK を返した件数 */
    uint16_t tx_dropped; /**< 送信キューで捨てた件数 */
    uint16_t temp_clamped; /**< 範囲外で丸めた件数 */
    uint16_t temp_rejected; /**< 変化率で棄却した件数 */
    uint32_t max_loop_cycles; /**< ループ1周の実行時間の最大（cycles） */
    uint32_t max_task_cycles[DIAG_TASK_COUNT]; /**< sense, control, link, ui, report, health の順 */
    uint16_t overruns; /**< オーバーランの件数 */
    uint16_t uart_ring_dropped; /**< 受信リングが満杯で捨てたバイト数（bsp、rx_overflow とは別。#45） */
    uint16_t uart_errors; /**< UART の受信エラーの件数（bsp。#46） */
    uint16_t stack_peak; /**< スタックの最大使用量（バイト。bsp。#47） */
} diag_t;

/** 集める元。 */
typedef struct {
    reset_reason_t reset_reason; /**< リセット要因 */
    uint16_t reset_count; /**< リセット回数 */
    const link_counters_t *link; /**< link の受信・送信のカウンタ */
    const sched_t *sched; /**< tasks は DIAG_TASK_COUNT 行より少なくてもよい（足りない分は 0） */
    uint16_t cmd_dropped_init; /**< INIT 中に捨てた CMD の件数 */
    uint16_t cmd_duplicates; /**< 重複として前回の ACK を返した件数 */
    uint16_t temp_clamped; /**< 範囲外で丸めた件数 */
    uint16_t temp_rejected; /**< 変化率で棄却した件数 */
    bsp_diag_t bsp; /**< bsp の診断値（bsp_diag_port_t で読んだもの） */
} diag_sources_t;

/**
 * @brief 診断値を集める。
 * @param out 診断値
 * @param src 集める元
 */
void diag_collect(diag_t *out, const diag_sources_t *src);

/**
 * @brief 再起動したあとのリセット回数（0xFFFF で止める）。
 * @param count いまの回数
 * @return 次の回数
 */
uint16_t diag_next_reset_count(uint16_t count);

#endif /* SERVICE_DIAG_H */
