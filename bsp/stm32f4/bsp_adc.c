/*
 * 温度の取得（TIM2 → ADC1 → DMA2）と boot_id の材料（詳細設計書 第3章・第7章、ADR-0005、#44）。
 *
 * - 取得：TIM2 の TRGO（PSC 839・ARR 999 で 100Hz）で ADC1_IN0（PA0）を変換し、DMA2 Stream0 Channel0 で
 *   循環バッファ20語に転送する。半分転送・全転送の割り込み（どちらも100ms ごと）は「確定した半分」の番号を
 *   残すだけで、平均（(sum + 5) / 10、四捨五入）は take_sample（タスク側）で行う
 * - ADC：ADCCLK = PCLK2 ÷ 4 = 21MHz（上限 36MHz）、12ビット、サンプリング 480 サイクル（約23μs）
 * - 材料：TIM2 を動かす前に、ADC をソフトウェア開始で使い、内部温度（ADC1_IN18）と VREFINT（ADC1_IN17）を
 *   32回ずつ読んだ値の下位4ビット、DWT のサイクル数、UID（96ビット）、RCC_CSR を集めておく
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp_internal.h"
#include "pinmap.h"
#include "ports.h"
#include "stm32f4xx.h"
#include "stm32f4xx_ll_adc.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_tim.h"

#define TIM2_PSC 839U
#define TIM2_ARR 999U
#define DMA_WORDS 20U
#define HALF_WORDS 10U
#define HALF_NONE 0xFFU
#define ENTROPY_READS 32U
#define TS_START_CYCLES 840U /* 内部温度センサーの起動待ち（10μs）を 84MHz のサイクル数で */

#define ENTROPY_BYTES ((2U * (ENTROPY_READS / 2U)) + 4U + 12U + 4U)

/* DMA が書く。ISR が確定させた半分は、次の100ms まで DMA に上書きされない */
static volatile uint16_t s_dma_buf[DMA_WORDS];
/* ISR が書き、take_sample が読んで消す（0 か 1、なければ HALF_NONE） */
static volatile uint8_t s_done_half = HALF_NONE;

static uint8_t s_entropy[ENTROPY_BYTES];
static size_t s_entropy_len;

/* ---- ADC の共通設定 ---- */
static void adc_common_init(void)
{
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC1);
    LL_ADC_SetCommonClock(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_CLOCK_SYNC_PCLK_DIV4);
    LL_ADC_SetResolution(ADC1, LL_ADC_RESOLUTION_12B);
    LL_ADC_SetDataAlignment(ADC1, LL_ADC_DATA_ALIGN_RIGHT);
    LL_ADC_SetSequencersScanMode(ADC1, LL_ADC_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetSequencerLength(ADC1, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetContinuousMode(ADC1, LL_ADC_REG_CONV_SINGLE);
}

/* ---- boot_id の材料（ソフトウェア開始） ---- */
static uint16_t read_once(uint32_t channel)
{
    LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, channel);
    LL_ADC_REG_StartConversionSWStart(ADC1);
    while (LL_ADC_IsActiveFlag_EOCS(ADC1) == 0U) {
    }
    return LL_ADC_REG_ReadConversionData12(ADC1); /* DR を読むと EOC が消える */
}

/* 32回の下位4ビットを、2回分ずつ1バイトに詰める */
static size_t read_nibbles(uint32_t channel, uint8_t *out)
{
    for (uint32_t i = 0U; i < (ENTROPY_READS / 2U); i++) {
        uint32_t lo = (uint32_t)read_once(channel) & 0x0FU;
        uint32_t hi = (uint32_t)read_once(channel) & 0x0FU;
        out[i] = (uint8_t)((hi << 4U) | lo);
    }
    return ENTROPY_READS / 2U;
}

static size_t put_u32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFFU);
    out[1] = (uint8_t)((v >> 8U) & 0xFFU);
    out[2] = (uint8_t)((v >> 16U) & 0xFFU);
    out[3] = (uint8_t)((v >> 24U) & 0xFFU);
    return 4U;
}

void bsp_entropy_collect(void)
{
    adc_common_init();
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_TEMPSENSOR, LL_ADC_SAMPLINGTIME_480CYCLES);
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_VREFINT, LL_ADC_SAMPLINGTIME_480CYCLES);
    LL_ADC_REG_SetTriggerSource(ADC1, LL_ADC_REG_TRIG_SOFTWARE);
    LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_TEMPSENSOR);
    LL_ADC_Enable(ADC1);
    uint32_t t0 = DWT->CYCCNT;
    while ((DWT->CYCCNT - t0) < TS_START_CYCLES) {
    }

    size_t n = 0U;
    n += read_nibbles(LL_ADC_CHANNEL_TEMPSENSOR, &s_entropy[n]);
    n += read_nibbles(LL_ADC_CHANNEL_VREFINT, &s_entropy[n]);
    n += put_u32(&s_entropy[n], DWT->CYCCNT);
    n += put_u32(&s_entropy[n], READ_REG(*((const volatile uint32_t *)UID_BASE)));
    n += put_u32(&s_entropy[n], READ_REG(*((const volatile uint32_t *)(UID_BASE + 4U))));
    n += put_u32(&s_entropy[n], READ_REG(*((const volatile uint32_t *)(UID_BASE + 8U))));
    n += put_u32(&s_entropy[n], bsp_reset_csr());
    s_entropy_len = n;

    LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_NONE);
    LL_ADC_Disable(ADC1);
}

