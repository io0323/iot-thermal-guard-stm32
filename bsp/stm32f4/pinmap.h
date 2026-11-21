/**
 * @file pinmap.h
 * @brief ピンの割り当て（Nucleo-F401RE、すべて Arduino ヘッダに出ているピン）。ADR-0005 の表と同じ。
 *
 * ピンを変えるときはこのファイルと ADR-0005 だけを直す（詳細設計書 第5章）。
 */
#ifndef BSP_STM32F4_PINMAP_H
#define BSP_STM32F4_PINMAP_H

#include "stm32f4xx_ll_gpio.h"

/* 温度（NTC の分圧）：PA0（A0）＝ADC1_IN0 */
#define PIN_TEMP_PORT GPIOA
#define PIN_TEMP_PIN LL_GPIO_PIN_0
#define PIN_TEMP_ADC_CHANNEL LL_ADC_CHANNEL_0

/* ファン PWM：PA6（D12）＝TIM3_CH1（AF2） */
#define PIN_FAN_PORT GPIOA
#define PIN_FAN_PIN LL_GPIO_PIN_6
#define PIN_FAN_AF LL_GPIO_AF_2

/* UART：USART2、PA2＝TX・PA3＝RX（AF7、ST-LINK の仮想 COM） */
#define PIN_UART_PORT GPIOA
#define PIN_UART_TX_PIN LL_GPIO_PIN_2
#define PIN_UART_RX_PIN LL_GPIO_PIN_3
#define PIN_UART_AF LL_GPIO_AF_7

/* LED：緑 PA5（LD2、D13）、黄 PB4（D5）、赤 PB5（D4）。High で点灯 */
#define PIN_LED_GREEN_PORT GPIOA
#define PIN_LED_GREEN_PIN LL_GPIO_PIN_5
#define PIN_LED_YELLOW_PORT GPIOB
#define PIN_LED_YELLOW_PIN LL_GPIO_PIN_4
#define PIN_LED_RED_PORT GPIOB
#define PIN_LED_RED_PIN LL_GPIO_PIN_5

/* ブザー：PB10（D6）、GPIO の出力。High で鳴る */
#define PIN_BUZZER_PORT GPIOB
#define PIN_BUZZER_PIN LL_GPIO_PIN_10

/* ボタン：PC13（B1、押すと Low。基板のプルアップ）。割り込みは使わず、10ms ごとにレベルを読む（#48） */
#define PIN_BUTTON_PORT GPIOC
#define PIN_BUTTON_PIN LL_GPIO_PIN_13

#endif /* BSP_STM32F4_PINMAP_H */
