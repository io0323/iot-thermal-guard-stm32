/**
 * @file reset_decode.h
 * @brief RCC_CSR のリセットのフラグからリセット要因を決める（詳細設計書 第7章、open-questions #51）。
 *
 * STM32 のヘッダに依存しない純粋な関数なので、ホストの単体テスト（tests/unit/test_reset_decode.c）で確かめる。
 * ビットの位置は RM0368 の RCC_CSR と同じで、bsp_reset.c が CMSIS の定義と一致することをコンパイル時に確かめる。
 */
#ifndef BSP_STM32F4_RESET_DECODE_H
#define BSP_STM32F4_RESET_DECODE_H

#include <stdint.h>

#include "ports.h"

#define RESET_CSR_BORRSTF (1UL << 25U)  /**< BOR（電源投入でも立つ） */
#define RESET_CSR_PINRSTF (1UL << 26U)  /**< ピン（どのリセットでも立つ） */
#define RESET_CSR_PORRSTF (1UL << 27U)  /**< 電源投入（POR・PDR） */
#define RESET_CSR_SFTRSTF (1UL << 28U)  /**< ソフトウェア */
#define RESET_CSR_IWDGRSTF (1UL << 29U) /**< IWDG */
#define RESET_CSR_LPWRRSTF (1UL << 31U) /**< 低電力 */

/**
 * @brief RCC_CSR の値からリセット要因を決める。
 *
 * 優先順は IWDG → ソフトウェア → 電源投入 → BOR → 低電力 → ピン（#51）。電源投入では BORRSTF とピンも立つので
 * BOR より先に電源投入を見る。ピンはどのリセットでも立つので最後に見る。どれも立っていなければ RESET_UNKNOWN。
 * @param csr RCC_CSR の値（RMVF で消す前）
 * @return リセット要因
 */
reset_reason_t reset_decode_csr(uint32_t csr);

#endif /* BSP_STM32F4_RESET_DECODE_H */
