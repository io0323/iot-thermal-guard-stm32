/**
 * @file button.h
 * @brief ボタンの判定（詳細設計書 第5章）。task_ui が10ms ごとに button_pressed() のレベルを渡す。
 *
 * - チャタリング除去：確定と違うレベルが2回続けて（20ms）読めたら確定を切り替える。1回だけの揺れは無視する
 * - 短押し：押してから3000ms 未満で離したとき（離しが確定した時点で出す）
 * - 長押し：押し続けて3000ms に達した時点（離すのを待たない）。長押しのあとに離しても短押しにしない
 * - 押した時間は「押しの確定」から「離しの確定」まで。両方とも同じだけ遅れるので、生のレベルの長さと同じになる
 * - イベントは4件のキュー。満杯なら新しいイベントを捨てる（open-questions #36）
 */
#ifndef SERVICE_BUTTON_H
#define SERVICE_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#define BUTTON_DEBOUNCE_READS 2u  /**< 同じレベルがこの回数続いたら確定 */
#define BUTTON_LONG_MS 3000u      /**< 長押しの時間 */
#define BUTTON_QUEUE_LEN 4u       /**< イベントのキューの長さ */

/** ボタンのイベント。 */
typedef enum { BTN_EV_SHORT, BTN_EV_LONG } button_event_t;

/** ボタンの状態。 */
typedef struct {
    bool pressed;     /**< 確定したレベル */
    uint8_t differ;   /**< 確定と違うレベルが続けて読めた回数 */
    uint32_t press_ms;
    bool long_fired;  /**< この押しで長押しを出したか */
    button_event_t q[BUTTON_QUEUE_LEN];
    uint8_t head;
    uint8_t count;
} button_t;

/**
 * @brief 初期化する（離した状態から始める）。
 * @param b 対象
 */
void button_init(button_t *b);

/**
 * @brief 10ms ごとにレベルを渡す。
 * @param b       対象
 * @param pressed button_pressed() の値
 * @param now     時刻
 */
void button_update(button_t *b, bool pressed, uint32_t now);

/**
 * @brief イベントを1件取り出す。
 * @param b  対象
 * @param ev 取り出したイベント
 * @return なければ false
 */
bool button_pop(button_t *b, button_event_t *ev);

#endif /* SERVICE_BUTTON_H */
