/**
 * @file bsp_internal.h
 * @brief bsp_stm32f4 の内部の関数（bsp_init が順に呼ぶ）。ports の外には見せない。
 */
#ifndef BSP_STM32F4_BSP_INTERNAL_H
#define BSP_STM32F4_BSP_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ports.h"
#include "stm32f4xx.h"

/* 割り込みの優先度（ADR-0005。優先度グループは全ビットをプリエンプション、小さいほど優先） */
#define BSP_PRIO_SYSTICK 1U
#define BSP_PRIO_DMA2_STREAM0 2U
#define BSP_PRIO_USART2 3U

/** 割り込みを禁止し、それまでの PRIMASK を返す（複数ワードの読み出しを守る短い禁止区間）。 */
static inline uint32_t bsp_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/** bsp_irq_save の前の状態に戻す。 */
static inline void bsp_irq_restore(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/* bsp_clock.c：84MHz、SysTick 1ms、DWT、優先度グループ */
void bsp_clock_init(void);
extern const tick_port_t g_bsp_tick;

/* bsp_io.c：LED・ブザー・ボタン・ファンの PWM（TIM3） */
void bsp_io_init(void);
extern const pwm_port_t g_bsp_pwm;
extern const gpio_port_t g_bsp_gpio;

/* bsp_reset.c：リセット要因（RCC_CSR）と回数（.noinit）、IWDG */
void bsp_reset_init(void);
uint32_t bsp_reset_csr(void); /* 起動時に読んだ RCC_CSR（消す前の値。entropy が使う） */
void bsp_iwdg_start(void);
extern const reset_port_t g_bsp_reset;
extern const wdt_port_t g_bsp_wdt;

/* bsp_adc.c：boot_id の材料（ソフトウェア開始）と、TIM2→ADC1→DMA2 の取得 */
void bsp_entropy_collect(void);
void bsp_adc_start(void);
extern const adc_port_t g_bsp_adc;
extern const entropy_port_t g_bsp_entropy;

/* bsp_uart.c：USART2 と受信・送信のリング */
void bsp_uart_init(void);
extern const uart_port_t g_bsp_uart;
void bsp_uart_counters(uint16_t *ring_dropped, uint16_t *errors);

/* bsp_stack.c：スタックの塗りと最大使用量 */
void bsp_stack_paint(void);
uint16_t bsp_stack_peak(void);

#endif /* BSP_STM32F4_BSP_INTERNAL_H */
