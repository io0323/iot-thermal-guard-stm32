/**
 * @file tick.h
 * @brief 時刻の比較（詳細設計書 第2章）。uptime_ms は u32 で約49.7日で一周する。
 *
 * 時刻の比較はこの関数だけを使い、符号付きへのキャストを各所に書かない（MISRA 10.8 の逸脱を作らないため）。
 */
#ifndef LIB_TICK_H
#define LIB_TICK_H

#include <stdbool.h>
#include <stdint.h>

#define TICK_HALF_RANGE 0x80000000UL /**< これ未満の差を「期限を過ぎた（またはちょうど）」と読む */

/**
 * @brief 時刻 now が期限 deadline に達したか（一周をまたいでもよい）。
 *
 * 符号なしの差 (now − deadline) が 0x80000000 未満なら真。差が半周（約24.8日）以上離れた時刻は比べられない。
 * @param now      現在時刻（ms）
 * @param deadline 期限（ms）
 * @return now が deadline と同じか、それより後なら true
 */
/* 取り違えの検出：now と deadline の取り違えは test_tick の test_argument_order・test_boundaries_of_unsigned_difference で落ちる（open-questions #41） */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static inline bool tick_reached(uint32_t now, uint32_t deadline)
{
    return (now - deadline) < TICK_HALF_RANGE; /* u32 どうしの差なので一周して u32 に収まる */
}

#endif /* LIB_TICK_H */
