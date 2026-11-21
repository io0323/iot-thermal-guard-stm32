#include "host_log.h"

#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>

static FILE *s_out;

static const char *reset_name(reset_reason_t r)
{
    switch (r) {
    case RESET_UNKNOWN:
        return "unknown";
    case RESET_POWER_ON:
        return "power_on";
    case RESET_PIN:
        return "pin";
    case RESET_WATCHDOG:
        return "watchdog";
    case RESET_SOFTWARE:
        return "software";
    case RESET_BROWNOUT:
        return "brownout";
    case RESET_LOW_POWER:
        return "low_power";
    default:
        return "unknown";
    }
}

static void put_hex(const uint8_t *raw, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        fprintf(s_out, (i == 0u) ? "%02x" : " %02x", raw[i]);
    }
}

static void put_fields(const link_frame_t *f)
{
    switch ((frame_type_t)f->type) {
    case FRAME_TELEMETRY:
        fprintf(s_out, "{\"uptime_ms\":%" PRIu32 ",\"temp_c100\":%d,\"quality\":%u,\"state\":%u,\"fan_pm\":%u}",
                f->u.telemetry.uptime_ms, f->u.telemetry.temp_c100, f->u.telemetry.quality, f->u.telemetry.state,
                f->u.telemetry.fan_pm);
        break;
    case FRAME_EVENT:
        fprintf(s_out,
                "{\"uptime_ms\":%" PRIu32 ",\"code\":%u,\"severity\":%u,\"from\":%u,\"to\":%u,\"temp_c100\":%d}",
                f->u.event.uptime_ms, f->u.event.code, f->u.event.severity, f->u.event.from, f->u.event.to,
                f->u.event.temp_c100);
        break;
    case FRAME_HEARTBEAT:
        fprintf(s_out, "{\"uptime_ms\":%" PRIu32 "}", f->u.heartbeat.uptime_ms);
        break;
    case FRAME_STATE:
        fprintf(s_out, "{\"fw_version\":%u,\"boot_id\":\"%08" PRIx32 "\",\"period_ms\":%u}", f->u.state.fw_version,
                f->u.state.boot_id, f->u.state.period_ms);
        break;
    case FRAME_CMD:
        fprintf(s_out, "{\"cmd_ref\":%u,\"op\":%u,\"ttl_s\":%u,\"args\":\"", f->u.cmd.cmd_ref, f->u.cmd.op,
                f->u.cmd.ttl_s);
        put_hex(f->u.cmd.args, f->u.cmd.args_len);
        fputs("\"}", s_out);
        break;
    case FRAME_ACK:
        fprintf(s_out, "{\"cmd_ref\":%u,\"status\":%u,\"reason\":%u,\"limited\":%u,\"applied\":%u}", f->u.ack.cmd_ref,
                f->u.ack.status, f->u.ack.reason, f->u.ack.limited, f->u.ack.applied);
        break;
    default:
        fputs("{}", s_out);
        break;
    }
}

static void log_frame(const char *kind, uint32_t t, const link_frame_t *f, const uint8_t *raw, size_t len, bool with_zero)
{
    fprintf(s_out, "{\"t_ms\":%" PRIu32 ",\"kind\":\"%s\",\"type\":\"%s\",\"seq\":%u,\"fields\":", t, kind,
            link_frame_type_name(f->type), f->seq);
    put_fields(f);
    fputs(",\"hex\":\"", s_out);
    put_hex(raw, len);
    if (with_zero) {
        fputs((len > 0u) ? " 00" : "00", s_out);
    }
    fputs("\"}\n", s_out);
}

static void on_boot(uint32_t t, uint32_t boot_id, reset_reason_t reason, uint16_t count)
{
    fprintf(s_out,
            "{\"t_ms\":%" PRIu32 ",\"kind\":\"boot\",\"boot_id\":\"%08" PRIx32
            "\",\"reset_reason\":\"%s\",\"reset_count\":%u}\n",
            t, boot_id, reset_name(reason), count);
}

static void on_state(uint32_t t, fsm_state_t from, fsm_state_t to)
{
    fprintf(s_out, "{\"t_ms\":%" PRIu32 ",\"kind\":\"state\",\"from\":\"%s\",\"to\":\"%s\"}\n", t, fsm_state_name(from),
            fsm_state_name(to));
}

