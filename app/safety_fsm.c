/*
 * 状態遷移（詳細設計書 第4章の遷移表）。{現状態, 条件関数, 次状態} の静的な表を上から評価する。
 *
 * | # | 現状態                  | 条件                                         | 次の状態                                  |
 * | 1 | INIT                    | 準備完了、かつ温度が故障（2秒で ready でない）| SENSOR_FAULT                              |
 * | 2 | INIT                    | 準備完了                                      | 分類(t)                                   |
 * | 3 | NORMAL・WARNING・CRITICAL | 温度が故障                                  | SENSOR_FAULT                              |
 * | 4 | SENSOR_FAULT            | 承認、かつ valid_run ≧ 10                     | 分類(t)                                   |
 * | 5 | NORMAL・WARNING         | t ≧ crit が10回連続                           | CRITICAL                                  |
 * | 6 | NORMAL                  | t ≧ warn が10回連続                           | WARNING                                   |
 * | 7 | WARNING                 | t < warn−hyst が10回連続                      | NORMAL                                    |
 * | 8 | CRITICAL                | 承認、かつ t < crit−hyst                      | t ≧ warn−hyst なら WARNING、それ以外は NORMAL |
 * | 9 | どれでも                | 上のどれにも当たらない                        | 変わらない                                |
 */
#include "safety_fsm.h"

#include <stdint.h>

#include "event_map.h"
#include "temp_sensor.h"

#define BIT(s) (1U << (unsigned)(s))

typedef bool (*cond_fn_t)(const fsm_ctx_t *ctx, const fsm_input_t *in);
typedef fsm_state_t (*next_fn_t)(const fsm_input_t *in);

typedef struct {
    uint8_t row;
    uint32_t states; /**< 現状態のビット集合 */
    cond_fn_t cond;
    next_fn_t next;
} fsm_row_t;

/* ready で故障がないときだけ温度を使える（ready でないのは故障と同じ扱い、#23・#24） */
static bool temp_bad(const fsm_input_t *in)
{
    return (!in->temp.ready) || (in->temp.fault != TEMP_FAULT_NONE);
}

static int32_t t_of(const fsm_input_t *in)
{
    return (int32_t)in->temp.temp_c100;
}

static bool c_init_fault(const fsm_ctx_t *ctx, const fsm_input_t *in)
{
    (void)ctx;
    return in->init_done && temp_bad(in);
}

static bool c_init_done(const fsm_ctx_t *ctx, const fsm_input_t *in)
{
    (void)ctx;
    return in->init_done;
}

static bool c_temp_fault(const fsm_ctx_t *ctx, const fsm_input_t *in)
{
    (void)ctx;
    return temp_bad(in);
}

static bool c_recover(const fsm_ctx_t *ctx, const fsm_input_t *in)
{
    (void)ctx;
    return in->ack && (!temp_bad(in)) && (in->temp.valid_run >= FSM_RECOVER_VALID_RUN);
}

static bool c_crit_hold(const fsm_ctx_t *ctx, const fsm_input_t *in)
{
    (void)in;
    return ctx->crit_run >= FSM_HOLD_STEPS;
}

static bool c_warn_hold(const fsm_ctx_t *ctx, const fsm_input_t *in)
{
    (void)in;
    return ctx->warn_run >= FSM_HOLD_STEPS;
}

static bool c_clear_hold(const fsm_ctx_t *ctx, const fsm_input_t *in)
{
    (void)in;
    return ctx->clear_run >= FSM_HOLD_STEPS;
}

static bool c_crit_ack(const fsm_ctx_t *ctx, const fsm_input_t *in)
{
    (void)ctx;
    return in->ack && !temp_bad(in) && t_of(in) < ((int32_t)in->crit_c100 - (int32_t)in->hyst_c100);
}

static fsm_state_t n_fault(const fsm_input_t *in)
{
    (void)in;
    return ST_SENSOR_FAULT;
}

