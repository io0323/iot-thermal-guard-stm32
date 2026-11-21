/**
 * @file fake_ports.h
 * @brief 単体テスト用の手書きのフェイク（ports は関数ポインタの構造体なので CMock は使わない）。
 */
#ifndef TESTS_FAKE_PORTS_H
#define TESTS_FAKE_PORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ports.h"

#define FAKE_UART_CAP 4096u

/** フェイクの状態。テストから直接読み書きする。 */
typedef struct {
    uint32_t now_ms;
    uint32_t cycles;
    uint32_t cycles_step; /**< cycles() を呼ぶたびに進める量 */
    uint16_t adc_raw;
    bool adc_ready;
    uint32_t adc_restarts; /**< adc->restart() を呼んだ回数 */
    uint16_t pwm_pm;
    uint32_t pwm_writes;
    uint8_t rx[FAKE_UART_CAP];
    size_t rx_len, rx_pos;
    uint8_t tx[FAKE_UART_CAP];
    size_t tx_len;
    bool tx_full; /**< true なら write を受け付けない */
    reset_reason_t reset_reason;
    uint16_t reset_count;
    uint8_t entropy[8];
    size_t entropy_len;
} fake_state_t;

extern fake_state_t g_fake;

/**
 * @brief フェイクを初期化し、ポート一式を返す。
 * @param out ポート
 */
void fake_ports_init(ports_t *out);

/**
 * @brief UART の受信に積む。
 * @param data バイト列
 * @param len  長さ
 */
void fake_uart_feed(const uint8_t *data, size_t len);

/**
 * @brief tick だけのポート（スケジューラのテスト用）。
 * @return ポート
 */
const tick_port_t *fake_tick_port(void);

#endif /* TESTS_FAKE_PORTS_H */
