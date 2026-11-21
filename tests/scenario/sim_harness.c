#include "sim_harness.h"

#include <string.h>

#include "fake_ports.h"

static ports_t s_ports;
static scenario_player_t s_player;
static sim_log_t s_log;
static uint32_t s_now;
static bool s_heartbeat;
static uint16_t s_seq_hb;
static uint16_t s_seq_cmd;

static void on_state(uint32_t t, fsm_state_t from, fsm_state_t to)
{
    if (s_log.n_state < SIM_MAX_RECORDS) {
        s_log.state[s_log.n_state++] = (sim_state_t){t, from, to};
    }
}

static void on_fan(uint32_t t, uint16_t pm)
{
    if (s_log.n_pwm < SIM_MAX_RECORDS) {
        s_log.pwm[s_log.n_pwm++] = (sim_pwm_t){t, pm};
    }
}

static void on_tx(uint32_t t, const link_frame_t *f, const uint8_t *raw, size_t len)
{
    (void)raw;
    (void)len;
    if (s_log.n_tx < SIM_MAX_RECORDS) {
        s_log.tx[s_log.n_tx].t_ms = t;
        s_log.tx[s_log.n_tx].f = *f;
        s_log.n_tx++;
    }
}

static const fw_observer_t s_obs = {NULL, on_state, on_fan, on_tx, NULL, NULL};

void sim_init(const scenario_t *sc)
{
    memset(&s_log, 0, sizeof s_log);
    fake_ports_init(&s_ports);
    scenario_player_init(&s_player, sc);
    s_now = 0u;
    s_heartbeat = true;
    s_seq_hb = 0u;
    s_seq_cmd = 0u;
    g_fake.now_ms = 0u;
    firmware_init(&s_ports, &s_obs);
}

void sim_set_heartbeat(bool on)
{
    s_heartbeat = on;
}

void sim_send(link_frame_t f)
{
    if (f.type == (uint8_t)FRAME_HEARTBEAT) {
        f.seq = s_seq_hb++;
    } else {
        f.seq = s_seq_cmd++;
    }
    uint8_t enc[LINK_ENCODED_MAX];
    size_t n = link_frame_encode(&f, enc, sizeof enc);
    fake_uart_feed(enc, n);
}

void sim_run_until(uint32_t t_ms)
{
    while ((int32_t)(t_ms - s_now) >= 0) {
        g_fake.now_ms = s_now;
        scenario_player_advance(&s_player, s_now);
        uint16_t raw = 0u;
        g_fake.adc_ready = scenario_player_sample(&s_player, s_now, &raw);
        g_fake.adc_raw = raw;
        if (s_heartbeat && (s_now % 1000u) == 500u) {
            link_frame_t hb = {.type = (uint8_t)FRAME_HEARTBEAT};
            hb.u.heartbeat.uptime_ms = s_now;
            sim_send(hb);
        }
        firmware_poll();
        g_fake.tx_len = 0u; /* 送信は観測フックで記録済み。フェイクの送信バッファを空ける */
        s_now++;
    }
}

uint32_t sim_now(void)
{
    return s_now;
}

const sim_log_t *sim_log(void)
{
    return &s_log;
}

fsm_state_t sim_state(void)
{
    return (s_log.n_state == 0u) ? ST_INIT : s_log.state[s_log.n_state - 1u].to;
}

uint16_t sim_pwm_now(void)
{
    return g_fake.pwm_pm;
}

int64_t sim_first_pwm_at_least(uint32_t since, uint16_t pm)
{
    for (size_t i = 0; i < s_log.n_pwm; i++) {
        if (s_log.pwm[i].t_ms >= since && s_log.pwm[i].pm >= pm) {
            return (int64_t)s_log.pwm[i].t_ms;
        }
    }
    return -1;
}

int64_t sim_first_state(fsm_state_t to)
{
    for (size_t i = 0; i < s_log.n_state; i++) {
        if (s_log.state[i].to == to) {
            return (int64_t)s_log.state[i].t_ms;
        }
    }
    return -1;
}

int sim_find_tx(size_t from, uint8_t type, int code)
{
    for (size_t i = from; i < s_log.n_tx; i++) {
        const link_frame_t *f = &s_log.tx[i].f;
        if (f->type == type && (code < 0 || (type == (uint8_t)FRAME_EVENT && f->u.event.code == (uint8_t)code))) {
            return (int)i;
        }
    }
    return -1;
}

size_t sim_count_events(uint8_t code)
{
    size_t n = 0u;
    for (size_t i = 0; i < s_log.n_tx; i++) {
        if (s_log.tx[i].f.type == (uint8_t)FRAME_EVENT && s_log.tx[i].f.u.event.code == code) {
            n++;
        }
    }
    return n;
}

int64_t sim_first_input_at_least(const scenario_t *sc, double temp_c, uint32_t until)
{
    for (uint32_t t = 0; t <= until; t++) {
        if (scenario_temp_at(sc, t) >= temp_c) {
            return (int64_t)t;
        }
    }
    return -1;
}
