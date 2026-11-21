#include "cooling_policy.h"

#include <stdint.h>

#include "safety_fsm.h"

#define AUTO_START_C100 3500
#define AUTO_FULL_C100 4500
#define AUTO_MAX_PM 400

// cppcheck-suppress misra-c2012-8.7 ; ADR-0002 D-11（単体テストから直接呼ぶ公開関数）
uint16_t cooling_auto(int16_t t_c100)
{
    if (t_c100 < AUTO_START_C100) {
        return 0U;
    }
    if (t_c100 >= AUTO_FULL_C100) {
        return (uint16_t)AUTO_MAX_PM;
    }
    int32_t pm = ((int32_t)t_c100 - AUTO_START_C100) * AUTO_MAX_PM / 1000;
    return (uint16_t)pm;
}

// cppcheck-suppress misra-c2012-8.7 ; ADR-0002 D-12（単体テストから直接呼ぶ公開関数）
uint16_t cooling_floor(fsm_state_t s)
{
    switch (s) {
    case ST_NORMAL:
        return 0U;
    case ST_WARNING:
        return 700U;
    case ST_INIT:
    case ST_CRITICAL:
    case ST_SENSOR_FAULT:
    default:
        return (uint16_t)DUTY_FULL_PM;
    }
}

/* 取り違えの検出：s と t_c100 の取り違えは test_cooling_policy の test_auto_vs_floor で落ちる（open-questions #41） */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
uint16_t cooling_duty(fsm_state_t s, int16_t t_c100, bool manual, uint16_t manual_pm, bool force_full)
{
    uint16_t duty = manual ? manual_pm : cooling_auto(t_c100);
    uint16_t floor = cooling_floor(s);
    if (floor > duty) {
        duty = floor;
    }
    if (force_full) {
        duty = (uint16_t)DUTY_FULL_PM;
    }
    return duty;
}
