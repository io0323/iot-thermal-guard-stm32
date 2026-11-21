/*
 * タスクの登録と配線（詳細設計書 第2章）。
 *
 * P1 の範囲：
 * - INIT は「temp_sensor が ready になるまで」。その間はフレームを送らず、CMD は捨てて数え、HEARTBEAT は受け付ける
 * - INIT を抜けたら STATE → EVENT BOOT（from・to は 0、CR-2）→ STATE_CHANGED の順に送る
 * - task_ui（ボタン・LED・ブザー）と task_health（生存報告・ウォッチドッグ）は表の行だけ置き、P3 で実装する。
 *   INIT 中の LED・ブザーの点灯とウォッチドッグのリフレッシュも P3
 */
#include "firmware.h"

#include "boot_id.h"
#include "command_handler.h"
#include "cooling_policy.h"
#include "link.h"
#include "report.h"
#include "scheduler.h"
#include "temp_sensor.h"
#include "upstream.h"
#include "version.h"

enum { T_SENSE, T_CONTROL, T_LINK, T_UI, T_REPORT, T_HEALTH };

static void task_sense(uint32_t now);
static void task_control(uint32_t now);
static void task_link(uint32_t now);
static void task_ui(uint32_t now);
static void task_report(uint32_t now);
static void task_health(uint32_t now);

/* 表の順：sense → control → link → ui → report → health。control は sense と同じ位相 */
static sched_task_t s_tasks[FW_TASK_COUNT] = {
    {task_sense, 100u, 0u, false, 0u},
    {task_control, 100u, 0u, false, 0u},
    {task_link, 10u, 0u, false, 0u},
    {task_ui, 10u, 0u, false, 0u},       /* P3 */
    {task_report, 1000u, 0u, false, 0u}, /* INIT を抜けた時刻 +1000ms から */
    {task_health, 100u, 0u, false, 0u},  /* P3 */
};

static const ports_t *s_p;
static const fw_observer_t *s_obs;
static sched_t s_sched;
static link_t s_link;
static upstream_t s_up;
static cmd_handler_t s_cmd;
static fsm_ctx_t s_fsm;
static report_t s_report;
static fsm_state_t s_state;
static uint16_t s_fan_pm;
static uint32_t s_boot_id;
static reset_reason_t s_reset_reason;
static uint16_t s_reset_count;

static bool in_init(void)
{
    return s_state == ST_INIT;
}

static void update_output(uint32_t now)
{
    temp_reading_t r;
    temp_get(&r);
    uint16_t duty = cooling_duty(s_state, r.temp_c100, s_cmd.override_active, s_cmd.override_pm, false);
    if (duty != s_fan_pm) {
        s_p->pwm->set_duty_pm(duty);
        s_fan_pm = duty;
        if (s_obs != NULL && s_obs->fan != NULL) {
            s_obs->fan(now, duty);
        }
    }
}

static void snapshot(uint32_t now, report_snapshot_t *s)
{
    temp_reading_t r;
    temp_get(&r);
    s->uptime_ms = now;
    s->temp_c100 = r.temp_c100;
    s->quality = r.quality;
    s->state = (uint8_t)s_state;
    s->fan_pm = s_fan_pm;
    s->fw_version = (uint16_t)FW_VERSION;
    s->boot_id = s_boot_id;
}

static void send_state(uint32_t now)
{
    report_snapshot_t s;
    link_frame_t f;
    snapshot(now, &s);
    report_make_state(&s, &f);
    (void)link_send(&s_link, &f);
}

/* temp_c100 はその時点の値（センサー故障中は −32768。open-questions #10） */
static void send_event(uint32_t now, event_code_t code, severity_t sev, uint8_t from, uint8_t to)
{
    temp_reading_t r;
    temp_get(&r);
    link_frame_t f = {.type = (uint8_t)FRAME_EVENT};
    f.u.event.uptime_ms = now;
    f.u.event.code = (uint8_t)code;
    f.u.event.severity = (uint8_t)sev;
    f.u.event.from = from;
    f.u.event.to = to;
    f.u.event.temp_c100 = r.temp_c100;
    (void)link_send(&s_link, &f);
}

