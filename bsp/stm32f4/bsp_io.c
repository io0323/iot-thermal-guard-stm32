/*
 * LED・ブザー・ボタン・ファンの PWM（ADR-0005、詳細設計書 第5章）。ピンは pinmap.h。
 *
 * - ファン：TIM3_CH1（PA6）、タイマークロック 84MHz、PSC 0・ARR 3359 で 25kHz、PWM モード1。
 *   CCR = duty × 3360 ÷ 1000（duty 1000 で CCR 3360 > ARR となり、常に High）
 * - ボタン：PC13 のレベルを読むだけ（押すと Low）。割り込みは使わない（#48、ADR-0005）
 */
#include <stdbool.h>
#include <stdint.h>

#include "bsp_internal.h"
#include "pinmap.h"
#include "ports.h"
#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_tim.h"

#define PWM_ARR 3359U
#define PWM_STEPS 3360U
#define DUTY_FULL_PM 1000U

static void output_pin(GPIO_TypeDef *port, uint32_t pin)
{
    LL_GPIO_ResetOutputPin(port, pin);
    LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(port, pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port, pin, LL_GPIO_SPEED_FREQ_LOW);
    LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_NO);
}

static void pwm_set_duty(uint16_t pm)
{
    uint32_t d = (pm > DUTY_FULL_PM) ? DUTY_FULL_PM : (uint32_t)pm;
    LL_TIM_OC_SetCompareCH1(TIM3, (d * PWM_STEPS) / DUTY_FULL_PM);
}

static void pwm_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_GPIO_SetPinMode(PIN_FAN_PORT, PIN_FAN_PIN, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(PIN_FAN_PORT, PIN_FAN_PIN, PIN_FAN_AF);
    LL_GPIO_SetPinOutputType(PIN_FAN_PORT, PIN_FAN_PIN, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(PIN_FAN_PORT, PIN_FAN_PIN, LL_GPIO_SPEED_FREQ_LOW);

    LL_TIM_SetPrescaler(TIM3, 0U);
    LL_TIM_SetAutoReload(TIM3, PWM_ARR);
    LL_TIM_EnableARRPreload(TIM3);
    LL_TIM_OC_SetMode(TIM3, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_EnablePreload(TIM3, LL_TIM_CHANNEL_CH1);
    pwm_set_duty(DUTY_FULL_PM); /* 起動時はファン100%（詳細設計書 第2章の起動の順序 1） */
    LL_TIM_CC_EnableChannel(TIM3, LL_TIM_CHANNEL_CH1);
    LL_TIM_GenerateEvent_UPDATE(TIM3); /* プリロードを反映してから動かす */
    LL_TIM_EnableCounter(TIM3);
}

void bsp_io_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA | LL_AHB1_GRP1_PERIPH_GPIOB | LL_AHB1_GRP1_PERIPH_GPIOC);
    pwm_init();
    output_pin(PIN_LED_GREEN_PORT, PIN_LED_GREEN_PIN);
    output_pin(PIN_LED_YELLOW_PORT, PIN_LED_YELLOW_PIN);
    output_pin(PIN_LED_RED_PORT, PIN_LED_RED_PIN);
    output_pin(PIN_BUZZER_PORT, PIN_BUZZER_PIN);
    LL_GPIO_SetPinMode(PIN_BUTTON_PORT, PIN_BUTTON_PIN, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(PIN_BUTTON_PORT, PIN_BUTTON_PIN, LL_GPIO_PULL_NO);
}

static void write_pin(GPIO_TypeDef *port, uint32_t pin, bool on)
{
    if (on) {
        LL_GPIO_SetOutputPin(port, pin);
    } else {
        LL_GPIO_ResetOutputPin(port, pin);
    }
}

static void gpio_set_led(led_t led, bool on)
{
    switch (led) {
    case LED_GREEN:
        write_pin(PIN_LED_GREEN_PORT, PIN_LED_GREEN_PIN, on);
        break;
    case LED_YELLOW:
        write_pin(PIN_LED_YELLOW_PORT, PIN_LED_YELLOW_PIN, on);
        break;
    case LED_RED:
        write_pin(PIN_LED_RED_PORT, PIN_LED_RED_PIN, on);
        break;
    default:
        break;
    }
}

static void gpio_set_buzzer(bool on)
{
    write_pin(PIN_BUZZER_PORT, PIN_BUZZER_PIN, on);
}

static bool gpio_button_pressed(void)
{
    return LL_GPIO_IsInputPinSet(PIN_BUTTON_PORT, PIN_BUTTON_PIN) == 0U; /* 押すと Low */
}

const pwm_port_t g_bsp_pwm = {pwm_set_duty};
const gpio_port_t g_bsp_gpio = {gpio_set_led, gpio_set_buzzer, gpio_button_pressed};
