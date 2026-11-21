/*
 * ターゲット版の main（ホスト版の bsp/host/main.c とは別）。
 * スタックを塗る → bsp_init → firmware_init → ループで firmware_poll。
 */
#include "bsp_internal.h"
#include "firmware.h"
#include "ports.h"

#ifndef RENODE_TEST_HOOKS
#define RENODE_TEST_HOOKS 0 /* target-renode だけ 1（CMake の TG_RENODE_TEST_HOOKS） */
#endif

static ports_t s_ports; /* firmware がポインタを持ち続けるので静的に置く */

#if RENODE_TEST_HOOKS
/*
 * target-renode だけ（ADR-0006）：Renode のモニターから 0 以外を書くとループを止め、IWDG のリセットを起こす
 * （ウォッチドッグのテスト）。target-debug・target-release にはこの変数はない。.bss なのでリセットで 0 に戻る
 */
volatile uint32_t g_renode_hang;
#endif

int main(void)
{
    bsp_stack_paint();
    bsp_init(&s_ports);
    firmware_init(&s_ports, NULL); /* 観測用のフックはホスト版のログ用なので使わない */
    for (;;) {
#if RENODE_TEST_HOOKS
        while (g_renode_hang != 0U) {
        }
#endif
        firmware_poll();
    }
}
