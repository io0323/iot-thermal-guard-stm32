#include "health.h"

#include <stdint.h>

#include "tick.h"

#define ALL_BITS ((uint8_t)((1U << (unsigned)HEALTH_TASK_COUNT) - 1U))

void health_init(health_t *h)
{
    h->bits = 0U;
    h->has_report_ref = false;
    h->report_ref_ms = 0U;
}

void health_checkin(health_t *h, health_task_t task)
{
    if (task < HEALTH_TASK_COUNT) {
        h->bits = (uint8_t)(h->bits | (1U << (unsigned)task));
    }
}

void health_report_ran(health_t *h, uint32_t now)
{
    h->has_report_ref = true;
    h->report_ref_ms = now;
}

bool health_check(health_t *h, uint32_t now)
{
    if (h->bits != ALL_BITS || !h->has_report_ref) {
        return false;
    }
    /* now − ref ≦ 1500（#39）。!tick_reached(now, ref + 1500 + 1) と同じ */
    if (tick_reached(now, h->report_ref_ms + HEALTH_REPORT_DEADLINE_MS + 1U)) {
        return false;
    }
    h->bits = 0U;
    return true;
}

void health_clear(health_t *h)
{
    h->bits = 0U;
}
