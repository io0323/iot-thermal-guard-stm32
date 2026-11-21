#include "alarm.h"

#include <stdint.h>

#include "ports.h"
#include "safety_fsm.h"

#define INIT_STEP_MS 100U
#define WARN_PERIOD_MS 1000U
#define CRIT_LED_PERIOD_MS 250U
#define CRIT_BUZZER_PERIOD_MS 500U
#define FORCE_PERIOD_MS 500U

/* 周期 period の前半（50%）なら true */
static bool first_half(uint32_t elapsed, uint32_t period)
{
    return (elapsed % period) < (period / 2U);
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

/* 取り違えの検出：s と now の取り違えは test_alarm の test_critical_red_4hz_buzzer_250・test_warning_yellow_1hz で落ちる（open-questions #41） */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
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
    out->led[LED_YELLOW] = el >= INIT_STEP_MS && el < (2U * INIT_STEP_MS);
    out->led[LED_GREEN] = el >= (2U * INIT_STEP_MS) && el < (3U * INIT_STEP_MS);
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
