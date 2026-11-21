/**
 * @file bsp_host.h
 * @brief ホスト版の bsp（詳細設計書 第8章）。実機の割り込みにあたる処理をメインループの決まった位置で呼ぶ。
 */
#ifndef BSP_HOST_H
#define BSP_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "ports.h"
#include "scenario.h"

/** bsp_host_pump の結果。 */
typedef enum { HOST_IO_OK, HOST_IO_EOF, HOST_IO_ERROR } host_io_t;

/**
 * @brief シリアルを raw・115200・8N1・ノンブロッキングで開く（pty でボーレートの設定が失敗しても続行）。
 * @param path パス
 * @return 開けたら true
 */
bool bsp_host_open_uart(const char *path);

/**
 * @brief ADC に渡す生値を温度から決める（ntc_model_raw、open-questions #20）。
 * @param temp_c 温度（℃）
 */
void bsp_host_set_temp(double temp_c);

#define HOST_WDT_TIMEOUT_MS 500u /**< 疑似ウォッチドッグ：最後のリフレッシュからこの時間で再起動 */

/**
 * @brief シナリオを再生する（--temp より優先）。
 *
 * シナリオの時刻は「環境変数 MCU_C_01_SCENARIO_ELAPSED_MS（なければ 0）＋起動からの経過」。疑似ウォッチドッグで
 * 起動し直しても続くので、hang は1回だけ起きる（open-questions #35）。uptime_ms は再起動で 0 から数え直す。
 * @param sc シナリオ（再生中は有効なままにしておく）
 */
void bsp_host_set_scenario(const scenario_t *sc);

/**
 * @brief シナリオの event を今の時刻まで当て、hang に達したかを返す。
 * @return hang に達していれば true（ファームウェアのループを止める）
 */
bool bsp_host_scenario_hang(void);

/**
 * @brief 疑似ウォッチドッグが切れたか（最後のリフレッシュから500ms）。
 * @return 切れていれば true
 */
bool bsp_host_wdt_expired(void);

/**
 * @brief 疑似ウォッチドッグのリセット：MCU_C_01_RESET_REASON=watchdog、MCU_C_01_RESET_COUNT=回数＋1、
 *        MCU_C_01_SCENARIO_ELAPSED_MS を付けて、同じ引数で自分自身を execv() する。
 *        UART とログは O_CLOEXEC で開いているので、次のプロセスが開き直す。
 * @param argv main の引数
 * @return 戻らない。execv が失敗したときだけ戻る
 */
void bsp_host_watchdog_reset(char *const argv[]);

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
