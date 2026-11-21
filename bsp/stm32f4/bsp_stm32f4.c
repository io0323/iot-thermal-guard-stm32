/*
 * bsp_stm32f4：ports の実装（詳細設計書 第2章の ports.h、ADR-0005）。
 *
 * 初期化の順序：
 *   1. クロック（84MHz）・SysTick・DWT
 *   2. GPIO とファンの PWM（ファンはこの時点で100%）
 *   3. リセット要因（RCC_CSR を読んで RMVF で消す）と回数（.noinit）
 *   4. boot_id の材料（ADC をソフトウェア開始で使う。TIM2 を動かす前）
 *   5. 温度の取得（TIM2 → ADC1 → DMA2）
 *   6. USART2
 *   7. IWDG（いったん動かすと止められないので最後。以降は firmware が INIT 中も含めてリフレッシュする）
 */
#include "ports.h"

#include <stdint.h>

#include "bsp_internal.h"

/* スタートアップの __libc_init_array が呼ぶ。-nostartfiles で crti.o を使わないので空のものを置く（C の初期化は .init_array だけ） */
void _init(void);
void _init(void)
{
}

static void bsp_diag_read(bsp_diag_t *out)
{
    bsp_uart_counters(&out->uart_ring_dropped, &out->uart_errors);
    out->stack_peak = bsp_stack_peak();
}

static const bsp_diag_port_t s_bsp_diag = {bsp_diag_read};

void bsp_init(ports_t *out)
{
    bsp_clock_init();
    bsp_io_init();
    bsp_reset_init();
    bsp_entropy_collect();
    bsp_adc_start();
    bsp_uart_init();
    bsp_iwdg_start();

    out->tick = &g_bsp_tick;
    out->adc = &g_bsp_adc;
    out->pwm = &g_bsp_pwm;
    out->gpio = &g_bsp_gpio;
    out->uart = &g_bsp_uart;
    out->wdt = &g_bsp_wdt;
    out->reset = &g_bsp_reset;
    out->entropy = &g_bsp_entropy;
    out->bsp_diag = &s_bsp_diag;
}
