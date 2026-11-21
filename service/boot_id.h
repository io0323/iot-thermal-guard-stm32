/**
 * @file boot_id.h
 * @brief boot_id の生成（詳細設計書 第7章）。entropy_port から集めたバイト列を CRC-32 で 32 ビットにし、0 なら 1。
 */
#ifndef SERVICE_BOOT_ID_H
#define SERVICE_BOOT_ID_H

#include <stdint.h>

#include "ports.h"

#define BOOT_ID_ENTROPY_MAX 64U

/**
 * @brief boot_id を作る。
 * @param e entropy のポート（実機は ADC の雑音・DWT・UID・RCC_CSR、ホストは getentropy の4バイト）
 * @return boot_id（0 にはならない）
 */
uint32_t boot_id_make(const entropy_port_t *e);

#endif /* SERVICE_BOOT_ID_H */