static void task_sense(uint32_t now)
{
    uint16_t raw = 0u;
    if (s_p->adc->take_sample(&raw)) {
        temp_update(raw, now);
    }
    temp_tick(now);
}

static void on_init_exit(uint32_t now, const fsm_output_t *o)
{
    send_state(now);
    send_event(now, EV_BOOT, SEV_INFO, 0u, 0u); /* from・to は 0（CR-2） */
    send_event(now, (event_code_t)o->event_code, (severity_t)o->severity, (uint8_t)o->from, (uint8_t)o->to);
    report_start(&s_report, now);
    sched_start(&s_sched, T_REPORT, now + REPORT_PERIOD_MS);
}

static void task_control(uint32_t now)
{
    fsm_input_t in;
    fsm_output_t o;
    temp_get(&in.temp);
    in.init_done = in.temp.ready; /* P1 の INIT は「temp_sensor が ready になるまで」 */
    in.ack = false;               /* ボタンは P3 */
    in.warn_c100 = s_cmd.warn_c100;
    in.crit_c100 = s_cmd.crit_c100;
    in.hyst_c100 = s_cmd.hyst_c100;
    fsm_step(&s_fsm, &in, &o);
    if (o.changed) {
        s_state = o.to;
        if (s_obs != NULL && s_obs->state != NULL) {
            s_obs->state(now, o.from, o.to);
        }
        if (o.from == ST_INIT) {
            on_init_exit(now, &o);
        } else if (o.event_code != (uint8_t)EV_NONE) {
            send_event(now, (event_code_t)o.event_code, (severity_t)o.severity, (uint8_t)o.from, (uint8_t)o.to);
        } else {
            /* EVENT なしの遷移はない */
        }
    }
    update_output(now);
}

static void handle_cmd(uint32_t now, const link_cmd_t *c)
{
    temp_reading_t r;
    temp_get(&r);
    cmd_env_t env = {.state = s_state, .temp_c100 = r.temp_c100, .force_full = false};
    cmd_result_t res;
    cmd_handle(&s_cmd, c, now, &env, &res);
    if (res.output_changed) {
        update_output(now);
    }
    if (!res.has_ack) {
        return;
    }
    if (res.send_state) {
        send_state(now); /* GET_STATUS は STATE → ACK（open-questions #13） */
    }
    link_frame_t f = {.type = (uint8_t)FRAME_ACK};
    f.u.ack = res.ack;
    (void)link_send(&s_link, &f); /* 入らなくてもキャッシュには実行結果が残る（#14） */
}

static void handle_frame(uint32_t now, const link_frame_t *f)
{
    if (f->type == (uint8_t)FRAME_HEARTBEAT) {
        upstream_event_t ev = upstream_on_heartbeat(&s_up, now);
        if (ev == UPSTREAM_EV_RESTORED && !in_init()) {
            send_event(now, EV_UPSTREAM_RESTORED, SEV_INFO, (uint8_t)s_state, (uint8_t)s_state);
        }
    } else if (f->type == (uint8_t)FRAME_CMD) {
        handle_cmd(now, &f->u.cmd);
    } else {
        /* link が HEARTBEAT・CMD 以外を捨てている */
    }
}

static void receive(uint32_t now)
{
    uint8_t buf[64];
    size_t n;
    while ((n = s_p->uart->read(buf, sizeof buf)) > 0u) {
        for (size_t i = 0; i < n; i++) {
            link_frame_t f;
            link_rx_error_t err = LINK_RX_OK;
            link_rx_status_t st = link_rx_byte(&s_link, buf[i], &f, &err);
            size_t raw_len = 0u;
            const uint8_t *raw = link_rx_last_raw(&s_link, &raw_len);
            if (st == LINK_RX_FRAME) {
                if (s_obs != NULL && s_obs->rx != NULL) {
                    s_obs->rx(now, &f, raw, raw_len);
                }
                handle_frame(now, &f);
            } else if (st == LINK_RX_DISCARDED) {
                if (s_obs != NULL && s_obs->rx_error != NULL) {
                    s_obs->rx_error(now, err, raw, (err == LINK_RX_OVERFLOW) ? 0u : raw_len);
                }
            } else {
                /* フレームの途中 */
            }
        }
    }
}