static fsm_state_t n_classify(const fsm_input_t *in)
{
    return fsm_classify(in->temp.temp_c100, in->warn_c100, in->crit_c100);
}

static fsm_state_t n_critical(const fsm_input_t *in)
{
    (void)in;
    return ST_CRITICAL;
}

static fsm_state_t n_warning(const fsm_input_t *in)
{
    (void)in;
    return ST_WARNING;
}

static fsm_state_t n_normal(const fsm_input_t *in)
{
    (void)in;
    return ST_NORMAL;
}

static fsm_state_t n_after_crit_ack(const fsm_input_t *in)
{
    return (t_of(in) >= ((int32_t)in->warn_c100 - (int32_t)in->hyst_c100)) ? ST_WARNING : ST_NORMAL;
}

void fsm_init(fsm_ctx_t *ctx)
{
    ctx->state = ST_INIT;
    ctx->warn_run = 0U;
    ctx->crit_run = 0U;
    ctx->clear_run = 0U;
}

// cppcheck-suppress misra-c2012-8.7 ; ADR-0002 D-13（単体テストから直接呼ぶ公開関数）
/* 取り違えの検出：warn・crit の並びの取り違えは test_safety_fsm の test_classify_direct・test_row2_init_classifies で落ちる（open-questions #41） */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
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

static uint8_t count(uint8_t run, bool cond)
{
    if (!cond) {
        return 0U;
    }
    return (run < UINT8_MAX) ? (uint8_t)(run + 1U) : run;
}

static void update_counters(fsm_ctx_t *ctx, const fsm_input_t *in)
{
    if (temp_bad(in)) {
        ctx->warn_run = 0U;
        ctx->crit_run = 0U;
        ctx->clear_run = 0U;
        return;
    }
    int32_t t = t_of(in);
    ctx->warn_run = count(ctx->warn_run, t >= (int32_t)in->warn_c100);
    ctx->crit_run = count(ctx->crit_run, t >= (int32_t)in->crit_c100);
    ctx->clear_run = count(ctx->clear_run, t < ((int32_t)in->warn_c100 - (int32_t)in->hyst_c100));
}

void fsm_step(fsm_ctx_t *ctx, const fsm_input_t *in, fsm_output_t *out)
{
    static const fsm_row_t k_fsm_table[] = {
        {1U, BIT(ST_INIT), c_init_fault, n_fault},
        {2U, BIT(ST_INIT), c_init_done, n_classify},
        {3U, BIT(ST_NORMAL) | BIT(ST_WARNING) | BIT(ST_CRITICAL), c_temp_fault, n_fault},
        {4U, BIT(ST_SENSOR_FAULT), c_recover, n_classify},
        {5U, BIT(ST_NORMAL) | BIT(ST_WARNING), c_crit_hold, n_critical},
        {6U, BIT(ST_NORMAL), c_warn_hold, n_warning},
        {7U, BIT(ST_WARNING), c_clear_hold, n_normal},
        {8U, BIT(ST_CRITICAL), c_crit_ack, n_after_crit_ack},
    };

    update_counters(ctx, in);
    out->from = ctx->state;
    out->to = ctx->state;
    out->changed = false;
    out->event_code = (uint8_t)EV_NONE;
    out->severity = (uint8_t)SEV_INFO;
    out->row = 9U;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(k_fsm_table) / sizeof(k_fsm_table[0])); i++) {
        const fsm_row_t *r = &k_fsm_table[i];
        if (((r->states & BIT(ctx->state)) != 0U) && r->cond(ctx, in)) {
            fsm_state_t to = r->next(in);
            out->row = r->row;
            out->to = to;
            out->changed = (to != ctx->state);
            (void)event_map(ctx->state, to, &out->event_code, &out->severity);
            ctx->state = to; /* カウンタは戻さない */
            return;
        }
    }
    /* 行9：変わらない */
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
