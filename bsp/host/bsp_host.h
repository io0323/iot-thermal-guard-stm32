/**
 * @file bsp_host.h
 * @brief ホスト版の bsp（詳細設計書 第8章）。実機の割り込みにあたる処理をメインループの決まった位置で呼ぶ。
 */
#ifndef BSP_HOST_H
#define BSP_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "ports.h"

/** bsp_host_pump の結果。 */
typedef enum { HOST_IO_OK, HOST_IO_EOF, HOST_IO_ERROR } host_io_t;

/**
 * @brief シリアルを raw・115200・8N1・ノンブロッキングで開く（pty でボーレートの設定が失敗しても続行）。
 * @param path パス
 * @return 開けたら true
 */
bool bsp_host_open_uart(const char *path);

/**
 * @brief ADC に渡す生値を温度から決める（NTC 10kΩ・B=3435、10kΩ プルアップ、12ビット）。
 * @param temp_c 温度（℃）
 */
void bsp_host_set_temp(double temp_c);

/**
 * @brief ADC に渡している生値。
 * @return 生値
 */
uint16_t bsp_host_adc_raw(void);

/**
 * @brief UART の入出力を進める（受信をリングに入れ、送信バッファを書き出す）。最大 timeout_ms 待つ。
 * @param timeout_ms poll の待ち時間
 * @return 相手が閉じたら HOST_IO_EOF
 */
host_io_t bsp_host_pump(int timeout_ms);

/**
 * @brief シリアルを閉じる。
 */
void bsp_host_close(void);

#endif /* BSP_HOST_H */
