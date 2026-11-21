#include "moving_avg.h"

#include <stdint.h>

void moving_avg8_init(moving_avg8_t *a)
{
    a->sum = 0;
    a->count = 0U;
    a->next = 0U;
    for (uint8_t i = 0; i < MOVING_AVG_N; i++) {
        a->buf[i] = 0;
    }
}

void moving_avg8_push(moving_avg8_t *a, int16_t v)
{
    if (a->count >= MOVING_AVG_N) {
        a->sum -= a->buf[a->next];
    } else {
        a->count++;
    }
    a->buf[a->next] = v;
    a->sum += v;
    a->next = (uint8_t)((a->next + 1U) % MOVING_AVG_N);
}

bool moving_avg8_full(const moving_avg8_t *a)
{
    return a->count >= MOVING_AVG_N;
}

int16_t moving_avg8_value(const moving_avg8_t *a)
{
    if (a->count == 0U) {
        return 0;
    }
    int32_t n = (int32_t)a->count;
    int32_t half = n / 2;
    int32_t q = (a->sum >= 0) ? ((a->sum + half) / n) : -((-a->sum + half) / n);
    return (int16_t)q;
}
