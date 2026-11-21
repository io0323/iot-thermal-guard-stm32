#include "command_handler.h"

#include "byteorder.h"
#include "cooling_policy.h"
#include "sat.h"
#include "thresholds.h"

void cmd_init(cmd_handler_t *h)
{
    for (uint8_t i = 0; i < CMD_CACHE_SIZE; i++) {
        h->cache[i].valid = false;
    }
    h->cache_next = 0u;
    h->override_active = false;
    h->override_pm = 0u;
    h->override_expires_ms = 0u;
    h->warn_c100 = (int16_t)THRESHOLD_WARN_C100;
    h->crit_c100 = (int16_t)THRESHOLD_CRIT_C100;
    h->hyst_c100 = (int16_t)THRESHOLD_HYST_C100;
    h->cmd_dropped_init = 0u;
    h->cmd_duplicates = 0u;
}

static const cmd_cache_entry_t *cache_find(const cmd_handler_t *h, uint16_t cmd_ref)
{
    for (uint8_t i = 0; i < CMD_CACHE_SIZE; i++) {
        if (h->cache[i].valid && h->cache[i].cmd_ref == cmd_ref) {
            return &h->cache[i];
        }
    }
    return NULL;
}

static void cache_put(cmd_handler_t *h, uint8_t op, const link_ack_t *a)
{
    cmd_cache_entry_t *e = &h->cache[h->cache_next];
    e->valid = true;
    e->cmd_ref = a->cmd_ref;
    e->op = op;
    e->status = a->status;
    e->reason = a->reason;
    e->limited = a->limited;
    e->applied = a->applied;
    h->cache_next = (uint8_t)((h->cache_next + 1u) % CMD_CACHE_SIZE);
}

static void reject(link_ack_t *a, ack_reason_t reason)
{
    a->status = (uint8_t)ACK_REJECTED;
    a->reason = (uint8_t)reason;
    a->limited = 0u;
    a->applied = 0u;
}

static void set_fan_override(cmd_handler_t *h, const link_cmd_t *c, uint32_t now, const cmd_env_t *env, cmd_result_t *out)
{
    if (c->args_len != 2u) {
        reject(&out->ack, REASON_INVALID_ARG);
        return;
    }
    uint16_t duty = get_u16le(&c->args[0]);
    if (duty > DUTY_FULL_PM || c->ttl_s < CMD_TTL_MIN_S || c->ttl_s > CMD_TTL_MAX_S) {
        reject(&out->ack, REASON_INVALID_ARG);
        return;
    }
    h->override_active = true;
    h->override_pm = duty;
    h->override_expires_ms = now + ((uint32_t)c->ttl_s * 1000u);
    out->output_changed = true;
    uint16_t applied = cooling_duty(env->state, env->temp_c100, true, duty, env->force_full);
    out->ack.applied = applied;
    out->ack.limited = (duty < applied) ? 1u : 0u;
}

static bool threshold_valid(int16_t warn, int16_t crit, int16_t hyst)
{
    if (warn < CMD_WARN_MIN_C100 || warn >= crit || crit > CMD_CRIT_MAX_C100) {
        return false;
    }
    if (hyst < CMD_HYST_MIN_C100 || hyst > CMD_HYST_MAX_C100) {
        return false;
    }
    return ((int32_t)crit - (int32_t)warn) >= (int32_t)hyst;
}

static void set_threshold(cmd_handler_t *h, const link_cmd_t *c, const cmd_env_t *env, cmd_result_t *out)
{
    if (c->args_len != 6u) {
        reject(&out->ack, REASON_INVALID_ARG);
        return;
    }
    if (env->state == ST_CRITICAL || env->state == ST_SENSOR_FAULT) {
        reject(&out->ack, REASON_UNSAFE);
        return;
    }
    int16_t warn = get_i16le(&c->args[0]);
    int16_t crit = get_i16le(&c->args[2]);
    int16_t hyst = get_i16le(&c->args[4]);
    if (!threshold_valid(warn, crit, hyst)) {
        reject(&out->ack, REASON_INVALID_ARG);
        return;
    }
    h->warn_c100 = warn;
    h->crit_c100 = crit;
    h->hyst_c100 = hyst;
}

/* 引数なしの op（CLEAR_OVERRIDE・GET_STATUS・GET_DIAG） */
static void no_arg_op(cmd_handler_t *h, const link_cmd_t *c, cmd_result_t *out)
{
    if (c->args_len != 0u) {
        reject(&out->ack, REASON_INVALID_ARG);
        return;
    }
    if (c->op == (uint8_t)OP_CLEAR_OVERRIDE) {
        out->output_changed = cmd_clear_override(h);
    } else if (c->op == (uint8_t)OP_GET_STATUS) {
        out->send_state = true;
    } else {
        /* GET_DIAG：ACK(done) だけ（CR-1 待ち） */
    }
}

static void execute(cmd_handler_t *h, const link_cmd_t *c, uint32_t now, const cmd_env_t *env, cmd_result_t *out)
{
    switch (c->op) {
    case OP_SET_FAN_OVERRIDE:
        set_fan_override(h, c, now, env, out);
        break;
    case OP_SET_THRESHOLD:
        set_threshold(h, c, env, out);
        break;
    case OP_CLEAR_OVERRIDE:
    case OP_GET_STATUS:
    case OP_GET_DIAG:
        no_arg_op(h, c, out);
        break;
    default:
        reject(&out->ack, REASON_UNKNOWN_OP);
        break;
    }
}

void cmd_handle(cmd_handler_t *h, const link_cmd_t *cmd, uint32_t now_ms, const cmd_env_t *env, cmd_result_t *out)
{
    out->has_ack = false;
    out->send_state = false;
    out->output_changed = false;
    out->ack = (link_ack_t){.cmd_ref = cmd->cmd_ref, .status = (uint8_t)ACK_DONE, .reason = (uint8_t)REASON_NONE};

    /* ① INIT 中は捨てる（ACK なし） */
    if (env->state == ST_INIT) {
        sat_inc_u16(&h->cmd_dropped_init);
        return;
    }
    out->has_ack = true;
    /* ② 重複：前回と同じ ACK。GET_STATUS は STATE を送り直す（読み出しで副作用がない、#13） */
    const cmd_cache_entry_t *hit = cache_find(h, cmd->cmd_ref);
    if (hit != NULL) {
        sat_inc_u16(&h->cmd_duplicates);
        out->ack.status = hit->status;
        out->ack.reason = hit->reason;
        out->ack.limited = hit->limited;
        out->ack.applied = hit->applied;
        out->send_state = (hit->op == (uint8_t)OP_GET_STATUS && hit->status == (uint8_t)ACK_DONE);
        return;
    }
    /* ③ 検証と実行 → ④ キャッシュ */
    execute(h, cmd, now_ms, env, out);
    cache_put(h, cmd->op, &out->ack);
}

bool cmd_lease_tick(cmd_handler_t *h, uint32_t now_ms)
{
    if (h->override_active && (int32_t)(now_ms - h->override_expires_ms) >= 0) {
        h->override_active = false;
        return true;
    }
    return false;
}

bool cmd_clear_override(cmd_handler_t *h)
{
    bool was = h->override_active;
    h->override_active = false;
    return was;
}
