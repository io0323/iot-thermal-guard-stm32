#include "button.h"

static void push(button_t *b, button_event_t ev)
{
    if (b->count >= BUTTON_QUEUE_LEN) {
        return; /* 満杯なら新しいイベントを捨てる（#36） */
    }
    uint8_t tail = (uint8_t)((b->head + b->count) % BUTTON_QUEUE_LEN);
    b->q[tail] = ev;
    b->count++;
}

void button_init(button_t *b)
{
    b->pressed = false;
    b->differ = 0u;
    b->press_ms = 0u;
    b->long_fired = false;
    b->head = 0u;
    b->count = 0u;
}

void button_update(button_t *b, bool pressed, uint32_t now)
{
    /* 長押しは、このサンプルで離しが確定する前に判定する（ちょうど3000ms 押して離したら長押し） */
    if (b->pressed && !b->long_fired && (int32_t)(now - (b->press_ms + BUTTON_LONG_MS)) >= 0) {
        b->long_fired = true;
        push(b, BTN_EV_LONG);
    }
    if (pressed == b->pressed) {
        b->differ = 0u;
        return;
    }
    b->differ++;
    if (b->differ < BUTTON_DEBOUNCE_READS) {
        return;
    }
    b->differ = 0u;
    b->pressed = pressed;
    if (pressed) {
        b->press_ms = now;
        b->long_fired = false;
    } else if (!b->long_fired) {
        push(b, BTN_EV_SHORT); /* 長押しを出していなければ3000ms 未満 */
    } else {
        /* 長押しのあとの離しは何も出さない */
    }
}

bool button_pop(button_t *b, button_event_t *ev)
{
    if (b->count == 0u) {
        return false;
    }
    *ev = b->q[b->head];
    b->head = (uint8_t)((b->head + 1u) % BUTTON_QUEUE_LEN);
    b->count--;
    return true;
}
