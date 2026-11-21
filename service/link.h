/**
 * @file link.h
 * @brief UART リンク：受信の組み立て、送信キュー、seq の割り当て、破棄理由別の件数（詳細設計書 第6章）。
 */
#ifndef SERVICE_LINK_H
#define SERVICE_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_frame.h"

#define LINK_RX_BUF_SIZE 224u  /**< 0x00 までをためるフレームバッファ */
#define LINK_TXQ_SLOTS 16u     /**< 送信キューの枠数 */
#define LINK_TX_PAYLOAD_MAX 16u /**< 枠に入れるペイロードの最大（作品01が送るのは最大10バイト） */

/** 種別ごとの seq カウンタの添字（作品01が送る種別だけ）。 */
typedef enum { LINK_SEQ_TELEMETRY, LINK_SEQ_EVENT, LINK_SEQ_HEARTBEAT, LINK_SEQ_STATE, LINK_SEQ_ACK, LINK_SEQ_COUNT } link_seq_index_t;

/** 送信キューの枠。符号化前の種別とペイロード（seq は送り出すときに割り当てる）。 */
typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t payload[LINK_TX_PAYLOAD_MAX];
} link_tx_slot_t;

/** 診断値（飽和する）。 */
typedef struct {
    uint16_t rx_cobs_errors;
    uint16_t rx_len_errors;
    uint16_t rx_crc_errors;
    uint16_t rx_unexpected;
    uint16_t rx_overflow;
    uint16_t tx_dropped;
} link_counters_t;

/** リンクの状態。 */
typedef struct {
    uint8_t rx_buf[LINK_RX_BUF_SIZE];
    size_t rx_len;
    size_t rx_last_len; /**< 直前に区切ったフレームの長さ（ログ用） */
    bool rx_dropping; /**< あふれたので次の 0x00 まで捨てている */
    link_tx_slot_t txq[LINK_TXQ_SLOTS];
    uint8_t tx_head;
    uint8_t tx_count;
    uint16_t seq[LINK_SEQ_COUNT];
    link_counters_t cnt;
} link_t;

/** link_rx_byte の結果。 */
typedef enum { LINK_RX_NONE, LINK_RX_FRAME, LINK_RX_DISCARDED } link_rx_status_t;

/**
 * @brief 初期化する（キューを空にし、seq を 0 にする）。
 * @param l 対象
 */
void link_init(link_t *l);

/**
 * @brief 受信した1バイトを渡す。0x00 でフレームを区切って復号する。
 *
 * 作品01が受けるのは HEARTBEAT と CMD だけで、それ以外の種別は unexpected_type として捨てる。
 * フレームバッファ（224バイト）があふれたら rx_overflow を数え、次の 0x00 まで捨てる。
 * @param l   対象
 * @param b   受信したバイト
 * @param out LINK_RX_FRAME のとき、復号したフレーム
 * @param err LINK_RX_DISCARDED のとき、捨てた理由
 * @return LINK_RX_NONE（途中）、LINK_RX_FRAME、LINK_RX_DISCARDED
 */
link_rx_status_t link_rx_byte(link_t *l, uint8_t b, link_frame_t *out, link_rx_error_t *err);

/**
 * @brief 直前に区切ったフレームの生のバイト列（区切りの 0x00 を除く。ログ用）。
 * @param l   対象
 * @param len 長さ
 * @return バイト列。次の link_rx_byte までだけ有効
 */
const uint8_t *link_rx_last_raw(const link_t *l, size_t *len);

/**
 * @brief 送信キューに入れる。seq は無視し、送り出すときに割り当てる。
 *
 * 満杯のとき：TELEMETRY・HEARTBEAT は捨てる。STATE・EVENT・ACK はキュー内で最も古い TELEMETRY または
 * HEARTBEAT を押し出して入る。押し出せるものがなければ捨てる。捨てた件数（押し出した分も）は tx_dropped。
 * @param l 対象
 * @param f フレーム（作品01が送る種別だけ）
 * @return キューに入れたら true
 */
bool link_send(link_t *l, const link_frame_t *f);

/**
 * @brief キューの先頭を、その種別の次の seq で符号化する（キューからはまだ出さない）。
 * @param l     対象
 * @param out   出力先
 * @param cap   出力先の大きさ
 * @param frame NULL でなければ、符号化したフレームの内容（ログ用）
 * @return 符号化後の長さ（0x00 を含む）。キューが空なら 0
 */
size_t link_tx_peek(const link_t *l, uint8_t *out, size_t cap, link_frame_t *frame);

/**
 * @brief link_tx_peek で符号化した先頭を送り出したものとしてキューから出し、その種別の seq を進める。
 * @param l 対象
 */
void link_tx_commit(link_t *l);

/**
 * @brief キューに入っている件数。
 * @param l 対象
 * @return 件数
 */
uint8_t link_tx_pending(const link_t *l);

#endif /* SERVICE_LINK_H */