static size_t entropy_gather(uint8_t *buf, size_t cap)
{
    size_t n = (s_entropy_len < cap) ? s_entropy_len : cap;
    (void)memcpy(buf, s_entropy, n);
    return n;
}

/* ---- 取得（TIM2 → ADC1 → DMA2） ---- */
static void dma_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
    LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_0, LL_DMA_CHANNEL_0);
    LL_DMA_SetDataTransferDirection(DMA2, LL_DMA_STREAM_0, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
    LL_DMA_SetMode(DMA2, LL_DMA_STREAM_0, LL_DMA_MODE_CIRCULAR);
    LL_DMA_SetPeriphIncMode(DMA2, LL_DMA_STREAM_0, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(DMA2, LL_DMA_STREAM_0, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(DMA2, LL_DMA_STREAM_0, LL_DMA_PDATAALIGN_HALFWORD);
    LL_DMA_SetMemorySize(DMA2, LL_DMA_STREAM_0, LL_DMA_MDATAALIGN_HALFWORD);
    LL_DMA_SetPeriphAddress(DMA2, LL_DMA_STREAM_0, LL_ADC_DMA_GetRegAddr(ADC1, LL_ADC_DMA_REG_REGULAR_DATA));
    LL_DMA_SetMemoryAddress(DMA2, LL_DMA_STREAM_0, (uint32_t)&s_dma_buf[0]);
    LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_0, DMA_WORDS);
    LL_DMA_EnableIT_HT(DMA2, LL_DMA_STREAM_0);
    LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_0);
    LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_0);
    NVIC_SetPriority(DMA2_Stream0_IRQn, BSP_PRIO_DMA2_STREAM0);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_0);
}

static void adc_stream_init(void)
{
    adc_common_init();
    LL_GPIO_SetPinMode(PIN_TEMP_PORT, PIN_TEMP_PIN, LL_GPIO_MODE_ANALOG);
    LL_ADC_SetChannelSamplingTime(ADC1, PIN_TEMP_ADC_CHANNEL, LL_ADC_SAMPLINGTIME_480CYCLES);
    LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, PIN_TEMP_ADC_CHANNEL);
    LL_ADC_REG_SetTriggerSource(ADC1, LL_ADC_REG_TRIG_EXT_TIM2_TRGO);
    LL_ADC_REG_SetDMATransfer(ADC1, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
    LL_ADC_ClearFlag_OVR(ADC1);
    LL_ADC_Enable(ADC1);
    LL_ADC_REG_StartConversionExtTrig(ADC1, LL_ADC_REG_TRIG_EXT_RISING);
}

static void tim2_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_TIM_SetPrescaler(TIM2, TIM2_PSC);
    LL_TIM_SetAutoReload(TIM2, TIM2_ARR);
    LL_TIM_SetTriggerOutput(TIM2, LL_TIM_TRGO_UPDATE);
    LL_TIM_GenerateEvent_UPDATE(TIM2);
    LL_TIM_EnableCounter(TIM2);
}

void bsp_adc_start(void)
{
    s_done_half = HALF_NONE;
    dma_init();
    adc_stream_init();
    tim2_init();
}

/* ADC が止まったとき（無応答、temp_sensor が1秒ごとに呼ぶ）：TIM2・ADC・DMA を止めて初めから動かし直す */
static void adc_restart(void)
{
    NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    LL_TIM_DisableCounter(TIM2);
    LL_ADC_Disable(ADC1);
    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_0);
    while (LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_0) != 0U) {
    }
    LL_DMA_ClearFlag_HT0(DMA2);
    LL_DMA_ClearFlag_TC0(DMA2);
    LL_DMA_ClearFlag_TE0(DMA2);
    LL_DMA_ClearFlag_DME0(DMA2);
    LL_DMA_ClearFlag_FE0(DMA2);
    bsp_adc_start();
}

/* ISR：確定した半分の番号を残し、フラグを消すだけ（平均はタスク側） */
void DMA2_Stream0_IRQHandler(void);
void DMA2_Stream0_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_HT0(DMA2) != 0U) {
        LL_DMA_ClearFlag_HT0(DMA2);
        s_done_half = 0U;
    }
    if (LL_DMA_IsActiveFlag_TC0(DMA2) != 0U) {
        LL_DMA_ClearFlag_TC0(DMA2);
        s_done_half = 1U;
    }
    if (LL_DMA_IsActiveFlag_TE0(DMA2) != 0U) {
        LL_DMA_ClearFlag_TE0(DMA2); /* ストリームは止まる。値が来なくなり、300ms で temp_sensor が restart() を呼ぶ */
    }
}

/* 100ms ごとに task_sense が呼ぶ。確定した半分の10語を四捨五入で平均する */
static bool adc_take_sample(uint16_t *raw)
{
    uint32_t primask = bsp_irq_save();
    uint8_t half = s_done_half;
    s_done_half = HALF_NONE;
    bsp_irq_restore(primask);
    if (half == HALF_NONE) {
        return false;
    }
    uint32_t sum = 0U;
    uint32_t base = (uint32_t)half * HALF_WORDS;
    for (uint32_t i = 0U; i < HALF_WORDS; i++) {
        sum += (uint32_t)s_dma_buf[base + i];
    }
    *raw = (uint16_t)((sum + (HALF_WORDS / 2U)) / HALF_WORDS);
    return true;
}

const adc_port_t g_bsp_adc = {adc_take_sample, adc_restart};
const entropy_port_t g_bsp_entropy = {entropy_gather};
