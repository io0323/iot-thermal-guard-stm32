/* button：チャタリング除去、短押し・長押しの境界、長押しのあとの離し、キューが満杯（第5章、#36） */
#include "button.h"
#include "unity.h"

static button_t B;
static uint32_t T;

void setUp(void)
{
    button_init(&B);
    T = 0u;
}
void tearDown(void) {}

/* level を ms の間（10ms ごとに）渡す */
static void hold(bool level, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i += 10u) {
        button_update(&B, level, T);
        T += 10u;
    }
}

static int pop_all(button_event_t *evs, int cap)
{
    int n = 0;
    button_event_t ev;
    while (n < cap && button_pop(&B, &ev)) {
        evs[n++] = ev;
    }
    return n;
}

static void test_single_glitch_ignored(void)
{
    hold(false, 100);
    hold(true, 10); /* 1回だけ（20ms 未満） */
    hold(false, 100);
    TEST_ASSERT_FALSE(B.pressed);
    button_event_t ev;
    TEST_ASSERT_FALSE(button_pop(&B, &ev));
}

static void test_glitch_during_press_does_not_release(void)
{
    hold(true, 500);
    hold(false, 10); /* 押している間の1回の揺れ */
    hold(true, 500);
    TEST_ASSERT_TRUE(B.pressed);
    button_event_t ev;
    TEST_ASSERT_FALSE(button_pop(&B, &ev));
}

static void test_two_reads_confirm(void)
{
    hold(false, 30);
    button_update(&B, true, T);
    TEST_ASSERT_FALSE(B.pressed);
    T += 10u;
    button_update(&B, true, T);
    TEST_ASSERT_TRUE(B.pressed); /* 2回目（20ms）で確定 */
}

static void test_short_press_2990(void)
{
    hold(false, 30);
    hold(true, 2990);
    hold(false, 30);
    button_event_t evs[4];
    TEST_ASSERT_EQUAL_INT(1, pop_all(evs, 4));
    TEST_ASSERT_EQUAL(BTN_EV_SHORT, evs[0]);
}

static void test_long_press_3000_on_reaching(void)
{
    hold(false, 30);
    hold(true, 20);           /* 確定は2回目の読み */
    uint32_t press_ms = T - 10u;
    while ((T - press_ms) < BUTTON_LONG_MS) {
        hold(true, 10);
        button_event_t ev;
        TEST_ASSERT_FALSE(button_pop(&B, &ev)); /* 3000ms に達するまでは出ない */
    }
    button_update(&B, true, T); /* ちょうど3000ms：離すのを待たずに出る */
    button_event_t ev;
    TEST_ASSERT_TRUE(button_pop(&B, &ev));
    TEST_ASSERT_EQUAL(BTN_EV_LONG, ev);
}

static void test_exactly_3000_then_release_is_long_only(void)
{
    hold(false, 30);
    hold(true, 3000);
    hold(false, 30);
    button_event_t evs[4];
    TEST_ASSERT_EQUAL_INT(1, pop_all(evs, 4));
    TEST_ASSERT_EQUAL(BTN_EV_LONG, evs[0]);
}

static void test_release_after_long_is_not_short(void)
{
    hold(true, 6000); /* 長押しは1回だけ */
    hold(false, 100);
    button_event_t evs[4];
    TEST_ASSERT_EQUAL_INT(1, pop_all(evs, 4));
    TEST_ASSERT_EQUAL(BTN_EV_LONG, evs[0]);
}

static void test_queue_full_drops_newest(void)
{
    for (int i = 0; i < 5; i++) {
        hold(true, 100);
        hold(false, 100);
    }
    hold(true, 3000); /* 6件目は長押し */
    hold(false, 100);
    button_event_t evs[8];
    TEST_ASSERT_EQUAL_INT(4, pop_all(evs, 8));
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL(BTN_EV_SHORT, evs[i]); /* 古い4件が残り、新しいものを捨てる */
    }
    hold(true, 100);
    hold(false, 100);
    TEST_ASSERT_EQUAL_INT(1, pop_all(evs, 8)); /* 取り出せば、また入る */
}

static void test_time_wraps(void)
{
    T = 0xFFFFFF00u; /* 押している間に uptime が一周する */
    hold(false, 30);
    hold(true, 3020); /* 確定（2回目の読み）から 3000ms の読みまで */
    button_event_t ev;
    TEST_ASSERT_TRUE(button_pop(&B, &ev));
    TEST_ASSERT_EQUAL(BTN_EV_LONG, ev);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_glitch_ignored);
    RUN_TEST(test_glitch_during_press_does_not_release);
    RUN_TEST(test_two_reads_confirm);
    RUN_TEST(test_short_press_2990);
    RUN_TEST(test_long_press_3000_on_reaching);
    RUN_TEST(test_exactly_3000_then_release_is_long_only);
    RUN_TEST(test_release_after_long_is_not_short);
    RUN_TEST(test_queue_full_drops_newest);
    RUN_TEST(test_time_wraps);
    return UNITY_END();
}
