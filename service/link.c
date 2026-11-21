#include "link.h"

#include <stddef.h>
#include <stdint.h>

#include "link_frame.h"
#include "sat.h"

void link_init(link_t *l)
{
    l->rx_len = 0U;
    l->rx_last_len = 0U;
    l->rx_dropping = false;
    l->tx_head = 0U;
    l->tx_count = 0U;
    for (size_t i = 0; i < (size_t)LINK_SEQ_COUNT; i++) {
        l->seq[i] = 0U;
    }
    l->cnt = (link_counters_t){0};
}

static void count_error(link_counters_t *c, link_rx_error_t e)
{
    switch (e) {
    case LINK_RX_COBS_ERROR:
        sat_inc_u16(&c->rx_cobs_errors);
        break;
    case LINK_RX_LENGTH_MISMATCH:
        sat_inc_u16(&c->rx_len_errors);
        break;
    case LINK_RX_CRC_MISMATCH:
        sat_inc_u16(&c->rx_crc_errors);
        break;
    case LINK_RX_UNEXPECTED_TYPE:
        sat_inc_u16(&c->rx_unexpected);
        break;
    case LINK_RX_OVERFLOW:
        sat_inc_u16(&c->rx_overflow);
        break;
    case LINK_RX_OK:
    default:
        break;
    }
}

link_rx_status_t link_rx_byte(link_t *l, uint8_t b, link_frame_t *out, link_rx_error_t *err)
{
    if (b != 0U) {
        if (l->rx_dropping) {
            return LINK_RX_NONE;
        }
        if (l->rx_len >= LINK_RX_BUF_SIZE) {
            l->rx_dropping = true;
            l->rx_len = 0U;
            count_error(&l->cnt, LINK_RX_OVERFLOW);
            *err = LINK_RX_OVERFLOW;
            return LINK_RX_DISCARDED;
        }
        l->rx_buf[l->rx_len] = b;
        l->rx_len++;
        return LINK_RX_NONE;
    }
    /* 0x00：フレームの区切り */
    if (l->rx_dropping) {
        l->rx_dropping = false;
        l->rx_len = 0U;
        return LINK_RX_NONE;
    }
    size_t len = l->rx_len;
    l->rx_len = 0U;
    link_rx_error_t e = link_frame_decode(l->rx_buf, len, out);
    if (e == LINK_RX_OK && out->type != (uint8_t)FRAME_HEARTBEAT && out->type != (uint8_t)FRAME_CMD) {
        e = LINK_RX_UNEXPECTED_TYPE;
    }
    /* link_rx_last_raw のために長さを残す（次のバイトで上書きされる） */
    l->rx_last_len = len;
    if (e != LINK_RX_OK) {
        count_error(&l->cnt, e);
        *err = e;
        return LINK_RX_DISCARDED;
    }
    return LINK_RX_FRAME;
}

const uint8_t *link_rx_last_raw(const link_t *l, size_t *len)
{
    *len = l->rx_last_len;
    return l->rx_buf;
}

static int seq_index(uint8_t type)
{
    switch (type) {
    case FRAME_TELEMETRY:
        return LINK_SEQ_TELEMETRY;
    case FRAME_EVENT:
        return LINK_SEQ_EVENT;
    case FRAME_HEARTBEAT:
        return LINK_SEQ_HEARTBEAT;
    case FRAME_STATE:
        return LINK_SEQ_STATE;
    case FRAME_ACK:
        return LINK_SEQ_ACK;
    default:
        return -1;
    }
}

static bool is_periodic(uint8_t type)
{
    return type == (uint8_t)FRAME_TELEMETRY || type == (uint8_t)FRAME_HEARTBEAT;
}

static uint8_t slot_at(const link_t *l, uint8_t k)
{
    return (uint8_t)((l->tx_head + k) % LINK_TXQ_SLOTS);
}

/* キューの k 番目（先頭から）を取り除き、後ろを詰める。 */
static void remove_at(link_t *l, uint8_t k)
{
    for (uint8_t i = k; (uint8_t)(i + 1U) < l->tx_count; i++) {
        l->txq[slot_at(l, i)] = l->txq[slot_at(l, (uint8_t)(i + 1U))];
    }
    l->tx_count--;
}

bool link_send(link_t *l, const link_frame_t *f)
{
    if (seq_index(f->type) < 0) {
        sat_inc_u16(&l->cnt.tx_dropped);
        return false;
    }
    link_tx_slot_t slot;
    size_t n = link_payload_pack(f, slot.payload, sizeof slot.payload);
    if (n == 0U) {
        sat_inc_u16(&l->cnt.tx_dropped);
        return false;
    }
    slot.type = f->type;
    slot.len = (uint8_t)n;
    if (l->tx_count >= LINK_TXQ_SLOTS) {
        if (is_periodic(f->type)) {
            sat_inc_u16(&l->cnt.tx_dropped);
            return false;
        }
        uint8_t k = 0U;
        while (k < l->tx_count && !is_periodic(l->txq[slot_at(l, k)].type)) {
            k++;
        }
        sat_inc_u16(&l->cnt.tx_dropped); /* 押し出した1件、または入れられなかった1件 */
        if (k >= l->tx_count) {
            return false;
        }
        remove_at(l, k);
    }
    l->txq[slot_at(l, l->tx_count)] = slot;
    l->tx_count++;
    return true;
}

size_t link_tx_peek(const link_t *l, uint8_t *out, size_t cap, link_frame_t *frame)
{
    if (l->tx_count == 0U) {
        return 0U;
    }
    const link_tx_slot_t *s = &l->txq[l->tx_head];
    int idx = seq_index(s->type);
    uint16_t seq = (idx >= 0) ? l->seq[idx] : 0U;
    size_t n = link_encode_raw(s->type, seq, s->payload, s->len, out, cap);
    if (n != 0U && frame != NULL) {
        (void)link_frame_decode(out, n - 1U, frame); /* 送るバイト列そのものから戻す */
    }
    return n;
}

void link_tx_commit(link_t *l)
{
    if (l->tx_count == 0U) {
        return;
    }
    int idx = seq_index(l->txq[l->tx_head].type);
    if (idx >= 0) {
        l->seq[idx] = (uint16_t)(l->seq[idx] + 1U); /* 16ビットで一周してよい */
    }
    l->tx_head = (uint8_t)((l->tx_head + 1U) % LINK_TXQ_SLOTS);
    l->tx_count--;
}

uint8_t link_tx_pending(const link_t *l)
{
    return l->tx_count;
}
