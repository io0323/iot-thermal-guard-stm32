/**
 * @file link_frame.h
 * @brief UART フレームの型と符号化・復号（iot-common-spec v0.2.1 第7章、詳細設計書 第6章）。
 *
 * 形：`[type u8][seq u16 LE][len u8][payload][crc u16 LE]` を COBS で符号化し、末尾に 0x00。
 * CRC-16/CCITT-FALSE を type から payload 末尾までにかける。
 */
#ifndef SERVICE_LINK_FRAME_H
#define SERVICE_LINK_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cobs.h"

/** フレームの種別。 */
typedef enum {
    FRAME_TELEMETRY = 0x01,
    FRAME_EVENT = 0x02,
    FRAME_HEARTBEAT = 0x03,
    FRAME_STATE = 0x04,
    FRAME_CMD = 0x10,
    FRAME_ACK = 0x11
} frame_type_t;

#define LINK_HEADER_SIZE 4U /**< ヘッダの大きさ（type・seq・len） */
#define LINK_CRC_SIZE 2U /**< CRC の大きさ */
#define LINK_PAYLOAD_MAX 200U /**< ペイロードの最大 */
#define LINK_CMD_HEAD_SIZE 5U /**< CMD の固定部分（cmd_ref・op・ttl_s）の大きさ */
#define LINK_CMD_ARGS_MAX (LINK_PAYLOAD_MAX - LINK_CMD_HEAD_SIZE) /**< CMD の args の最大 */
#define LINK_RAW_MAX (LINK_HEADER_SIZE + LINK_PAYLOAD_MAX + LINK_CRC_SIZE) /**< COBS で符号化する前のフレームの最大 */
/** 符号化後の最大（区切りの 0x00 を含む）。 */
#define LINK_ENCODED_MAX (COBS_MAX_ENCODED(LINK_RAW_MAX) + 1U)

/** 各種別のペイロード長（CMD は最短）。 */
#define LINK_LEN_TELEMETRY 10U
#define LINK_LEN_EVENT 10U /**< EVENT のペイロード長 */
#define LINK_LEN_HEARTBEAT 4U /**< HEARTBEAT のペイロード長 */
#define LINK_LEN_STATE 8U /**< STATE のペイロード長 */
#define LINK_LEN_ACK 7U /**< ACK のペイロード長 */

/** TELEMETRY 0x01 */
typedef struct {
    uint32_t uptime_ms; /**< 起動からのミリ秒 */
    int16_t temp_c100; /**< 温度（0.01℃、故障中は −32768） */
    uint8_t quality; /**< 品質（0 良好、2 故障） */
    uint8_t state; /**< 状態 */
    uint16_t fan_pm; /**< ファンの duty（‰） */
} link_telemetry_t;

/** EVENT 0x02 */
typedef struct {
    uint32_t uptime_ms; /**< 起動からのミリ秒 */
    uint8_t code; /**< EVENT の code */
    uint8_t severity; /**< 重大度 */
    uint8_t from; /**< 遷移の前の状態 */
    uint8_t to; /**< 遷移の後の状態 */
    int16_t temp_c100; /**< その時点の温度（0.01℃） */
} link_event_t;

/** HEARTBEAT 0x03 */
typedef struct {
    uint32_t uptime_ms; /**< 起動からのミリ秒 */
} link_heartbeat_t;

/** STATE 0x04 */
typedef struct {
    uint16_t fw_version; /**< ファームウェアの版 */
    uint32_t boot_id; /**< 起動ごとの識別子 */
    uint16_t period_ms; /**< TELEMETRY の周期（ms） */
} link_state_t;

/** CMD 0x10。args は生のバイト列（解釈は command_handler）。 */
typedef struct {
    uint16_t cmd_ref; /**< 指令の識別子（ACK で返す） */
    uint8_t op; /**< 指令の種類 */
    uint16_t ttl_s; /**< 有効時間（秒） */
    uint8_t args_len; /**< args の長さ */
    uint8_t args[LINK_CMD_ARGS_MAX]; /**< 指令の引数 */
} link_cmd_t;

