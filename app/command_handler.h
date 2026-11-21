/**
 * @file command_handler.h
 * @brief 指令処理（詳細設計書 第6章、open-questions #13・#14）。
 *
 * 処理の順序：① INIT 中なら捨てる（ACK なし） → ② 重複キャッシュに cmd_ref があれば、前回と同じ ACK を返して終わる
 * （GET_STATUS なら STATE を送り直してから） → ③ 検証と実行 → ④ 結果をキャッシュに入れて ACK を送る。
 * rejected の結果もキャッシュする。ACK を送信キューに入れられなくても、キャッシュには実際の実行結果を残す。
 */
#ifndef APP_COMMAND_HANDLER_H
#define APP_COMMAND_HANDLER_H

#include <stdbool.h>
#include <stdint.h>

#include "link_frame.h"
#include "safety_fsm.h"

#define CMD_CACHE_SIZE 16U /**< 重複検出のキャッシュの件数（cmd_ref ごとに前回の ACK を持つ） */
#define CMD_TTL_MIN_S 1U /**< ttl_s の最小（CR-4） */
#define CMD_TTL_MAX_S 600U /**< ttl_s の最大（CR-4） */
#define CMD_WARN_MIN_C100 (-2000) /**< SET_THRESHOLD の warn の下限（−20.00℃） */
#define CMD_CRIT_MAX_C100 8000 /**< SET_THRESHOLD の crit の上限（80.00℃） */
#define CMD_HYST_MIN_C100 50 /**< SET_THRESHOLD の hyst の下限（0.50℃） */
#define CMD_HYST_MAX_C100 1000 /**< SET_THRESHOLD の hyst の上限（10.00℃） */

/** op（iot-common-spec v0.2.1 第7章）。値は uint8_t の op と比べて使う（型名は付けない、MISRA 2.3）。 */
enum {
    OP_SET_FAN_OVERRIDE = 0x01,
    OP_CLEAR_OVERRIDE = 0x02,
    OP_SET_THRESHOLD = 0x03,
    OP_GET_STATUS = 0x10,
    OP_GET_DIAG = 0x11
};

/** ACK の status。作品01は accepted を使わない。値は uint8_t の status に入れて使う（型名は付けない、MISRA 2.3）。 */
enum { ACK_ACCEPTED = 0, ACK_DONE = 1, ACK_REJECTED = 2, ACK_FAILED = 3 };

/** ACK の reason。作品01は expired を返さない（CR-4）。 */
typedef enum {
    REASON_NONE = 0,
    REASON_UNSAFE = 1,
    REASON_INVALID_ARG = 2,
    REASON_UNKNOWN_OP = 3,
    REASON_EXPIRED = 4,
    REASON_INTERNAL = 5
} ack_reason_t;

/** 重複キャッシュの1件。 */
typedef struct {
    bool valid; /**< この枠を使っているか */
    uint16_t cmd_ref; /**< CMD の cmd_ref */
    uint8_t op; /**< CMD の op */
    uint8_t status; /**< ACK の status */
    uint8_t reason; /**< ACK の reason */
    uint8_t limited; /**< ACK の limited */
    uint16_t applied; /**< ACK の applied */
} cmd_cache_entry_t;

/** 指令処理の状態。 */
typedef struct {
    cmd_cache_entry_t cache[CMD_CACHE_SIZE]; /**< 重複検出のキャッシュ */
    uint8_t cache_next; /**< 次に上書きする位置（古いものから） */
    bool override_active; /**< 手動指定（SET_FAN_OVERRIDE）が有効か */
    uint16_t override_pm; /**< 手動指定の duty（‰） */
    uint32_t override_expires_ms; /**< 手動指定のリースの期限（uptime_ms） */
    int16_t warn_c100; /**< warn しきい値（0.01℃） */
    int16_t crit_c100; /**< crit しきい値（0.01℃） */
    int16_t hyst_c100; /**< ヒステリシス（0.01℃） */
    uint16_t cmd_dropped_init; /**< INIT 中に捨てた CMD の件数（飽和） */
    uint16_t cmd_duplicates; /**< 重複として前回の ACK を返した件数（飽和） */
} cmd_handler_t;

/** 指令を処理するときの周りの状態。 */
typedef struct {
    fsm_state_t state; /**< いまの状態 */
    int16_t temp_c100; /**< いまの温度（0.01℃、故障中は −32768） */
    bool force_full; /**< ローカルの強制全開中か */
} cmd_env_t;

/** 指令の処理結果。呼び出し側は send_state → ACK の順に送る。 */
typedef struct {
    bool has_ack;          /**< false なら何も送らない（INIT 中に捨てた） */
    bool send_state;       /**< ACK の前に STATE を送る（GET_STATUS） */
    bool output_changed;   /**< 手動指定が変わったので出力を再計算する */
    link_ack_t ack; /**< 返す ACK（has_ack のときだけ有効） */
} cmd_result_t;

/**
 * @brief 初期化する（キャッシュを空にし、しきい値を既定値にする）。
 * @param h 対象
 */
void cmd_init(cmd_handler_t *h);

/**
 * @brief 受けた CMD を処理する。
 * @param h      対象
 * @param cmd    CMD
 * @param now_ms 受信時刻（リースの起点）
 * @param env    周りの状態
 * @param out    結果
 */
void cmd_handle(cmd_handler_t *h, const link_cmd_t *cmd, uint32_t now_ms, const cmd_env_t *env, cmd_result_t *out);

/**
 * @brief リースの期限を判定する（task_link が10ms ごとに呼ぶ）。`now - expires >= 0` で切れる。
 * @param h      対象
 * @param now_ms 現在時刻
 * @return 切れて手動指定を解除したら true
 */
bool cmd_lease_tick(cmd_handler_t *h, uint32_t now_ms);

/**
 * @brief 手動指定を解除する（上位断）。
 * @param h 対象
 * @return 手動指定があったら true
 */
bool cmd_clear_override(cmd_handler_t *h);

#endif /* APP_COMMAND_HANDLER_H */