static void on_fan(uint32_t t, uint16_t pm)
{
    fprintf(s_out, "{\"t_ms\":%" PRIu32 ",\"kind\":\"fan\",\"pm\":%u}\n", t, pm);
}

static void on_tx(uint32_t t, const link_frame_t *f, const uint8_t *raw, size_t len)
{
    log_frame("tx", t, f, raw, len, false); /* raw は末尾の 0x00 を含む */
}

static void on_rx(uint32_t t, const link_frame_t *f, const uint8_t *raw, size_t len)
{
    log_frame("rx", t, f, raw, len, true); /* raw は 0x00 を含まない */
}

static void on_rx_error(uint32_t t, link_rx_error_t reason, const uint8_t *raw, size_t len)
{
    fprintf(s_out, "{\"t_ms\":%" PRIu32 ",\"kind\":\"rx_error\",\"reason\":\"%s\",\"hex\":\"", t,
            link_rx_error_name(reason));
    put_hex(raw, len);
    fputs("\"}\n", s_out);
}

static void on_led(uint32_t t, led_t led, bool on)
{
    static const char *const names[] = {"green", "yellow", "red"};
    const char *name = ((size_t)led < (sizeof names / sizeof names[0])) ? names[led] : "unknown";
    fprintf(s_out, "{\"t_ms\":%" PRIu32 ",\"kind\":\"led\",\"led\":\"%s\",\"on\":%s}\n", t, name, on ? "true" : "false");
}

static void on_buzzer(uint32_t t, bool on)
{
    fprintf(s_out, "{\"t_ms\":%" PRIu32 ",\"kind\":\"buzzer\",\"on\":%s}\n", t, on ? "true" : "false");
}

static const fw_observer_t s_obs = {on_boot, on_state, on_fan, on_tx, on_rx, on_rx_error, on_led, on_buzzer};

bool host_log_open(const char *path, bool append)
{
    if (path == NULL) {
        s_out = stderr;
        return true;
    }
    /* O_CLOEXEC：疑似ウォッチドッグの execv で次のプロセスに漏らさない（次のプロセスが開き直す） */
    int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC);
    int fd = open(path, flags, 0644);
    if (fd < 0) {
        return false;
    }
    s_out = fdopen(fd, append ? "a" : "w");
    if (s_out == NULL) {
        (void)close(fd);
        return false;
    }
    (void)setvbuf(s_out, NULL, _IOLBF, 0); /* 1行ずつ書き出す */
    return true;
}

const fw_observer_t *host_log_observer(void)
{
    return &s_obs;
}

void host_log_diag(uint32_t t, const fw_diag_t *d)
{
    fprintf(s_out,
            "{\"t_ms\":%" PRIu32 ",\"kind\":\"diag\",\"reset_reason\":\"%s\",\"reset_count\":%u,"
            "\"rx_cobs_errors\":%u,\"rx_len_errors\":%u,\"rx_crc_errors\":%u,\"rx_unexpected\":%u,\"rx_overflow\":%u,"
            "\"cmd_dropped_init\":%u,\"cmd_duplicates\":%u,\"tx_dropped\":%u,\"temp_clamped\":%u,\"temp_rejected\":%u,"
            "\"max_loop_cycles\":%" PRIu32 ",\"max_task_cycles\":[",
            t, reset_name((reset_reason_t)d->reset_reason), d->reset_count, d->rx_cobs_errors, d->rx_len_errors,
            d->rx_crc_errors, d->rx_unexpected, d->rx_overflow, d->cmd_dropped_init, d->cmd_duplicates, d->tx_dropped,
            d->temp_clamped, d->temp_rejected, d->max_loop_cycles);
    for (size_t i = 0; i < FW_TASK_COUNT; i++) {
        fprintf(s_out, (i == 0u) ? "%" PRIu32 : ",%" PRIu32, d->max_task_cycles[i]);
    }
    fprintf(s_out, "],\"overruns\":%u,\"uart_ring_dropped\":%u,\"uart_errors\":%u,\"stack_peak\":%u}\n", d->overruns,
            d->uart_ring_dropped, d->uart_errors, d->stack_peak);
    (void)fflush(s_out);
}

void host_log_close(void)
{
    if (s_out != NULL && s_out != stderr) {
        (void)fclose(s_out);
    }
    s_out = NULL;
}
