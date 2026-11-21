/**
 * @file upstream.h
 * @brief 上位（作品03）の監視。UNKNOWN／ALIVE／LOST の3状態（詳細設計書 第6章の表）。
 *
 * | 状態    | HEARTBEAT を受けた        | 最後の受信（未受信なら起動時刻）から10秒       |
 * | UNKNOWN | ALIVE。EVENT なし          | LOST。手動指定を解除し UPSTREAM_LOST          |
 * | ALIVE   | 時刻を更新                 | LOST。手動指定を解除し UPSTREAM_LOST          |
 * | LOST    | ALIVE。UPSTREAM_RESTORED   | 何もしない                                    |
 *
 * 手動指定の解除と EVENT の送信は呼び出し側が戻り値を見て行う。
 */
#ifndef SERVICE_UPSTREAM_H
#define SERVICE_UPSTREAM_H

#include <stdint.h>

#define UPSTREAM_LOST_MS 10000U /**< HEARTBEAT がこの時間来なければ上位断 */

/** 上位の状態。 */
typedef enum { UPSTREAM_UNKNOWN, UPSTREAM_ALIVE, UPSTREAM_LOST } upstream_state_t;

/** 状態の変化で呼び出し側がすること。 */
typedef enum { UPSTREAM_EV_NONE, UPSTREAM_EV_LOST, UPSTREAM_EV_RESTORED } upstream_event_t;

/** 上位監視の状態。 */
typedef struct {
    upstream_state_t state; /**< いまの状態 */
    uint32_t last_ms; /**< 最後の受信（未受信なら起動時刻） */
} upstream_t;

/**
 * @brief 起動時に初期化する（UNKNOWN、起点は起動時刻）。
 * @param u      対象
 * @param now_ms 起動時刻
 */
void upstream_init(upstream_t *u, uint32_t now_ms);

/**
 * @brief HEARTBEAT を受けたときに呼ぶ。
 * @param u      対象
 * @param now_ms 受信時刻
 * @return LOST から戻ったときだけ UPSTREAM_EV_RESTORED
 */
upstream_event_t upstream_on_heartbeat(upstream_t *u, uint32_t now_ms);

/**
 * @brief 途絶を判定する（task_link が10ms ごとに呼ぶ）。
 * @param u      対象
 * @param now_ms 現在時刻
 * @return UNKNOWN・ALIVE で10秒たったら UPSTREAM_EV_LOST
 */
upstream_event_t upstream_tick(upstream_t *u, uint32_t now_ms);

/**
 * @brief いまの状態。
 * @param u 対象
 * @return 状態
 */
upstream_state_t upstream_state(const upstream_t *u);

#endif /* SERVICE_UPSTREAM_H */
