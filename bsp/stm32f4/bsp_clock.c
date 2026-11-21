/*
 * クロック・SysTick・DWT（ADR-0005）。
 *
 * - HSI 16MHz → PLL（M=16、N=336、P=4、Q=7）で SYSCLK 84MHz。AHB 84MHz、APB1 42MHz（タイマーは 84MHz）、APB2 84MHz
 *   PLLQ=7 は 48MHz 系をデータシートの上限 48MHz に収めるため（#43。USB・SDIO は使わない）
 * - Flash の待ち 2WS（84MHz、2.7〜3.6V）、電圧スケール 2（84MHz まで）
 * - ST-LINK の MCO（HSE）には頼らない
 * - SysTick 1ms：now_ms。DWT の CYCCNT：cycles()
 */
#include <stdint.h>

#include "bsp_internal.h"
#include "ports.h"
#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"

#define SYSCLK_HZ 84000000U
#define SYSTICK_HZ 1000U
#define PLL_M LL_RCC_PLLM_DIV_16
#define PLL_N 336U
#define PLL_P LL_RCC_PLLP_DIV_4
#define PLL_Q LL_RCC_PLLQ_DIV_7

/* SysTick の ISR だけが書く（1ワードなので読み出しは禁止区間なしで一度に読める） */
static volatile uint32_t s_ms;

static void wait_hsi_pll(void)
{
    LL_RCC_HSI_Enable();
    while (LL_RCC_HSI_IsReady() != 1U) {
    }
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, PLL_M, PLL_N, PLL_P);
    /* 48MHz 系の分周（PLLQ）。ConfigDomain_SYS は Q を書かないので直接書く */
    MODIFY_REG(RCC->PLLCFGR, RCC_PLLCFGR_PLLQ, PLL_Q);
    LL_RCC_PLL_Enable();
    while (LL_RCC_PLL_IsReady() != 1U) {
    }
}

static void switch_sysclk(void)
{
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {
    }
}

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void bsp_clock_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE2);
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_2);
    while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_2) {
    }
    LL_FLASH_EnablePrefetch();
    LL_FLASH_EnableInstCache();
    LL_FLASH_EnableDataCache();
    wait_hsi_pll();
    switch_sysclk();
    SystemCoreClock = SYSCLK_HZ;

    /* 優先度グループ：全ビット（実装は上位4ビット）をプリエンプションに、サブ優先度なし（ST の NVIC_PRIORITYGROUP_4 と同じ値） */
    NVIC_SetPriorityGrouping(3U);
    (void)SysTick_Config(SYSCLK_HZ / SYSTICK_HZ);
    NVIC_SetPriority(SysTick_IRQn, BSP_PRIO_SYSTICK);
    dwt_init();
}

/* ISR：ティックの加算だけ */
void SysTick_Handler(void);
void SysTick_Handler(void)
{
    s_ms++;
}

static uint32_t tick_now_ms(void)
{
    return s_ms;
}

static uint32_t tick_cycles(void)
{
    return DWT->CYCCNT;
}

const tick_port_t g_bsp_tick = {tick_now_ms, tick_cycles};
