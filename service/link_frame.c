#include "link_frame.h"

#include "byteorder.h"
#include "cobs.h"
#include "crc16.h"

static size_t pack_telemetry(const link_telemetry_t *t, uint8_t *p)
{
    put_u32le(&p[0], t->uptime_ms);
    put_i16le(&p[4], t->temp_c100);
    p[6] = t->quality;
    p[7] = t->state;
    put_u16le(&p[8], t->fan_pm);
    return LINK_LEN_TELEMETRY;
}

static size_t pack_event(const link_event_t *e, uint8_t *p)
{
    put_u32le(&p[0], e->uptime_ms);
    p[4] = e->code;
    p[5] = e->severity;
    p[6] = e->from;
    p[7] = e->to;
    put_i16le(&p[8], e->temp_c100);
    return LINK_LEN_EVENT;
}

static size_t pack_state(const link_state_t *s, uint8_t *p)
{
    put_u16le(&p[0], s->fw_version);
    put_u32le(&p[2], s->boot_id);
    put_u16le(&p[6], s->period_ms);
    return LINK_LEN_STATE;
}

static size_t pack_ack(const link_ack_t *a, uint8_t *p)
{
    put_u16le(&p[0], a->cmd_ref);
    p[2] = a->status;
    p[3] = a->reason;
    p[4] = a->limited;
    put_u16le(&p[5], a->applied);
    return LINK_LEN_ACK;
}

static size_t pack_cmd(const link_cmd_t *c, uint8_t *p)
{
    put_u16le(&p[0], c->cmd_ref);
    p[2] = c->op;
    put_u16le(&p[3], c->ttl_s);
    for (size_t i = 0; i < c->args_len; i++) {
        p[LINK_CMD_HEAD_SIZE + i] = c->args[i];
    }
    return LINK_CMD_HEAD_SIZE + (size_t)c->args_len;
}

size_t link_payload_pack(const link_frame_t *f, uint8_t *out, size_t cap)
{
    uint8_t tmp[LINK_PAYLOAD_MAX];
    size_t n;
    switch ((frame_type_t)f->type) {
    case FRAME_TELEMETRY:
        n = pack_telemetry(&f->u.telemetry, tmp);
        break;
    case FRAME_EVENT:
        n = pack_event(&f->u.event, tmp);
        break;
    case FRAME_HEARTBEAT:
        put_u32le(&tmp[0], f->u.heartbeat.uptime_ms);
        n = LINK_LEN_HEARTBEAT;
        break;
    case FRAME_STATE:
        n = pack_state(&f->u.state, tmp);
        break;
    case FRAME_CMD:
        if (f->u.cmd.args_len > LINK_CMD_ARGS_MAX) {
            return 0u;
        }
        n = pack_cmd(&f->u.cmd, tmp);
        break;
    case FRAME_ACK:
        n = pack_ack(&f->u.ack, tmp);
        break;
    default:
        return 0u;
    }
    if (n > cap) {
        return 0u;
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = tmp[i];
    }
    return n;
}

size_t link_encode_raw(uint8_t type, uint16_t seq, const uint8_t *payload, size_t len, uint8_t *out, size_t cap)
{
    if (len > LINK_PAYLOAD_MAX) {
        return 0u;
    }
    uint8_t raw[LINK_RAW_MAX];
    raw[0] = type;
    put_u16le(&raw[1], seq);
    raw[3] = (uint8_t)len;
    for (size_t i = 0; i < len; i++) {
        raw[LINK_HEADER_SIZE + i] = payload[i];
    }
    size_t body = LINK_HEADER_SIZE + len;
    put_u16le(&raw[body], crc16_ccitt_false(raw, body));
    if (cap < 1u) {
        return 0u;
    }
    size_t n = cobs_encode(raw, body + LINK_CRC_SIZE, out, cap - 1u);
    if (n == 0u) {
        return 0u;
    }
    out[n] = 0u;
    return n + 1u;
}

size_t link_frame_encode(const link_frame_t *f, uint8_t *out, size_t cap)
{
    uint8_t payload[LINK_PAYLOAD_MAX];
    size_t len = link_payload_pack(f, payload, sizeof payload);
    if (len == 0u) {
        return 0u;
    }
    return link_encode_raw(f->type, f->seq, payload, len, out, cap);
}

/* 種別ごとのペイロード長が合うか。CMD は最短5バイト。知らない種別は false。 */
static bool known_type(uint8_t type, size_t *fixed_len)
{
    switch (type) {
    case FRAME_TELEMETRY:
        *fixed_len = LINK_LEN_TELEMETRY;
        return true;
    case FRAME_EVENT:
        *fixed_len = LINK_LEN_EVENT;
        return true;
    case FRAME_HEARTBEAT:
        *fixed_len = LINK_LEN_HEARTBEAT;
        return true;
    case FRAME_STATE:
        *fixed_len = LINK_LEN_STATE;
        return true;
    case FRAME_CMD:
        *fixed_len = 0u; /* 可変長 */
        return true;
    case FRAME_ACK:
        *fixed_len = LINK_LEN_ACK;
        return true;
    default:
        return false;
    }
}