static void transmit(uint32_t now)
{
    uint8_t enc[LINK_ENCODED_MAX];
    link_frame_t f;
    for (;;) {
        size_t n = link_tx_peek(&s_link, enc, sizeof enc, &f);
        if (n == 0u || s_p->uart->write(enc, n) != n) {
            break; /* 空き待ち。seq は送り出せたときだけ進む */
        }
        link_tx_commit(&s_link);
        if (s_obs != NULL && s_obs->tx != NULL) {
            s_obs->tx(now, &f, enc, n);
        }
    }
}

static void task_link(uint32_t now)
{
    receive(now);
    /* 上位監視の EVENT は INIT 中には送らず、INIT を抜けてから判定する */
    if (!in_init()) {
        if (cmd_lease_tick(&s_cmd, now)) {
            update_output(now);
        }
        if (upstream_tick(&s_up, now) == UPSTREAM_EV_LOST) {
            (void)cmd_clear_override(&s_cmd);
            update_output(now);
            send_event(now, EV_UPSTREAM_LOST, SEV_WARNING, (uint8_t)s_state, (uint8_t)s_state);
        }
    }
    transmit(now);
}

static void task_ui(uint32_t now)
{
    (void)now; /* P3：button・alarm */
}

static void task_report(uint32_t now)
{
    report_snapshot_t s;
    link_frame_t frames[REPORT_MAX_FRAMES];
    snapshot(now, &s);
    size_t n = report_tick(&s_report, &s, frames);
    for (size_t i = 0; i < n; i++) {
        (void)link_send(&s_link, &frames[i]);
    }
}

static void task_health(uint32_t now)
{
    (void)now; /* P3：health・ウォッチドッグ */
}

void firmware_init(const ports_t *ports, const fw_observer_t *obs)
{
    s_p = ports;
    s_obs = obs;
    s_fan_pm = (uint16_t)DUTY_FULL_PM; /* bsp_init でファンは100% */
    uint32_t now = s_p->tick->now_ms();

    s_reset_reason = s_p->reset->reason();
    s_reset_count = s_p->reset->count();
    s_boot_id = boot_id_make(s_p->entropy);
    if (s_obs != NULL && s_obs->boot != NULL) {
        s_obs->boot(now, s_boot_id, s_reset_reason, s_reset_count);
    }

    s_state = ST_INIT;
    temp_init();
    fsm_init(&s_fsm);
    link_init(&s_link);
    upstream_init(&s_up, now);
    cmd_init(&s_cmd);
    report_init(&s_report);
    for (size_t i = 0; i < FW_TASK_COUNT; i++) {
        s_tasks[i].enabled = false;
    }
    sched_init(&s_sched, s_tasks, FW_TASK_COUNT, s_p->tick);
    sched_start(&s_sched, T_SENSE, now);
    sched_start(&s_sched, T_CONTROL, now);
    sched_start(&s_sched, T_LINK, now);
}

void firmware_poll(void)
{
    sched_run(&s_sched);
}

uint32_t firmware_now_ms(void)
{
    return s_p->tick->now_ms();
}

void firmware_diag(fw_diag_t *out)
{
    *out = (fw_diag_t){0};
    out->reset_reason = (uint8_t)s_reset_reason;
    out->reset_count = s_reset_count;
    out->rx_cobs_errors = s_link.cnt.rx_cobs_errors;
    out->rx_len_errors = s_link.cnt.rx_len_errors;
    out->rx_crc_errors = s_link.cnt.rx_crc_errors;
    out->rx_unexpected = s_link.cnt.rx_unexpected;
    out->rx_overflow = s_link.cnt.rx_overflow;
    out->cmd_dropped_init = s_cmd.cmd_dropped_init;
    out->cmd_duplicates = s_cmd.cmd_duplicates;
    out->tx_dropped = s_link.cnt.tx_dropped;
    out->max_loop_cycles = s_sched.max_loop_cycles;
    for (size_t i = 0; i < FW_TASK_COUNT; i++) {
        out->max_task_cycles[i] = s_tasks[i].max_cycles;
    }
    out->overruns = s_sched.overruns;
}
