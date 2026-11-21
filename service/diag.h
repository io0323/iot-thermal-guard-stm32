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

#define DIAG_TASK_COUNT 6u /**< sense, control, link, ui, report, health */

/** 診断値。 */
typedef struct {
    uint8_t reset_reason;
    uint16_t reset_count;
    uint16_t rx_cobs_errors, rx_len_errors, rx_crc_errors, rx_unexpected, rx_overflow;
    uint16_t cmd_dropped_init, cmd_duplicates;
    uint16_t tx_dropped;
    uint16_t temp_clamped, temp_rejected;
    uint32_t max_loop_cycles;
    uint32_t max_task_cycles[DIAG_TASK_COUNT]; /**< sense, control, link, ui, report, health の順 */
    uint16_t overruns;
} diag_t;

/** 集める元。 */
typedef struct {
    reset_reason_t reset_reason;
    uint16_t reset_count;
    const link_counters_t *link;
    const sched_t *sched; /**< tasks は DIAG_TASK_COUNT 行より少なくてもよい（足りない分は 0） */
    uint16_t cmd_dropped_init, cmd_duplicates;
    uint16_t temp_clamped, temp_rejected;
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
