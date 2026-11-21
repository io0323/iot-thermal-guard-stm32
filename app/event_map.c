#include "event_map.h"

typedef struct {
    fsm_state_t from;
    fsm_state_t to;
    event_code_t code;
    severity_t severity;
} event_row_t;

/* 基本設計書 第8章の表を、遷移ごとの行に展開したもの */
static const event_row_t k_rows[] = {
    {ST_INIT, ST_NORMAL, EV_STATE_CHANGED, SEV_INFO},
    {ST_INIT, ST_WARNING, EV_STATE_CHANGED, SEV_WARNING},
    {ST_INIT, ST_CRITICAL, EV_STATE_CHANGED, SEV_CRITICAL},
    {ST_INIT, ST_SENSOR_FAULT, EV_SENSOR_FAULT, SEV_CRITICAL},
    {ST_NORMAL, ST_WARNING, EV_THRESHOLD_EXCEEDED, SEV_WARNING},
    {ST_NORMAL, ST_CRITICAL, EV_THRESHOLD_EXCEEDED, SEV_CRITICAL},
    {ST_WARNING, ST_CRITICAL, EV_THRESHOLD_EXCEEDED, SEV_CRITICAL},
    {ST_WARNING, ST_NORMAL, EV_THRESHOLD_CLEARED, SEV_INFO},
    {ST_CRITICAL, ST_WARNING, EV_THRESHOLD_CLEARED, SEV_INFO},
    {ST_CRITICAL, ST_NORMAL, EV_THRESHOLD_CLEARED, SEV_INFO},
    {ST_NORMAL, ST_SENSOR_FAULT, EV_SENSOR_FAULT, SEV_CRITICAL},
    {ST_WARNING, ST_SENSOR_FAULT, EV_SENSOR_FAULT, SEV_CRITICAL},
    {ST_CRITICAL, ST_SENSOR_FAULT, EV_SENSOR_FAULT, SEV_CRITICAL},
    {ST_SENSOR_FAULT, ST_NORMAL, EV_SENSOR_RECOVERED, SEV_INFO},
    {ST_SENSOR_FAULT, ST_WARNING, EV_SENSOR_RECOVERED, SEV_INFO},
    {ST_SENSOR_FAULT, ST_CRITICAL, EV_SENSOR_RECOVERED, SEV_INFO},
};

bool event_map(fsm_state_t from, fsm_state_t to, uint8_t *code, uint8_t *severity)
{
    for (uint32_t i = 0; i < (uint32_t)(sizeof k_rows / sizeof k_rows[0]); i++) {
        if (k_rows[i].from == from && k_rows[i].to == to) {
            *code = (uint8_t)k_rows[i].code;
            *severity = (uint8_t)k_rows[i].severity;
            return true;
        }
    }
    *code = (uint8_t)EV_NONE;
    *severity = (uint8_t)SEV_INFO;
    return false;
}
