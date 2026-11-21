/*
 * タスクの登録と配線（詳細設計書 第2章）。
 *
 * - INIT は temp_sensor が ready になる12サンプル目（約1.1秒）で抜ける。起動から2000ms までに ready にならなければ
 *   遷移表の行1で SENSOR_FAULT に入る（open-questions #26）。INIT 中に故障しても早くは抜けない。
 *   INIT の間はフレームを送らず、CMD は捨てて数え、HEARTBEAT は受け付ける
 * - INIT を抜けたら STATE → EVENT BOOT（from・to は 0、CR-2）→ STATE_CHANGED の順に送る
 * - task_ui：ボタンを読み、短押しは「鳴っている」なら消音、それ以外は承認として次の fsm_step に1回だけ渡す
 *   （当たる行がなくてもそのステップで消費する）。長押しは強制全開を切り替え、出力をすぐ計算し直す。
 *   そのあと LED・ブザーを状態のパターンに合わせる
 * - 強制全開は再起動で解除され、上位断では解除しない（ローカルの操作なので）
 * - task_health：INIT の間は直接リフレッシュする。INIT を抜けたら、sense・control・link・ui の生存報告と
 *   report の期限（最後の実行、まだなければ INIT を抜けた時刻から1500ms 以内）を確かめてからリフレッシュする
 */
#include "firmware.h"

#include <stddef.h>
#include <stdint.h>

#include "alarm.h"
#include "boot_id.h"
#include "button.h"
#include "command_handler.h"
#include "cooling_policy.h"
#include "diag.h"
#include "health.h"
#include "link.h"
#include "link_frame.h"
#include "ports.h"
#include "report.h"
#include "safety_fsm.h"
#include "scheduler.h"
#include "temp_sensor.h"
#include "tick.h"
#include "upstream.h"
#include "version.h"

enum { T_SENSE, T_CONTROL, T_LINK, T_UI, T_REPORT, T_HEALTH };

static void task_sense(uint32_t now);
static void task_control(uint32_t now);
static void task_link(uint32_t now);
static void task_ui(uint32_t now);
static void task_report(uint32_t now);
static void task_health(uint32_t now);

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
static uint32_t s_boot_ms;
static button_t s_button;
static alarm_t s_alarm;
static health_t s_health;
static bool s_force_full;
static bool s_ack_pending;

static bool in_init(void)
{
    return s_state == ST_INIT;
}

