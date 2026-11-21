#include "diag.h"

#include "sat.h"

void diag_collect(diag_t *out, const diag_sources_t *src)
{
    *out = (diag_t){0};
    out->reset_reason = (uint8_t)src->reset_reason;
    out->reset_count = src->reset_count;
    out->rx_cobs_errors = src->link->rx_cobs_errors;
    out->rx_len_errors = src->link->rx_len_errors;
    out->rx_crc_errors = src->link->rx_crc_errors;
    out->rx_unexpected = src->link->rx_unexpected;
    out->rx_overflow = src->link->rx_overflow;
    out->tx_dropped = src->link->tx_dropped;
    out->cmd_dropped_init = src->cmd_dropped_init;
    out->cmd_duplicates = src->cmd_duplicates;
    out->temp_clamped = src->temp_clamped;
    out->temp_rejected = src->temp_rejected;
    out->max_loop_cycles = src->sched->max_loop_cycles;
    for (size_t i = 0; i < DIAG_TASK_COUNT && i < src->sched->count; i++) {
        out->max_task_cycles[i] = src->sched->tasks[i].max_cycles;
    }
    out->overruns = src->sched->overruns;
}

uint16_t diag_next_reset_count(uint16_t count)
{
    uint16_t c = count;
    sat_inc_u16(&c);
    return c;
}
