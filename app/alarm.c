#include "alarm.h"

#define INIT_STEP_MS 100u
#define WARN_PERIOD_MS 1000u
#define CRIT_LED_PERIOD_MS 250u
#define CRIT_BUZZER_PERIOD_MS 500u
#define FORCE_PERIOD_MS 500u

/* 周期 period の前半（50%）なら true */
static bool first_half(uint32_t elapsed, uint32_t period)
{
    return (elapsed % period) < (period / 2u);
}

static bool silenceable(fsm_state_t s)
{
    return s == ST_CRITICAL || s == ST_SENSOR_FAULT;
}

void alarm_init(alarm_t *a, uint32_t now)
{
    a->state = ST_INIT;
    a->entered_ms = now;
    a->silenced = false;
}

void alarm_set_state(alarm_t *a, fsm_state_t s, uint32_t now)
{
    if (s == a->state) {
        return;
    }
    a->state = s;
    a->entered_ms = now;
    if (silenceable(s)) {
        a->silenced = false;
    }
}

bool alarm_sounding(const alarm_t *a)
{
    return silenceable(a->state) && !a->silenced;
}

void alarm_silence(alarm_t *a)
{
    a->silenced = true;
}

static void init_pattern(uint32_t el, alarm_out_t *out)
{
    out->led[LED_RED] = el < INIT_STEP_MS;
    out->led[LED_YELLOW] = el >= INIT_STEP_MS && el < (2u * INIT_STEP_MS);
    out->led[LED_GREEN] = el >= (2u * INIT_STEP_MS) && el < (3u * INIT_STEP_MS);
    out->buzzer = el < INIT_STEP_MS;
}

void alarm_output(const alarm_t *a, uint32_t now, bool force_full, alarm_out_t *out)
{
    uint32_t el = now - a->entered_ms;
    out->led[LED_GREEN] = false;
    out->led[LED_YELLOW] = false;
    out->led[LED_RED] = false;
    out->buzzer = false;
    switch (a->state) {
    case ST_INIT:
        init_pattern(el, out);
        break;
    case ST_NORMAL:
        out->led[LED_GREEN] = force_full ? first_half(el, FORCE_PERIOD_MS) : true;
        break;
    case ST_WARNING:
        out->led[LED_YELLOW] = first_half(el, WARN_PERIOD_MS);
        break;
    case ST_CRITICAL:
        out->led[LED_RED] = first_half(el, CRIT_LED_PERIOD_MS);
        out->buzzer = !a->silenced && first_half(el, CRIT_BUZZER_PERIOD_MS);
        break;
    case ST_SENSOR_FAULT:
        out->led[LED_RED] = true;
        out->buzzer = !a->silenced;
        break;
    default:
        break;
    }
}
