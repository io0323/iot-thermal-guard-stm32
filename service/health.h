/**
 * @file health.h
 * @brief 生存報告とウォッチドッグのリフレッシュの判定（詳細設計書 第7章）。
 *
 * - sense・control・link・ui は実行の最後に health_checkin() で自分のビットを立てる
 * - task_health（100ms）は health_check() が true のときだけ `wdt->refresh()` を呼ぶ。true になるのは、4つのビットが
 *   そろっていて、report の最後の実行（まだなければ INIT を抜けた時刻）から1500ms 以内（ちょうど1500ms を含む、#39）のとき。
 *   そのときビットを消す。そろっていなければビットは残す
 * - INIT の間は起動処理が直接リフレッシュするので、この判定は使わない（基準の時刻がまだない）
 */
#ifndef SERVICE_HEALTH_H
#define SERVICE_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#define HEALTH_REPORT_DEADLINE_MS 1500u /**< report の最後の実行からの期限 */

/** 生存報告をするタスク（周期が100ms 以下のもの）。 */
typedef enum { HEALTH_SENSE, HEALTH_CONTROL, HEALTH_LINK, HEALTH_UI, HEALTH_TASK_COUNT } health_task_t;

/** ヘルスの状態。 */
typedef struct {
    uint8_t bits;
    bool has_report_ref;
    uint32_t report_ref_ms; /**< report の最後の実行、まだなければ INIT を抜けた時刻 */
} health_t;

/**
 * @brief 初期化する（ビットなし、基準の時刻なし）。
 * @param h 対象
 */
void health_init(health_t *h);

/**
 * @brief 生存報告。
 * @param h    対象
 * @param task 報告するタスク
 */
void health_checkin(health_t *h, health_task_t task);

/**
 * @brief report の期限の基準を now にする（INIT を抜けたときと、report を実行したとき）。
 * @param h   対象
 * @param now 時刻
 */
void health_report_ran(health_t *h, uint32_t now);

/**
 * @brief リフレッシュしてよいかを判定する。true なら4つのビットを消す。
 * @param h   対象
 * @param now 時刻
 * @return リフレッシュしてよければ true
 */
bool health_check(health_t *h, uint32_t now);

/**
 * @brief ビットを消す（INIT の間、起動処理が直接リフレッシュしたとき）。
 * @param h 対象
 */
void health_clear(health_t *h);

#endif /* SERVICE_HEALTH_H */
