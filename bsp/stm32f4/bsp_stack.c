/*
 * スタックの塗りと最大使用量（P5、NFR-04）。
 *
 * スタックはリンカスクリプトで RAM の先頭に 4KB（_sstack〜_estack）。main の最初で、今の SP より下（まだ
 * 使っていない側）を 0xA5 で塗る。最大使用量は「下から数えて塗られたまま残っているバイト」を除いた量。
 * 塗った値と同じ値をたまたま書いた場合は少なめに出るので、目安として扱う（実測の確認は実機フェーズ）。
 */
#include <stdint.h>

#include "bsp_internal.h"
#include "stm32f4xx.h"

#define STACK_FILL 0xA5U

extern uint8_t _sstack[]; /* リンカスクリプト */
extern uint8_t _estack[];

void bsp_stack_paint(void)
{
    uint8_t *p = _sstack;
    uint8_t *sp = (uint8_t *)__get_MSP();
    while (p < sp) {
        *p = (uint8_t)STACK_FILL;
        p++;
    }
}

/* タスク側から呼ぶ（4KB を下から読むだけ。ISR からは呼ばない） */
uint16_t bsp_stack_peak(void)
{
    const uint8_t *p = _sstack;
    while ((p < _estack) && (*p == (uint8_t)STACK_FILL)) {
        p++;
    }
    return (uint16_t)(_estack - p);
}
