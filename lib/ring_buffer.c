#include "ring_buffer.h"

#include <stddef.h>
#include <stdint.h>

bool rb_init(ring_buffer_t *rb, uint8_t *storage, uint32_t capacity)
{
    if ((storage == NULL) || (capacity == 0U) || (capacity > 0x80000000U) || ((capacity & (capacity - 1U)) != 0U)) {
        return false;
    }
    rb->buf = storage;
    rb->mask = capacity - 1U;
    rb->head = 0U;
    rb->tail = 0U;
    return true;
}

// cppcheck-suppress misra-c2012-8.7 ; ADR-0002 D-14（単体テストから直接呼ぶ公開関数）
uint32_t rb_count(const ring_buffer_t *rb)
{
    return rb->head - rb->tail;
}

uint32_t rb_free(const ring_buffer_t *rb)
{
    return (rb->mask + 1U) - rb_count(rb);
}

bool rb_push(ring_buffer_t *rb, uint8_t b)
{
    uint32_t head = rb->head;
    if ((head - rb->tail) > rb->mask) {
        return false;
    }
    rb->buf[head & rb->mask] = b;
    rb->head = head + 1U; /* データを書いてから添字を進める */
    return true;
}

bool rb_pop(ring_buffer_t *rb, uint8_t *out)
{
    uint32_t tail = rb->tail;
    if (rb->head == tail) {
        return false;
    }
    *out = rb->buf[tail & rb->mask];
    rb->tail = tail + 1U;
    return true;
}
