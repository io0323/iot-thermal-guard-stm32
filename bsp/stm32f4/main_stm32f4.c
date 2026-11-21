/*
 * ターゲット版の main（ホスト版の bsp/host/main.c とは別）。
 * スタックを塗る → bsp_init → firmware_init → ループで firmware_poll。
 */
#include "bsp_internal.h"
#include "firmware.h"
#include "ports.h"

static ports_t s_ports; /* firmware がポインタを持ち続けるので静的に置く */

int main(void)
{
    bsp_stack_paint();
    bsp_init(&s_ports);
    firmware_init(&s_ports, NULL); /* 観測用のフックはホスト版のログ用なので使わない */
    for (;;) {
        firmware_poll();
    }
}
