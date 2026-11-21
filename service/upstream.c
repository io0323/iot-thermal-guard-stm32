#include "upstream.h"

#include <stdint.h>

#include "tick.h"

void upstream_init(upstream_t *u, uint32_t now_ms)
{
    u->state = UPSTREAM_UNKNOWN;
    u->last_ms = now_ms;
}

upstream_event_t upstream_on_heartbeat(upstream_t *u, uint32_t now_ms)
{
    upstream_state_t prev = u->state;
    u->state = UPSTREAM_ALIVE;
    u->last_ms = now_ms;
    return (prev == UPSTREAM_LOST) ? UPSTREAM_EV_RESTORED : UPSTREAM_EV_NONE;
}

upstream_event_t upstream_tick(upstream_t *u, uint32_t now_ms)
{
    if (u->state == UPSTREAM_LOST) {
        return UPSTREAM_EV_NONE;
    }
    if (tick_reached(now_ms, u->last_ms + UPSTREAM_LOST_MS)) {
        u->state = UPSTREAM_LOST;
        return UPSTREAM_EV_LOST;
    }
    return UPSTREAM_EV_NONE;
}

upstream_state_t upstream_state(const upstream_t *u)
{
    return u->state;
}
