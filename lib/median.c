#include "median.h"

void median5_init(median5_t *m)
{
    m->count = 0u;
    m->next = 0u;
    for (uint8_t i = 0; i < MEDIAN_N; i++) {
        m->buf[i] = 0;
    }
}

void median5_push(median5_t *m, int16_t v)
{
    m->buf[m->next] = v;
    m->next = (uint8_t)((m->next + 1u) % MEDIAN_N);
    if (m->count < MEDIAN_N) {
        m->count++;
    }
}

bool median5_full(const median5_t *m)
{
    return m->count >= MEDIAN_N;
}

int16_t median5_value(const median5_t *m)
{
    if (m->count == 0u) {
        return 0;
    }
    int16_t s[MEDIAN_N];
    uint8_t n = m->count;
    for (uint8_t i = 0; i < n; i++) {
        s[i] = m->buf[i]; /* 満たされる前は 0..count-1 に入っている */
    }
    /* 挿入ソート（5点なので十分） */
    for (uint8_t i = 1; i < n; i++) {
        int16_t x = s[i];
        uint8_t j = i;
        while (j > 0u && s[j - 1u] > x) {
            s[j] = s[j - 1u];
            j--;
        }
        s[j] = x;
    }
    return s[(uint8_t)((n - 1u) / 2u)];
}
