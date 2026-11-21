#include "ring_buffer.h"

#include <stddef.h>

bool rb_init(ring_buffer_t *rb, uint8_t *storage, uint32_t capacity)
{
    if (storage == NULL || capacity == 0u || capacity > 0x80000000u || (capacity & (capacity - 1u)) != 0u) {
        return false;
    }
    rb->buf = storage;
    rb->mask = capacity - 1u;
    rb->head = 0u;
    rb->tail = 0u;
    return true;
}

uint32_t rb_count(const ring_buffer_t *rb)
{
    return rb->head - rb->tail;
}

uint32_t rb_free(const ring_buffer_t *rb)
{
    return (rb->mask + 1u) - rb_count(rb);
}

bool rb_push(ring_buffer_t *rb, uint8_t b)
{
    uint32_t head = rb->head;
    if ((head - rb->tail) > rb->mask) {
        return false;
    }
    rb->buf[head & rb->mask] = b;
    rb->head = head + 1u; /* データを書いてから添字を進める */
    return true;
}

bool rb_pop(ring_buffer_t *rb, uint8_t *out)
{
    uint32_t tail = rb->tail;
    if (rb->head == tail) {
        return false;
    }
    *out = rb->buf[tail & rb->mask];
    rb->tail = tail + 1u;
    return true;
}
