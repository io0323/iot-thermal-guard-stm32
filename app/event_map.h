/**
 * @file event_map.h
 * @brief 遷移（from・to）→ EVENT の code と severity（基本設計書 第8章の表、CR-5 の暫定。1遷移1件）。
 *
 * | 遷移                                              | code                 | severity                 |
 * | NORMAL → WARNING                                  | 3 THRESHOLD_EXCEEDED | warning                  |
 * | NORMAL・WARNING → CRITICAL                        | 3 THRESHOLD_EXCEEDED | critical                 |
 * | WARNING → NORMAL、CRITICAL → WARNING・NORMAL      | 4 THRESHOLD_CLEARED  | info                     |
 * | 任意の状態 → SENSOR_FAULT                         | 5 SENSOR_FAULT       | critical                 |
 * | SENSOR_FAULT → 他の状態                           | 6 SENSOR_RECOVERED   | info                     |
 * | INIT → NORMAL・WARNING・CRITICAL                  | 2 STATE_CHANGED      | 遷移先に従う             |
 */
#ifndef APP_EVENT_MAP_H
#define APP_EVENT_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "safety_fsm.h"

/**
 * @brief 遷移に対応する EVENT を引く。
 * @param from     遷移前
 * @param to       遷移後
 * @param code     EVENT の code
 * @param severity EVENT の severity
 * @return 表にある遷移なら true（同じ状態や表にない遷移は false、code・severity は 0）
 */
bool event_map(fsm_state_t from, fsm_state_t to, uint8_t *code, uint8_t *severity);

#endif /* APP_EVENT_MAP_H */
