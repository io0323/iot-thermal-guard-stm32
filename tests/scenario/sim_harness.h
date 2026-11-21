/**
 * @file sim_harness.h
 * @brief 仮想時間のシナリオテストの土台（詳細設計書 第9章）。
 *
 * main/firmware.c（app・service を含む）を手書きのフェイクのポートにつなぎ、1ms ずつ仮想時間を進める。
 * シナリオ CSV の温度と event で ADC の値を決め、PWM・送信フレーム・状態の時系列を記録する。
 * 上位役として、既定で 1000ms ごとに HEARTBEAT を送る（上位断が起きないように）。
 *
 * - ボタン：CSV の button_down〜button_up の間、button_pressed() が true を返す
 * - 疑似ウォッチドッグ：最後の wdt->refresh() から500ms で切れる（hang に限らず、いつでも見張る）。切れたら
 *   フェイクのポートと firmware を初期化し直して再起動を模擬する（reset_reason は watchdog、回数は＋1、
 *   entropy を変えて別の boot_id にする）。uptime_ms は 0 から数え直すが、仮想時間とシナリオの時刻は続く（#35）
 * - hang：ファームウェアのループ（firmware_poll）を止める。監視は続けるので、500ms 後に再起動する
 * - 記録の時刻はすべて仮想時間（uptime_ms ではない）
 */
#ifndef TESTS_SIM_HARNESS_H
#define TESTS_SIM_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "firmware.h"
#include "link_frame.h"
#include "safety_fsm.h"
#include "scenario.h"

#define SIM_MAX_RECORDS 4096u

/** PWM に書いた値。 */
typedef struct {
    uint32_t t_ms;
    uint16_t pm;
} sim_pwm_t;

/** 状態の変化。 */
typedef struct {
    uint32_t t_ms;
    fsm_state_t from, to;
} sim_state_t;

/** 送信したフレーム。 */
typedef struct {
    uint32_t t_ms;
    link_frame_t f;
} sim_frame_t;

/** オンとオフ（ブザー）。 */
typedef struct {
    uint32_t t_ms;
    bool on;
} sim_onoff_t;

/** 起動（firmware_init）。 */
typedef struct {
    uint32_t t_ms;
    uint32_t boot_id;
    reset_reason_t reason;
    uint16_t count;
} sim_boot_t;

#define SIM_MAX_BOOTS 8u
#define SIM_WDT_TIMEOUT_MS 500u /**< 疑似ウォッチドッグ（ホスト版と同じ） */

/** 記録。 */
typedef struct {
    sim_pwm_t pwm[SIM_MAX_RECORDS];
    size_t n_pwm;
    sim_state_t state[SIM_MAX_RECORDS];
    size_t n_state;
    sim_frame_t tx[SIM_MAX_RECORDS];
    size_t n_tx;
    sim_onoff_t buzzer[SIM_MAX_RECORDS];
    size_t n_buzzer;
    sim_boot_t boot[SIM_MAX_BOOTS];
    size_t n_boot;
} sim_log_t;

/**
 * @brief 起動する（フェイクのポートを初期化し firmware_init を呼ぶ。時刻 0 から）。
 * @param sc シナリオ（実行中は有効なままにしておく）
 */
void sim_init(const scenario_t *sc);

/**
 * @brief 上位役の HEARTBEAT を送るか（既定は送る）。
 * @param on 送るなら true
 */
void sim_set_heartbeat(bool on);

/**
 * @brief t_ms まで 1ms ずつ進める（t_ms を含む）。
 * @param t_ms 時刻
 */
void sim_run_until(uint32_t t_ms);

/**
 * @brief 上位役としてフレームを送る（次の task_link で受信される）。
 * @param f フレーム（seq は種別ごとに自動で振る）
 */
void sim_send(link_frame_t f);

/**
 * @brief いまの LED（最後に書いた値）。
 * @param led LED
 * @return 点灯なら true
 */
bool sim_led(led_t led);

/**
 * @brief いまのブザー（最後に書いた値）。
 * @return 鳴っていれば true
 */
bool sim_buzzer(void);

/**
 * @brief 次に進める時刻。
 * @return 時刻
 */
uint32_t sim_now(void);

/**
 * @brief 記録。
 * @return 記録
 */
const sim_log_t *sim_log(void);

/**
 * @brief いまの状態（最後の状態変化の行き先。変化がなければ INIT）。
 * @return 状態
 */
fsm_state_t sim_state(void);

/**
 * @brief いまの PWM の値。
 * @return duty（‰）
 */
uint16_t sim_pwm_now(void);

/**
 * @brief since 以降で最初に PWM へ pm 以上を書いた時刻。
 * @param since 時刻
 * @param pm    値
 * @return 時刻。なければ −1
 */
int64_t sim_first_pwm_at_least(uint32_t since, uint16_t pm);

/**
 * @brief 最初に to へ変化した時刻。
 * @param to 状態
 * @return 時刻。なければ −1
 */
int64_t sim_first_state(fsm_state_t to);

/**
 * @brief from 番目以降で、種別（EVENT なら code も）が合う最初の送信フレームの添字。
 * @param from 添字
 * @param type 種別
 * @param code EVENT の code（−1 なら問わない）
 * @return 添字。なければ −1
 */
int sim_find_tx(size_t from, uint8_t type, int code);

/**
 * @brief EVENT の code が合う送信フレームの数。
 * @param code code
 * @return 件数
 */
size_t sim_count_events(uint8_t code);

/**
 * @brief シナリオの入力温度が初めて temp_c 以上になる時刻（1ms 刻みで探す）。
 * @param sc     シナリオ
 * @param temp_c 温度
 * @param until  探す上限
 * @return 時刻。なければ −1
 */
int64_t sim_first_input_at_least(const scenario_t *sc, double temp_c, uint32_t until);

#endif /* TESTS_SIM_HARNESS_H */
