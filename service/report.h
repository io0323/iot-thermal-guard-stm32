/**
 * @file report.h
 * @brief 周期送信（詳細設計書 第2章）。task_report（1秒）が TELEMETRY → HEARTBEAT を送り、
 *        STATE を最後に送ってから60秒たっていれば続けて STATE を送る。
 *
 * - 1秒の位相は INIT を抜けた時刻が起点（最初の送信は +1000ms、open-questions #9）。周期はスケジューラが守る
 * - INIT を抜けたときに送る STATE が60秒の起点。GET_STATUS で送った STATE は起点を動かさない
 */
#ifndef SERVICE_REPORT_H
#define SERVICE_REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_frame.h"

#define REPORT_PERIOD_MS 1000U /**< TELEMETRY・HEARTBEAT の周期 */
#define REPORT_STATE_PERIOD_MS 60000U /**< STATE の周期 */
#define REPORT_MAX_FRAMES 3U /**< 1回の report_tick で作る最大のフレーム数 */

/** 送る値の写し。 */
typedef struct {
    uint32_t uptime_ms; /**< 起動からのミリ秒 */
    int16_t temp_c100; /**< 温度（0.01℃） */
    uint8_t quality; /**< 品質 */
    uint8_t state; /**< 状態 */
    uint16_t fan_pm; /**< 最後に PWM へ書いた duty */
    uint16_t fw_version; /**< ファームウェアの版 */
    uint32_t boot_id; /**< boot_id */
} report_snapshot_t;

/** 周期送信の状態。 */
typedef struct {
    bool started; /**< INIT を抜けて周期送信を始めたか */
    uint32_t last_state_ms; /**< 周期の STATE を最後に送った時刻 */
} report_t;

/**
 * @brief 初期化する（INIT 中は何も送らない）。
 * @param r 対象
 */
void report_init(report_t *r);

/**
 * @brief INIT を抜けたときに呼ぶ。その時刻に送った STATE を60秒の起点にする。
 * @param r      対象
 * @param now_ms INIT を抜けた時刻
 */
void report_start(report_t *r, uint32_t now_ms);

/**
 * @brief task_report（1秒）から呼ぶ。送るフレームを順に返す。
 * @param r   対象
 * @param s   送る値
 * @param out 送るフレーム（TELEMETRY、HEARTBEAT、必要なら STATE の順）
 * @return フレームの数（INIT 中は 0）
 */
size_t report_tick(report_t *r, const report_snapshot_t *s, link_frame_t out[REPORT_MAX_FRAMES]);

/**
 * @brief STATE フレームを作る（起動時・GET_STATUS・周期で共通）。
 * @param s   送る値
 * @param out STATE フレーム
 */
void report_make_state(const report_snapshot_t *s, link_frame_t *out);

#endif /* SERVICE_REPORT_H */
