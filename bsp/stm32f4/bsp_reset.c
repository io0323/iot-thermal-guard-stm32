/*
 * リセット要因（RCC_CSR）・リセット回数（.noinit）・IWDG（詳細設計書 第7章、ADR-0005）。
 *
 * - RCC_CSR を起動時に1回読んで残し（entropy も使う）、RMVF で消す。要因の判定は reset_decode.c（#51）
 * - リセット回数は .noinit にマジック値と一緒に置く。電源投入（またはマジック値が壊れている）なら 0、
 *   それ以外は前回の値 + 1（0xFFFF で止める。ホスト版と同じ diag_next_reset_count）
 * - IWDG：LSI（公称 32kHz）を 1/32、リロード 500 で公称 500ms
 */
#include <stdint.h>

#include "bsp_internal.h"
#include "diag.h"
#include "ports.h"
#include "reset_decode.h"
#include "stm32f4xx.h"
#include "stm32f4xx_ll_iwdg.h"
#include "stm32f4xx_ll_rcc.h"

#define NOINIT_MAGIC 0x52535443UL /* "RSTC" */
#define IWDG_RELOAD 500U

typedef struct {
    uint32_t magic;
    uint32_t count; /* 下位16ビットだけを使う */
} noinit_reset_t;

static noinit_reset_t s_noinit __attribute__((section(".noinit")));

static uint32_t s_csr;
static reset_reason_t s_reason;
static uint16_t s_count;

/* reset_decode.h のビットの位置が CMSIS の定義と同じこと（ホストの単体テストはこの値で確かめる） */
_Static_assert(RESET_CSR_BORRSTF == RCC_CSR_BORRSTF, "BORRSTF");
_Static_assert(RESET_CSR_PINRSTF == RCC_CSR_PINRSTF, "PINRSTF");
_Static_assert(RESET_CSR_PORRSTF == RCC_CSR_PORRSTF, "PORRSTF");
_Static_assert(RESET_CSR_SFTRSTF == RCC_CSR_SFTRSTF, "SFTRSTF");
_Static_assert(RESET_CSR_IWDGRSTF == RCC_CSR_IWDGRSTF, "IWDGRSTF");
_Static_assert(RESET_CSR_LPWRRSTF == RCC_CSR_LPWRRSTF, "LPWRRSTF");

static uint16_t next_count(reset_reason_t r)
{
    bool valid = s_noinit.magic == NOINIT_MAGIC;
    uint16_t c = 0U;
    if (valid && (r != RESET_POWER_ON)) {
        c = diag_next_reset_count((uint16_t)(s_noinit.count & 0xFFFFU));
    }
    s_noinit.magic = NOINIT_MAGIC;
    s_noinit.count = c;
    return c;
}

void bsp_reset_init(void)
{
    s_csr = RCC->CSR;
    LL_RCC_ClearResetFlags();
    s_reason = reset_decode_csr(s_csr);
    s_count = next_count(s_reason);
}

uint32_t bsp_reset_csr(void)
{
    return s_csr;
}

void bsp_iwdg_start(void)
{
    LL_IWDG_Enable(IWDG); /* LSI も自動で動き出す。いったん動かすと止められない */
    LL_IWDG_EnableWriteAccess(IWDG);
    LL_IWDG_SetPrescaler(IWDG, LL_IWDG_PRESCALER_32);
    LL_IWDG_SetReloadCounter(IWDG, IWDG_RELOAD);
    while (LL_IWDG_IsReady(IWDG) != 1U) {
    }
    LL_IWDG_ReloadCounter(IWDG);
}

static reset_reason_t reset_reason(void)
{
    return s_reason;
}

static uint16_t reset_count(void)
{
    return s_count;
}

static void reset_system(void)
{
    NVIC_SystemReset();
}

static void wdt_refresh(void)
{
    LL_IWDG_ReloadCounter(IWDG);
}

const reset_port_t g_bsp_reset = {reset_reason, reset_count, reset_system};
const wdt_port_t g_bsp_wdt = {wdt_refresh};