/** ACK 0x11 */
typedef struct {
    uint16_t cmd_ref; /**< 対応する CMD の cmd_ref */
    uint8_t status; /**< status */
    uint8_t reason; /**< reason */
    uint8_t limited; /**< 安全側の下限に丸めたか */
    uint16_t applied; /**< 実際に適用した値 */
} link_ack_t;

/** 復号したフレーム、または符号化するフレーム。 */
typedef struct {
    uint8_t type; /**< frame_type_t の値 */
    uint16_t seq; /**< 種別ごとの連番 */
    union {
        link_telemetry_t telemetry;
        link_event_t event;
        link_heartbeat_t heartbeat;
        link_state_t state;
        link_cmd_t cmd;
        link_ack_t ack;
    } u; /**< 種別ごとの項目（type で選ぶ） */
} link_frame_t;

/** 受信で捨てた理由（詳細設計書 第6章、open-questions #11・#12）。 */
typedef enum {
    LINK_RX_OK = 0,
    LINK_RX_COBS_ERROR,
    LINK_RX_LENGTH_MISMATCH,
    LINK_RX_CRC_MISMATCH,
    LINK_RX_UNEXPECTED_TYPE,
    LINK_RX_OVERFLOW
} link_rx_error_t;

/**
 * @brief 種別ごとにペイロードを詰める（put_u16le などで1項目ずつ）。
 * @param f   フレーム
 * @param out 出力先
 * @param cap 出力先の大きさ
 * @return ペイロードの長さ。種別が不正か出力先が足りなければ 0（HEARTBEAT などは 0 にならない）
 */
size_t link_payload_pack(const link_frame_t *f, uint8_t *out, size_t cap);

/**
 * @brief ヘッダとペイロードに CRC を付けて COBS で符号化し、末尾に 0x00 を付ける。
 * @param type    種別
 * @param seq     連番
 * @param payload ペイロード
 * @param len     ペイロードの長さ（LINK_PAYLOAD_MAX 以下）
 * @param out     出力先
 * @param cap     出力先の大きさ
 * @return 符号化後の長さ（0x00 を含む）。失敗なら 0
 */
size_t link_encode_raw(uint8_t type, uint16_t seq, const uint8_t *payload, size_t len, uint8_t *out, size_t cap);

/**
 * @brief フレームを符号化する（link_payload_pack と link_encode_raw をまとめたもの）。
 * @param f   フレーム
 * @param out 出力先
 * @param cap 出力先の大きさ
 * @return 符号化後の長さ（0x00 を含む）。失敗なら 0
 */
size_t link_frame_encode(const link_frame_t *f, uint8_t *out, size_t cap);

/**
 * @brief 符号化済みのフレーム（区切りの 0x00 を除く）を復号する。6種別すべてを受け付ける。
 *
 * 検査の順序は COBS → 最短の長さ → CRC → len → 種別 → 種別ごとのペイロード長（open-questions #12）。
 * @param enc 符号化済みのバイト列（0x00 を含まない）
 * @param len 長さ
 * @param out 復号したフレーム
 * @return LINK_RX_OK、または捨てた理由
 */
link_rx_error_t link_frame_decode(const uint8_t *enc, size_t len, link_frame_t *out);

/**
 * @brief 捨てた理由の名前（ログ用）。
 * @param e 理由
 * @return "cobs_error" など
 */
const char *link_rx_error_name(link_rx_error_t e);

/**
 * @brief 種別の名前（ログ用）。
 * @param type 種別
 * @return "TELEMETRY" など。知らない種別は "UNKNOWN"
 */
const char *link_frame_type_name(uint8_t type);

#endif /* SERVICE_LINK_FRAME_H */
