#include "report.h"

#include <stdint.h>

#include "link_frame.h"

void report_init(report_t *r)
{
    r->started = false;
    r->last_state_ms = 0U;
}

void report_start(report_t *r, uint32_t now_ms)
{
    r->started = true;
    r->last_state_ms = now_ms;
}

void report_make_state(const report_snapshot_t *s, link_frame_t *out)
{
    out->type = (uint8_t)FRAME_STATE;
    out->seq = 0U;
    out->u.state.fw_version = s->fw_version;
    out->u.state.boot_id = s->boot_id;
    out->u.state.period_ms = (uint16_t)REPORT_PERIOD_MS;
}

size_t report_tick(report_t *r, const report_snapshot_t *s, link_frame_t out[REPORT_MAX_FRAMES])
{
    if (!r->started) {
        return 0U;
    }
    size_t n = 0U;
    out[n].type = (uint8_t)FRAME_TELEMETRY;
    out[n].seq = 0U;
    out[n].u.telemetry.uptime_ms = s->uptime_ms;
    out[n].u.telemetry.temp_c100 = s->temp_c100;
    out[n].u.telemetry.quality = s->quality;
    out[n].u.telemetry.state = s->state;
    out[n].u.telemetry.fan_pm = s->fan_pm;
    n++;
    out[n].type = (uint8_t)FRAME_HEARTBEAT;
    out[n].seq = 0U;
    out[n].u.heartbeat.uptime_ms = s->uptime_ms;
    n++;
    if ((int32_t)(s->uptime_ms - (r->last_state_ms + REPORT_STATE_PERIOD_MS)) >= 0) {
        report_make_state(s, &out[n]);
        n++;
        r->last_state_ms = s->uptime_ms;
    }
    return n;
}
