/**
 * @file ports.h
 * @brief ハードウェアのポート（関数ポインタの構造体だけ。実装は bsp が持つ）。詳細設計書 第2章。
 *
 * app・service はこのヘッダだけを通してハードに触れる。P1 で使わないポート（gpio・wdt など）も定義する。
 */
#ifndef PORTS_PORTS_H
#define PORTS_PORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 時間。now_ms は起動からのミリ秒（u32 で一周する）。cycles は実機は DWT、ホストは ns。 */
typedef struct {
    uint32_t (*now_ms)(void);
    uint32_t (*cycles)(void);
} tick_port_t;

/** ADC。100ms ごとに1値。take_sample は新しい値があれば true。 */
typedef struct {
    bool (*take_sample)(uint16_t *raw);
    void (*restart)(void);
} adc_port_t;

/** ファンの PWM。0–1000 ‰。 */
typedef struct {
    void (*set_duty_pm)(uint16_t pm);
} pwm_port_t;

/** LED の色。 */
typedef enum { LED_GREEN, LED_YELLOW, LED_RED } led_t;

/** LED・ブザー・ボタン。 */
typedef struct {
    void (*set_led)(led_t led, bool on);
    void (*set_buzzer)(bool on);
    bool (*button_pressed)(void);
} gpio_port_t;

/** UART。read は受信済みのバイトを cap まで返す。write は len バイトを受け取れたら len、受け取れなければ 0。 */
typedef struct {
    size_t (*read)(uint8_t *buf, size_t cap);
    size_t (*write)(const uint8_t *buf, size_t len);
} uart_port_t;

/** ウォッチドッグ。 */
typedef struct {
    void (*refresh)(void);
} wdt_port_t;

/** リセット要因。CR-2 の値と同じ順。 */
typedef enum {
    RESET_UNKNOWN,
    RESET_POWER_ON,
    RESET_PIN,
    RESET_WATCHDOG,
    RESET_SOFTWARE,
    RESET_BROWNOUT,
    RESET_LOW_POWER
} reset_reason_t;

/** リセット要因と回数。 */
typedef struct {
    reset_reason_t (*reason)(void);
    uint16_t (*count)(void);
    void (*system_reset)(void);
} reset_port_t;

/** boot_id の種。gather は集めたバイト数を返す。 */
typedef struct {
    size_t (*gather)(uint8_t *buf, size_t cap);
} entropy_port_t;

/** ポート一式。 */
typedef struct {
    const tick_port_t *tick;
    const adc_port_t *adc;
    const pwm_port_t *pwm;
    const gpio_port_t *gpio;
    const uart_port_t *uart;
    const wdt_port_t *wdt;
    const reset_port_t *reset;
    const entropy_port_t *entropy;
} ports_t;

/**
 * @brief ポートを用意する。ファンはこの時点で100%にする。bsp_host と bsp_stm32f4 がそれぞれ実装する。
 * @param out 用意したポート
 */
void bsp_init(ports_t *out);

#endif /* PORTS_PORTS_H */
