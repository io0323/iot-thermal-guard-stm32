/**
 * @file scheduler.h
 * @brief 静的なタスク表の協調型スケジューラ（詳細設計書 第2章）。
 *
 * - `now - next >= 0` なら実行し `next += period`（`now + period` にすると周期がずれていく）
 * - 遅れて `next` が過去のままなら、追いつくまで実行せず、位相を保って次の未来の周期へ進め、オーバーランを数える
 * - 同じミリ秒に来たタスクは表の順に実行する
 * - 各タスクとループ1周の実行時間を `cycles()` で測り、最大値を残す
 */
#ifndef SERVICE_SCHEDULER_H
#define SERVICE_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ports.h"

/** タスクの関数。引数は sched_run を呼んだ時刻。 */
typedef void (*sched_fn_t)(uint32_t now_ms);

/** 表の1行。 */
typedef struct {
    sched_fn_t fn; /**< タスクの関数 */
    uint32_t period_ms; /**< 周期（ms） */
    uint32_t next_ms; /**< 次に実行する時刻 */
    bool enabled; /**< 有効か（sched_start で有効にする） */
    uint32_t max_cycles; /**< 1回の実行時間の最大値 */
} sched_task_t;

/** スケジューラ。 */
typedef struct {
    sched_task_t *tasks; /**< タスク表 */
    size_t count; /**< タスク表の行数 */
    const tick_port_t *tick; /**< 時間のポート */
    uint16_t overruns; /**< オーバーランの件数（飽和） */
    uint32_t max_loop_cycles; /**< ループ1周の実行時間の最大（cycles） */
} sched_t;

/**
 * @brief 初期化する。表の各行の max_cycles を 0 にする（next_ms・enabled は呼び出し側が設定しておく）。
 * @param s     対象
 * @param tasks タスク表（表の順に実行する）
 * @param count 行数
 * @param tick  時間のポート
 */
void sched_init(sched_t *s, sched_task_t *tasks, size_t count, const tick_port_t *tick);

/**
 * @brief タスクを有効にし、最初の実行時刻を決める。
 * @param s        対象
 * @param id       表の行
 * @param first_ms 最初の実行時刻
 */
void sched_start(sched_t *s, size_t id, uint32_t first_ms);

/**
 * @brief 期限の来たタスクを表の順に1回ずつ実行する。
 * @param s 対象
 */
void sched_run(sched_t *s);

#endif /* SERVICE_SCHEDULER_H */