static void unpack(uint8_t type, const uint8_t *p, size_t len, link_frame_t *out)
{
    switch ((frame_type_t)type) {
    case FRAME_TELEMETRY:
        out->u.telemetry.uptime_ms = get_u32le(&p[0]);
        out->u.telemetry.temp_c100 = get_i16le(&p[4]);
        out->u.telemetry.quality = p[6];
        out->u.telemetry.state = p[7];
        out->u.telemetry.fan_pm = get_u16le(&p[8]);
        break;
    case FRAME_EVENT:
        out->u.event.uptime_ms = get_u32le(&p[0]);
        out->u.event.code = p[4];
        out->u.event.severity = p[5];
        out->u.event.from = p[6];
        out->u.event.to = p[7];
        out->u.event.temp_c100 = get_i16le(&p[8]);
        break;
    case FRAME_HEARTBEAT:
        out->u.heartbeat.uptime_ms = get_u32le(&p[0]);
        break;
    case FRAME_STATE:
        out->u.state.fw_version = get_u16le(&p[0]);
        out->u.state.boot_id = get_u32le(&p[2]);
        out->u.state.period_ms = get_u16le(&p[6]);
        break;
    case FRAME_CMD:
        out->u.cmd.cmd_ref = get_u16le(&p[0]);
        out->u.cmd.op = p[2];
        out->u.cmd.ttl_s = get_u16le(&p[3]);
        out->u.cmd.args_len = (uint8_t)(len - LINK_CMD_HEAD_SIZE);
        for (size_t i = 0; i < (len - LINK_CMD_HEAD_SIZE); i++) {
            out->u.cmd.args[i] = p[LINK_CMD_HEAD_SIZE + i];
        }
        break;
    case FRAME_ACK:
        out->u.ack.cmd_ref = get_u16le(&p[0]);
        out->u.ack.status = p[2];
        out->u.ack.reason = p[3];
        out->u.ack.limited = p[4];
        out->u.ack.applied = get_u16le(&p[5]);
        break;
    default:
        break;
    }
}

link_rx_error_t link_frame_decode(const uint8_t *enc, size_t len, link_frame_t *out)
{
    uint8_t raw[256]; /* フレームバッファ（224）より大きく取り、長すぎるものは length_mismatch にする */
    size_t n = 0u;
    if (!cobs_decode(enc, len, raw, sizeof raw, &n)) {
        return LINK_RX_COBS_ERROR;
    }
    if (n < (LINK_HEADER_SIZE + LINK_CRC_SIZE)) {
        return LINK_RX_LENGTH_MISMATCH;
    }
    size_t body = n - LINK_CRC_SIZE;
    if (crc16_ccitt_false(raw, body) != get_u16le(&raw[body])) {
        return LINK_RX_CRC_MISMATCH;
    }
    size_t payload_len = body - LINK_HEADER_SIZE;
    if ((size_t)raw[3] != payload_len || payload_len > LINK_PAYLOAD_MAX) {
        return LINK_RX_LENGTH_MISMATCH;
    }
    size_t fixed = 0u;
    if (!known_type(raw[0], &fixed)) {
        return LINK_RX_UNEXPECTED_TYPE;
    }
    if (raw[0] == (uint8_t)FRAME_CMD) {
        if (payload_len < LINK_CMD_HEAD_SIZE) {
            return LINK_RX_LENGTH_MISMATCH;
        }
    } else if (payload_len != fixed) {
        return LINK_RX_LENGTH_MISMATCH;
    }
    out->type = raw[0];
    out->seq = get_u16le(&raw[1]);
    unpack(raw[0], &raw[LINK_HEADER_SIZE], payload_len, out);
    return LINK_RX_OK;
}

const char *link_rx_error_name(link_rx_error_t e)
{
    switch (e) {
    case LINK_RX_OK:
        return "ok";
    case LINK_RX_COBS_ERROR:
        return "cobs_error";
    case LINK_RX_LENGTH_MISMATCH:
        return "length_mismatch";
    case LINK_RX_CRC_MISMATCH:
        return "crc_mismatch";
    case LINK_RX_UNEXPECTED_TYPE:
        return "unexpected_type";
    case LINK_RX_OVERFLOW:
        return "rx_overflow";
    default:
        return "unknown";
    }
}

const char *link_frame_type_name(uint8_t type)
{
    switch (type) {
    case FRAME_TELEMETRY:
        return "TELEMETRY";
    case FRAME_EVENT:
        return "EVENT";
    case FRAME_HEARTBEAT:
        return "HEARTBEAT";
    case FRAME_STATE:
        return "STATE";
    case FRAME_CMD:
        return "CMD";
    case FRAME_ACK:
        return "ACK";
    default:
        return "UNKNOWN";
    }
}
