/*
 * 【仮版（P1）】temp_sensor
 *
 * ADC の値は毎回受け取るが使わず、温度は 25.00℃（2500）固定・quality 0（good）を返す。
 * 12サンプル目で ready にする（本版のメディアン5点＋移動平均8点が満たされる時点と同じ）。
 * 変換表・メディアン・変化率・移動平均・故障判定・ADC 無応答の検出は P2 の本版で実装する。
 */
#include "temp_sensor.h"

#define TEMP_STUB_C100 2500

static uint8_t s_samples;
static uint8_t s_valid_run;

void temp_init(void)
{
    s_samples = 0u;
    s_valid_run = 0u;
}

void temp_update(uint16_t raw, uint32_t now_ms)
{
    (void)raw;    /* 仮版：値は受け取るが使わない */
    (void)now_ms;
    if (s_samples < TEMP_READY_SAMPLES) {
        s_samples++;
    }
    if (s_samples >= TEMP_READY_SAMPLES && s_valid_run < 255u) {
        s_valid_run++;
    }
}

void temp_tick(uint32_t now_ms)
{
    (void)now_ms; /* 仮版：無応答の検出は P2 */
}

void temp_get(temp_reading_t *out)
{
    out->temp_c100 = TEMP_STUB_C100;
    out->quality = TEMP_QUALITY_GOOD;
    out->ready = (s_samples >= TEMP_READY_SAMPLES);
    out->fault = TEMP_FAULT_NONE;
    out->valid_run = s_valid_run;
}
