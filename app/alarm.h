/**
 * @file alarm.h
 * @brief 状態ごとの LED とブザーのパターン（詳細設計書 第5章の表）。task_ui が10ms ごとに出力を読む。
 *
 * | 状態 | LED | ブザー |
 * | --- | --- | --- |
 * | INIT | 赤→黄→緑 を100ms ずつ、以降は3色消灯 | 最初の100ms だけ |
 * | NORMAL | 緑 点灯（強制全開中は 2Hz：250ms 点灯／250ms 消灯、#37） | なし |
 * | WARNING | 黄 1Hz（500ms／500ms） | なし |
 * | CRITICAL | 赤 4Hz（125ms／125ms） | 断続（250ms 鳴動／250ms 停止）、消音できる |
 * | SENSOR_FAULT | 赤 点灯 | 連続、消音できる |
 *
 * - 点滅と断続の位相は、状態に入った時刻を起点にする（INIT は起動した時刻）
 * - 消音は CRITICAL か SENSOR_FAULT に新しく入ったときに解除する
 * - 「鳴っている」は CRITICAL・SENSOR_FAULT で消音していない間（断続の音が止まっている瞬間も含む）
 */
#ifndef APP_ALARM_H
#define APP_ALARM_H

#include <stdbool.h>
#include <stdint.h>

#include "ports.h"
#include "safety_fsm.h"

#define ALARM_LED_COUNT 3U

/** 出力。led は led_t（LED_GREEN・LED_YELLOW・LED_RED）で引く。 */
typedef struct {
    bool led[ALARM_LED_COUNT];
    bool buzzer;
} alarm_out_t;

/** 警報の状態。 */
typedef struct {
    fsm_state_t state;
    uint32_t entered_ms;
    bool silenced;
} alarm_t;

/**
 * @brief INIT に入った状態で初期化する。
 * @param a   対象
 * @param now 起動した時刻（INIT のパターンの起点）
 */
void alarm_init(alarm_t *a, uint32_t now);

/**
 * @brief 状態が変わったことを知らせる。位相の起点を now にし、CRITICAL・SENSOR_FAULT に入ったなら消音を解除する。
 * @param a   対象
 * @param s   新しい状態
 * @param now 時刻
 */
void alarm_set_state(alarm_t *a, fsm_state_t s, uint32_t now);

/**
 * @brief ブザーが「鳴っている」か（CRITICAL・SENSOR_FAULT で消音していない）。
 * @param a 対象
 * @return 鳴っていれば true
 */
bool alarm_sounding(const alarm_t *a);

/**
 * @brief 消音する。
 * @param a 対象
 */
void alarm_silence(alarm_t *a);

/**
 * @brief 時刻 now の出力を決める。
 * @param a          対象
 * @param now        時刻
 * @param force_full 強制全開中か
 * @param out        出力
 */
void alarm_output(const alarm_t *a, uint32_t now, bool force_full, alarm_out_t *out);

#endif /* APP_ALARM_H */