static void update_output(uint32_t now)
{
    temp_reading_t r;
    temp_get(&r);
    uint16_t duty = cooling_duty(s_state, r.temp_c100, s_cmd.override_active, s_cmd.override_pm, s_force_full);
    if (duty != s_fan_pm) {
        s_p->pwm->set_duty_pm(duty);
        s_fan_pm = duty;
        if ((s_obs != NULL) && (s_obs->fan != NULL)) {
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
/* 取り違えの検出：並ぶ5つの取り違えは test_scenarios の test_init_adc_stall（uptime 2000・コード5・重大度2・from 0・to 4）で落ちる（open-questions #41） */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
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
    uint16_t raw = 0U;
    if (s_p->adc->take_sample(&raw)) {
        temp_update(raw, now);
    }
    temp_tick(now);
    health_checkin(&s_health, HEALTH_SENSE);
}

static void on_init_exit(uint32_t now, const fsm_output_t *o)
{
    send_state(now);
    send_event(now, EV_BOOT, SEV_INFO, 0U, 0U); /* from・to は 0（CR-2） */
    send_event(now, (event_code_t)o->event_code, (severity_t)o->severity, (uint8_t)o->from, (uint8_t)o->to);
    report_start(&s_report, now);
    health_report_ran(&s_health, now); /* report がまだ動いていない間の期限の起点（第7章） */
    sched_start(&s_sched, T_REPORT, now + REPORT_PERIOD_MS);
}

static void task_control(uint32_t now)
{
    fsm_input_t in;
    fsm_output_t o;
    temp_get(&in.temp);
    /* INIT を終える時点：ready になった、または起動から2000ms（#26） */
    in.init_done = in.temp.ready || tick_reached(now, s_boot_ms + FW_INIT_MAX_MS);
    in.ack = s_ack_pending; /* 短押しの承認は1回だけ渡し、当たる行がなくても消費する */
    s_ack_pending = false;
    in.warn_c100 = s_cmd.warn_c100;
    in.crit_c100 = s_cmd.crit_c100;
    in.hyst_c100 = s_cmd.hyst_c100;
    fsm_step(&s_fsm, &in, &o);
    if (o.changed) {
        s_state = o.to;
        alarm_set_state(&s_alarm, o.to, now);
        if ((s_obs != NULL) && (s_obs->state != NULL)) {
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
    health_checkin(&s_health, HEALTH_CONTROL);
}

static void handle_cmd(uint32_t now, const link_cmd_t *c)
{
    temp_reading_t r;
    temp_get(&r);
    cmd_env_t env = {.state = s_state, .temp_c100 = r.temp_c100, .force_full = s_force_full};
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
        if ((ev == UPSTREAM_EV_RESTORED) && (!in_init())) {
            send_event(now, EV_UPSTREAM_RESTORED, SEV_INFO, (uint8_t)s_state, (uint8_t)s_state);
        }
    } else if (f->type == (uint8_t)FRAME_CMD) {
        handle_cmd(now, &f->u.cmd);
    } else {
        /* link が HEARTBEAT・CMD 以外を捨てている */
    }
}

/* 受信したバイト列を link に渡し、フレームがそろうたびに処理する */
static void receive_chunk(uint32_t now, const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        link_frame_t f;
        link_rx_error_t err = LINK_RX_OK;
        link_rx_status_t st = link_rx_byte(&s_link, buf[i], &f, &err);
        size_t raw_len = 0U;
        const uint8_t *raw = link_rx_last_raw(&s_link, &raw_len);
        if (st == LINK_RX_FRAME) {
            if ((s_obs != NULL) && (s_obs->rx != NULL)) {
                s_obs->rx(now, &f, raw, raw_len);
            }
            handle_frame(now, &f);
        } else if (st == LINK_RX_DISCARDED) {
            if ((s_obs != NULL) && (s_obs->rx_error != NULL)) {
                s_obs->rx_error(now, err, raw, (err == LINK_RX_OVERFLOW) ? 0U : raw_len);
            }
        } else {
            /* フレームの途中 */
        }
    }
}

static void receive(uint32_t now)
{
    uint8_t buf[64];
    for (;;) {
        size_t n = s_p->uart->read(buf, sizeof buf);
        if (n == 0U) {
            break;
        }
        receive_chunk(now, buf, n);
    }
}

static void transmit(uint32_t now)
{
    uint8_t enc[LINK_ENCODED_MAX];
    link_frame_t f;
    for (;;) {
        size_t n = link_tx_peek(&s_link, enc, sizeof enc, &f);
        if ((n == 0U) || (s_p->uart->write(enc, n) != n)) {
            break; /* 空き待ち。seq は送り出せたときだけ進む */
        }
        link_tx_commit(&s_link);
        if ((s_obs != NULL) && (s_obs->tx != NULL)) {
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
    health_checkin(&s_health, HEALTH_LINK);
}

/* 取り違えの検出：now と ev の取り違えは test_scenarios の test_long_press_toggles_force_full（長押しで強制全開にならない）で落ちる（open-questions #41） */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void on_button(uint32_t now, button_event_t ev)
{
    if (ev == BTN_EV_LONG) {
        s_force_full = !s_force_full;
        update_output(now);
    } else if (alarm_sounding(&s_alarm)) {
        alarm_silence(&s_alarm);
    } else {
        s_ack_pending = true;
    }
}

static void apply_alarm(uint32_t now, bool force)
{
    static alarm_out_t s_alarm_out;

    alarm_out_t out;
    alarm_output(&s_alarm, now, s_force_full, &out);
    for (size_t i = 0; i < ALARM_LED_COUNT; i++) {
        if (force || (out.led[i] != s_alarm_out.led[i])) {
            s_p->gpio->set_led((led_t)i, out.led[i]);
            if ((s_obs != NULL) && (s_obs->led != NULL)) {
                s_obs->led(now, (led_t)i, out.led[i]);
            }
        }
    }
    if (force || (out.buzzer != s_alarm_out.buzzer)) {
        s_p->gpio->set_buzzer(out.buzzer);
        if ((s_obs != NULL) && (s_obs->buzzer != NULL)) {
            s_obs->buzzer(now, out.buzzer);
        }
    }
    s_alarm_out = out;
}

static void task_ui(uint32_t now)
{
    button_event_t ev;
    button_update(&s_button, s_p->gpio->button_pressed(), now);
    while (button_pop(&s_button, &ev)) {
        on_button(now, ev);
    }
    apply_alarm(now, false);
    health_checkin(&s_health, HEALTH_UI);
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
    health_report_ran(&s_health, now);
}

static void task_health(uint32_t now)
{
    if (in_init()) {
        health_clear(&s_health); /* INIT の間は起動処理が直接リフレッシュする */
        s_p->wdt->refresh();
    } else if (health_check(&s_health, now)) {
        s_p->wdt->refresh();
    } else {
        /* そろっていない、または report の期限切れ：リフレッシュしない */
    }
}

void firmware_init(const ports_t *ports, const fw_observer_t *obs)
{
    /* 表の順：sense → control → link → ui → report → health。control は sense と同じ位相 */
    static sched_task_t s_tasks[FW_TASK_COUNT] = {
        {task_sense, 100U, 0U, false, 0U},
        {task_control, 100U, 0U, false, 0U},
        {task_link, 10U, 0U, false, 0U},
        {task_ui, 10U, 0U, false, 0U},
        {task_report, 1000U, 0U, false, 0U}, /* INIT を抜けた時刻 +1000ms から */
        {task_health, 100U, 0U, false, 0U},
    };

    s_p = ports;
    s_obs = obs;
    s_fan_pm = (uint16_t)DUTY_FULL_PM; /* bsp_init でファンは100% */
    uint32_t now = s_p->tick->now_ms();

    s_reset_reason = s_p->reset->reason();
    s_reset_count = s_p->reset->count();
    s_boot_id = boot_id_make(s_p->entropy);
    if ((s_obs != NULL) && (s_obs->boot != NULL)) {
        s_obs->boot(now, s_boot_id, s_reset_reason, s_reset_count);
    }

    s_state = ST_INIT;
    s_boot_ms = now;
    temp_init(s_p->adc, now);
    fsm_init(&s_fsm);
    link_init(&s_link);
    upstream_init(&s_up, now);
    cmd_init(&s_cmd);
    report_init(&s_report);
    button_init(&s_button);
    alarm_init(&s_alarm, now);
    health_init(&s_health);
    s_force_full = false; /* 再起動で解除 */
    s_ack_pending = false;
    s_p->wdt->refresh();
    apply_alarm(now, true);
    for (size_t i = 0; i < FW_TASK_COUNT; i++) {
        s_tasks[i].enabled = false;
    }
    sched_init(&s_sched, s_tasks, FW_TASK_COUNT, s_p->tick);
    sched_start(&s_sched, T_SENSE, now);
    sched_start(&s_sched, T_CONTROL, now);
    sched_start(&s_sched, T_LINK, now);
    sched_start(&s_sched, T_UI, now);
    sched_start(&s_sched, T_HEALTH, now);
}

void firmware_poll(void)
{
    sched_run(&s_sched);
}

bool firmware_force_full(void)
{
    return s_force_full;
}

uint32_t firmware_now_ms(void)
{
    return s_p->tick->now_ms();
}

void firmware_diag(fw_diag_t *out)
{
    diag_sources_t src = {
        .reset_reason = s_reset_reason,
        .reset_count = s_reset_count,
        .link = &s_link.cnt,
        .sched = &s_sched,
        .cmd_dropped_init = s_cmd.cmd_dropped_init,
        .cmd_duplicates = s_cmd.cmd_duplicates,
    };
    temp_counters(&src.temp_clamped, &src.temp_rejected);
    s_p->bsp_diag->read(&src.bsp);
    diag_collect(out, &src);
}
