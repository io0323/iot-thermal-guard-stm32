/**
 * @file host_log.h
 * @brief ホスト版のログ（JSON Lines、詳細設計書 第8章、open-questions #15）。キーの一覧は README。
 */
#ifndef BSP_HOST_LOG_H
#define BSP_HOST_LOG_H

#include <stdbool.h>
#include <stdio.h>

#include "firmware.h"

/**
 * @brief ログの出力先を開く。
 * @param path   ファイル（NULL なら標準エラー）
 * @param append 追記するか（疑似ウォッチドッグで起動し直したとき。open-questions #38）
 * @return 開けたら true
 */
bool host_log_open(const char *path, bool append);

/**
 * @brief firmware に渡す観測フック。
 * @return フック
 */
const fw_observer_t *host_log_observer(void);

/**
 * @brief 診断値を1行書く。
 * @param t_ms 時刻
 * @param d    診断値
 */
void host_log_diag(uint32_t t_ms, const fw_diag_t *d);

/**
 * @brief 閉じる。
 */
void host_log_close(void);

#endif /* BSP_HOST_LOG_H */
