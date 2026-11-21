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
    uint32_t (*now_ms)(void); /**< 起動からのミリ秒（u32、約49.7日で一周） */
    uint32_t (*cycles)(void); /**< サイクル数（実機は DWT、ホストはナノ秒。差だけを使う） */
} tick_port_t;

/** ADC。100ms ごとに1値。take_sample は新しい値があれば true。 */
typedef struct {
    bool (*take_sample)(uint16_t *raw); /**< ADC の値を1つ取る。値がなければ false */
    void (*restart)(void); /**< ADC を初期化し直す（無応答のとき） */
} adc_port_t;

/** ファンの PWM。0–1000 ‰。 */
typedef struct {
    void (*set_duty_pm)(uint16_t pm); /**< PWM の duty（‰、0〜1000）を書く */
} pwm_port_t;

/** LED の色。 */
typedef enum { LED_GREEN, LED_YELLOW, LED_RED } led_t;

/** LED・ブザー・ボタン。 */
typedef struct {
    void (*set_led)(led_t led, bool on); /**< LED を点灯・消灯する */
    void (*set_buzzer)(bool on); /**< ブザーを鳴らす・止める */
    bool (*button_pressed)(void); /**< ボタンが押されているか（レベル） */
} gpio_port_t;

/** UART。read は受信済みのバイトを cap まで返す。write は len バイトを受け取れたら len、受け取れなければ 0。 */
typedef struct {
    size_t (*read)(uint8_t *buf, size_t cap); /**< 受信したバイトを最大 cap だけ読む。戻り値は読んだ数 */
    size_t (*write)(const uint8_t *buf, size_t len); /**< len バイトを送る。入らなければ 0 を返す */
} uart_port_t;

/** ウォッチドッグ。 */
typedef struct {
    void (*refresh)(void); /**< ウォッチドッグをリフレッシュする */
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
    reset_reason_t (*reason)(void); /**< 起動時のリセット要因 */
    uint16_t (*count)(void); /**< リセット回数（電源投入で 0） */
    void (*system_reset)(void); /**< ソフトウェアリセット */
} reset_port_t;

/** boot_id の種。gather は集めたバイト数を返す。 */
typedef struct {
    size_t (*gather)(uint8_t *buf, size_t cap); /**< boot_id の材料を集める。戻り値は集めたバイト数 */
} entropy_port_t;

/** bsp が持つ診断値（open-questions #45〜#47）。ホスト版はすべて 0。 */
typedef struct {
    uint16_t uart_ring_dropped; /**< 受信リングが満杯で捨てたバイト数（rx_overflow とは別） */
    uint16_t uart_errors; /**< UART の受信エラー（ORE・FE・NE・PE）の件数 */
    uint16_t stack_peak; /**< スタックの最大使用量（バイト） */
} bsp_diag_t;

/** bsp の診断値の読み出し。 */
typedef struct {
    /**
     * @brief bsp の診断値を読む。
     * @param out 診断値
     */
    void (*read)(bsp_diag_t *out);
} bsp_diag_port_t;

/** ポート一式。 */
typedef struct {
    const tick_port_t *tick; /**< 時間 */
    const adc_port_t *adc; /**< ADC */
    const pwm_port_t *pwm; /**< PWM（ファン） */
    const gpio_port_t *gpio; /**< LED・ブザー・ボタン */
    const uart_port_t *uart; /**< UART */
    const wdt_port_t *wdt; /**< ウォッチドッグ */
    const reset_port_t *reset; /**< リセット要因と回数 */
    const entropy_port_t *entropy; /**< boot_id の材料 */
    const bsp_diag_port_t *bsp_diag; /**< bsp の診断値 */
} ports_t;

/**
 * @brief ポートを用意する。ファンはこの時点で100%にする。bsp_host と bsp_stm32f4 がそれぞれ実装する。
 * @param out 用意したポート
 */
void bsp_init(ports_t *out);

#endif /* PORTS_PORTS_H */
