#include "scheduler.h"

#include "sat.h"

void sched_init(sched_t *s, sched_task_t *tasks, size_t count, const tick_port_t *tick)
{
    s->tasks = tasks;
    s->count = count;
    s->tick = tick;
    s->overruns = 0u;
    s->max_loop_cycles = 0u;
    for (size_t i = 0; i < count; i++) {
        tasks[i].max_cycles = 0u;
    }
}

void sched_start(sched_t *s, size_t id, uint32_t first_ms)
{
    if (id < s->count) {
        s->tasks[id].next_ms = first_ms;
        s->tasks[id].enabled = true;
    }
}

static void run_task(sched_t *s, sched_task_t *t, uint32_t now)
{
    uint32_t c0 = s->tick->cycles();
    t->fn(now);
    uint32_t dc = s->tick->cycles() - c0;
    if (dc > t->max_cycles) {
        t->max_cycles = dc;
    }
    t->next_ms += t->period_ms;
    if ((int32_t)(now - t->next_ms) >= 0) {
        /* 遅れ：追いつくまで実行せず、位相を保って未来の周期へ進める */
        uint32_t behind = now - t->next_ms;
        t->next_ms += ((behind / t->period_ms) + 1u) * t->period_ms;
        sat_inc_u16(&s->overruns);
    }
}

void sched_run(sched_t *s)
{
    uint32_t c0 = s->tick->cycles();
    uint32_t now = s->tick->now_ms();
    for (size_t i = 0; i < s->count; i++) {
        sched_task_t *t = &s->tasks[i];
        if (t->enabled && t->period_ms != 0u && (int32_t)(now - t->next_ms) >= 0) {
            run_task(s, t, now);
        }
    }
    uint32_t dc = s->tick->cycles() - c0;
    if (dc > s->max_loop_cycles) {
        s->max_loop_cycles = dc;
    }
}
