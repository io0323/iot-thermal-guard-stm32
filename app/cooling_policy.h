/**
 * @file cooling_policy.h
 * @brief ファンの出力の決定（詳細設計書 第5章）。
 *
 * ```
 * base   = 手動指定が有効 ? 指定値 : auto(t)
 * floor  = INIT 1000 ／ NORMAL 0 ／ WARNING 700 ／ CRITICAL 1000 ／ SENSOR_FAULT 1000
 * duty   = max(base, floor, 強制全開 ? 1000 : 0)          単位は ‰（0–1000）
 * auto(t)= t < 35.00℃ → 0、 35.00〜45.00℃ → (t − 3500) × 400 ÷ 1000、 t ≧ 45.00℃ → 400（整数、切り捨て）
 * ```
 */
#ifndef APP_COOLING_POLICY_H
#define APP_COOLING_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "safety_fsm.h"

#define DUTY_FULL_PM 1000u

/**
 * @brief auto(t)。
 * @param t_c100 温度
 * @return duty（‰）
 */
uint16_t cooling_auto(int16_t t_c100);

/**
 * @brief 状態の下限。
 * @param s 状態
 * @return duty（‰）
 */
uint16_t cooling_floor(fsm_state_t s);

/**
 * @brief ファンの duty を決める。
 * @param s          状態
 * @param t_c100     温度
 * @param manual     手動指定が有効か
 * @param manual_pm  手動指定の値（0–1000）
 * @param force_full ローカルの強制全開（P1 は常に false）
 * @return duty（‰）
 */
uint16_t cooling_duty(fsm_state_t s, int16_t t_c100, bool manual, uint16_t manual_pm, bool force_full);

#endif /* APP_COOLING_POLICY_H */
