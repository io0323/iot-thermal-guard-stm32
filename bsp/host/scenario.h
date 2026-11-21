/**
 * @file scenario.h
 * @brief シナリオ CSV（詳細設計書 第8章、open-questions #28）の読み込みと再生。ホスト版と sim_harness で共通。
 *
 * 形式：`t_ms,temp_c,event`。温度は次の行まで線形につなぎ、最後の行のあとは最後の温度のまま。
 * - 1行目が `t_ms` で始まればヘッダとして読み飛ばす。`#` で始まる行と空行は読み飛ばす。改行は LF・CRLF のどちらでもよい
 * - temp_c は全行に必須。t_ms は増える順（同じ値も不可）
 * - event は空か、sensor_open・sensor_short・sensor_ok・adc_stall・adc_resume・button_down・button_up・hang
 *   （P2 の間は button・hang を未対応にしていた。open-questions #33）
 * - hang は「ファームウェアのループを止める」。止めるのは再生する側（ホスト版の main、sim_harness）で、
 *   ここでは hang に達したかだけを持つ
 */
#ifndef BSP_HOST_SCENARIO_H
#define BSP_HOST_SCENARIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENARIO_MAX_ROWS 1024u

/** event の種類。 */
typedef enum {
    SC_EV_NONE,
    SC_EV_SENSOR_OPEN,
    SC_EV_SENSOR_SHORT,
    SC_EV_SENSOR_OK,
    SC_EV_ADC_STALL,
    SC_EV_ADC_RESUME,
    SC_EV_BUTTON_DOWN,
    SC_EV_BUTTON_UP,
    SC_EV_HANG
} sc_event_t;

/** 1行。 */
typedef struct {
    uint32_t t_ms;
    double temp_c;
    sc_event_t ev;
} sc_row_t;

/** 読み込んだシナリオ。 */
typedef struct {
    sc_row_t rows[SCENARIO_MAX_ROWS];
    size_t n;
} scenario_t;

/** 再生の状態。 */
typedef struct {
    const scenario_t *sc;
    size_t next_ev;  /**< まだ当てていない最初の行 */
    bool open;       /**< sensor_open：生値 4095 */
    bool shorted;    /**< sensor_short：生値 0 */
    bool stalled;    /**< adc_stall：値を出さない */
    bool button;     /**< button_down〜button_up：押している */
    bool hang;       /**< hang に達した */
} scenario_player_t;

/**
 * @brief 文字列からシナリオを読む。
 * @param sc     読み込み先
 * @param text   CSV の中身（NUL 終端）
 * @param err    失敗したときの理由（NULL でよい）
 * @param errlen err の大きさ
 * @return 成功なら true
 */
bool scenario_parse(scenario_t *sc, const char *text, char *err, size_t errlen);

/**
 * @brief ファイルからシナリオを読む。
 * @param sc     読み込み先
 * @param path   CSV のパス
 * @param err    失敗したときの理由（NULL でよい）
 * @param errlen err の大きさ
 * @return 成功なら true
 */
bool scenario_load(scenario_t *sc, const char *path, char *err, size_t errlen);

/**
 * @brief 時刻 t_ms の温度（行の間は線形補間）。
 * @param sc   シナリオ（1行以上）
 * @param t_ms 時刻
 * @return 温度（℃）
 */
double scenario_temp_at(const scenario_t *sc, uint32_t t_ms);

/**
 * @brief 再生を始める。
 * @param p  再生の状態
 * @param sc シナリオ
 */
void scenario_player_init(scenario_player_t *p, const scenario_t *sc);

/**
 * @brief t_ms までの event を当てる。
 * @param p    再生の状態
 * @param t_ms 時刻
 */
void scenario_player_advance(scenario_player_t *p, uint32_t t_ms);

/**
 * @brief t_ms に ADC が返す生値（先に scenario_player_advance で event を当てておく）。
 * @param p    再生の状態
 * @param t_ms 時刻
 * @param raw  生値
 * @return adc_stall 中なら false（値を出さない）
 */
bool scenario_player_sample(const scenario_player_t *p, uint32_t t_ms, uint16_t *raw);

#endif /* BSP_HOST_SCENARIO_H */
