/**
 * @file safety_fsm.h
 * @brief 安全監視の状態遷移（詳細設計書 第4章）。
 *
 * fsm_step() は100ms ごとに呼ばれる。時刻は持たず、「1秒継続」は「条件を満たすサンプルが10回連続」として数える。
 * 遷移表は上から順に評価し、最初に一致した行を使う。EVENT は event_map で決める（1遷移1件）。
 *
 * 連続回数のカウンタ（t ≧ warn、t ≧ crit、t < warn−hyst の3つ）は状態に関係なく毎ステップ数え、条件が崩れたときと
 * 温度が故障のとき（ready でないときも含む、open-questions #24）だけ 0 に戻す。状態が変わっても戻さない。
 */
#ifndef APP_SAFETY_FSM_H
#define APP_SAFETY_FSM_H

#include <stdbool.h>
#include <stdint.h>

#include "temp_sensor.h"

#define FSM_HOLD_STEPS 10U        /**< 1秒継続 = 10回連続 */
#define FSM_RECOVER_VALID_RUN 10U /**< SENSOR_FAULT からの復帰に要る正常の連続回数 */

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
    bool init_done; /**< INIT を終える時点（ready になった、または起動から2000ms） */
    bool ack;       /**< ボタンの承認（1回限り。P2 では常に false） */
    int16_t warn_c100, crit_c100, hyst_c100;
} fsm_input_t;

/** fsm_step の出力。 */
typedef struct {
    fsm_state_t from, to;
    bool changed;
    uint8_t event_code;
    uint8_t severity;
    uint8_t row; /**< 当たった行（1〜9。テストと記録用） */
} fsm_output_t;

/** 状態遷移の文脈。 */
typedef struct {
    fsm_state_t state;
    uint8_t warn_run;  /**< t ≧ warn の連続回数 */
    uint8_t crit_run;  /**< t ≧ crit の連続回数 */
    uint8_t clear_run; /**< t < warn − hyst の連続回数 */
} fsm_ctx_t;

/**
 * @brief INIT から始める（カウンタは 0）。
 * @param ctx 文脈
 */
void fsm_init(fsm_ctx_t *ctx);

/**
 * @brief 100ms ごとに1回呼ぶ。カウンタを更新してから遷移表を上から評価する。
 * @param ctx 文脈
 * @param in  入力
 * @param out 遷移と送る EVENT（変化がなければ changed = false、event_code = 0、row = 9）
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
