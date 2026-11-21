/**
 * @file safety_fsm.h
 * @brief 安全監視の状態遷移（詳細設計書 第4章）。インターフェースは本版と同じ。P1 は仮版（safety_fsm.c の注記）。
 */
#ifndef APP_SAFETY_FSM_H
#define APP_SAFETY_FSM_H

#include <stdbool.h>
#include <stdint.h>

#include "temp_sensor.h"

/** 状態（UART の state の番号と同じ）。 */
typedef enum { ST_INIT = 0, ST_NORMAL = 1, ST_WARNING = 2, ST_CRITICAL = 3, ST_SENSOR_FAULT = 4 } fsm_state_t;

/** EVENT の code（iot-common-spec v0.2.1 第7章）。 */
typedef enum {
    EV_NONE = 0,
    EV_BOOT = 1,
    EV_STATE_CHANGED = 2,
    EV_THRESHOLD_EXCEEDED = 3,
    EV_THRESHOLD_CLEARED = 4,
    EV_SENSOR_FAULT = 5,
    EV_SENSOR_RECOVERED = 6,
    EV_UPSTREAM_LOST = 7,
    EV_UPSTREAM_RESTORED = 8
} event_code_t;

/** EVENT の severity。 */
typedef enum { SEV_INFO = 0, SEV_WARNING = 1, SEV_CRITICAL = 2 } severity_t;

/** fsm_step の入力。 */
typedef struct {
    temp_reading_t temp;
    bool init_done;
    bool ack;
    int16_t warn_c100, crit_c100, hyst_c100;
} fsm_input_t;

/** fsm_step の出力。 */
typedef struct {
    fsm_state_t from, to;
    bool changed;
    uint8_t event_code;
    uint8_t severity;
} fsm_output_t;

/** 状態遷移の文脈。 */
typedef struct {
    fsm_state_t state;
} fsm_ctx_t;

/**
 * @brief INIT から始める。
 * @param ctx 文脈
 */
void fsm_init(fsm_ctx_t *ctx);

/**
 * @brief 100ms ごとに1回呼ぶ。遷移表を上から評価し、最初に一致した行を使う。
 * @param ctx 文脈
 * @param in  入力
 * @param out 遷移と送る EVENT（変化がなければ changed = false、event_code = 0）
 */
void fsm_step(fsm_ctx_t *ctx, const fsm_input_t *in, fsm_output_t *out);

/**
 * @brief 分類(t)：t ≧ crit なら CRITICAL、t ≧ warn なら WARNING、それ以外は NORMAL。
 * @param t_c100    温度
 * @param warn_c100 warn
 * @param crit_c100 crit
 * @return 状態
 */
fsm_state_t fsm_classify(int16_t t_c100, int16_t warn_c100, int16_t crit_c100);

/**
 * @brief 状態の名前（ログ用）。
 * @param s 状態
 * @return "NORMAL" など
 */
const char *fsm_state_name(fsm_state_t s);

#endif /* APP_SAFETY_FSM_H */
