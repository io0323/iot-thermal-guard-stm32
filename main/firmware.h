/**
 * @file firmware.h
 * @brief タスクの登録と配線（両方の bsp で共通）。詳細設計書 第2章。
 */
#ifndef MAIN_FIRMWARE_H
#define MAIN_FIRMWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diag.h"
#include "link_frame.h"
#include "ports.h"
#include "safety_fsm.h"

#define FW_TASK_COUNT DIAG_TASK_COUNT
#define FW_INIT_MAX_MS 2000U /**< INIT の最長。これまでに ready にならなければ SENSOR_FAULT */

/** 診断値（詳細設計書 第7章の表）。中身は service/diag.h。 */
typedef diag_t fw_diag_t;

/** 観測用のフック（ホスト版のログ）。どれも NULL でよい。 */
typedef struct {
    void (*boot)(uint32_t t_ms, uint32_t boot_id, reset_reason_t reason, uint16_t count);
    void (*state)(uint32_t t_ms, fsm_state_t from, fsm_state_t to);
    void (*fan)(uint32_t t_ms, uint16_t pm);
    void (*tx)(uint32_t t_ms, const link_frame_t *f, const uint8_t *raw, size_t len);
    void (*rx)(uint32_t t_ms, const link_frame_t *f, const uint8_t *raw, size_t len);
    void (*rx_error)(uint32_t t_ms, link_rx_error_t reason, const uint8_t *raw, size_t len);
    void (*led)(uint32_t t_ms, led_t led, bool on);   /**< LED が変わったとき */
    void (*buzzer)(uint32_t t_ms, bool on);           /**< ブザーが変わったとき */
} fw_observer_t;

/**
 * @brief 起動処理（ポートは bsp_init で用意済み）。リセット要因を読み、boot_id を作り、INIT に入る。
 * @param ports ポート
 * @param obs   観測用のフック（NULL でよい）
 */
void firmware_init(const ports_t *ports, const fw_observer_t *obs);

/**
 * @brief メインループの1周（期限の来たタスクを実行する）。
 */
void firmware_poll(void);

/**
 * @brief 診断値を読む。
 * @param out 診断値
 */
void firmware_diag(fw_diag_t *out);

/**
 * @brief 強制全開中か（長押しで切り替える。再起動で解除）。
 * @return 強制全開中なら true
 */
bool firmware_force_full(void);

/**
 * @brief 現在時刻（ログ用）。
 * @return uptime_ms
 */
uint32_t firmware_now_ms(void);

#endif /* MAIN_FIRMWARE_H */
