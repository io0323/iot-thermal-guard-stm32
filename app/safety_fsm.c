/*
 * 【仮版（P1）】safety_fsm
 *
 * 遷移表（詳細設計書 第4章）のうち、行2「INIT・準備完了 → 分類(t)、EVENT 2 STATE_CHANGED（severity は遷移先に従う）」
 * と、行9「どれにも当たらない → 変わらない」だけを実装する。行1・3〜8（センサー故障、しきい値の超過・解除、
 * 承認による復帰）と10回連続の判定は P2 の本版で実装する。
 */
#include "safety_fsm.h"

void fsm_init(fsm_ctx_t *ctx)
{
    ctx->state = ST_INIT;
}

fsm_state_t fsm_classify(int16_t t_c100, int16_t warn_c100, int16_t crit_c100)
{
    if (t_c100 >= crit_c100) {
        return ST_CRITICAL;
    }
    if (t_c100 >= warn_c100) {
        return ST_WARNING;
    }
    return ST_NORMAL;
}

static uint8_t severity_of(fsm_state_t s)
{
    switch (s) {
    case ST_WARNING:
        return (uint8_t)SEV_WARNING;
    case ST_CRITICAL:
    case ST_SENSOR_FAULT:
        return (uint8_t)SEV_CRITICAL;
    case ST_INIT:
    case ST_NORMAL:
    default:
        return (uint8_t)SEV_INFO;
    }
}

void fsm_step(fsm_ctx_t *ctx, const fsm_input_t *in, fsm_output_t *out)
{
    out->from = ctx->state;
    out->to = ctx->state;
    out->changed = false;
    out->event_code = (uint8_t)EV_NONE;
    out->severity = (uint8_t)SEV_INFO;

    /* 行2：INIT・準備完了 → 分類(t) */
    if (ctx->state == ST_INIT && in->init_done) {
        fsm_state_t to = fsm_classify(in->temp.temp_c100, in->warn_c100, in->crit_c100);
        out->to = to;
        out->changed = true;
        out->event_code = (uint8_t)EV_STATE_CHANGED;
        out->severity = severity_of(to);
        ctx->state = to;
    }
    /* 行9：上のどれにも当たらない → 変わらない（行1・3〜8 は P2） */
}

const char *fsm_state_name(fsm_state_t s)
{
    switch (s) {
    case ST_INIT:
        return "INIT";
    case ST_NORMAL:
        return "NORMAL";
    case ST_WARNING:
        return "WARNING";
    case ST_CRITICAL:
        return "CRITICAL";
    case ST_SENSOR_FAULT:
        return "SENSOR_FAULT";
    default:
        return "UNKNOWN";
    }
}
