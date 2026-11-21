/*
 * スタックの塗りと最大使用量（P5、NFR-04）。
 *
 * スタックはリンカスクリプトで RAM の先頭に 4KB（_sstack〜_estack）。main の最初で、スタックの底から「今の SP より
 * STACK_PAINT_MARGIN バイト下」までを 0xA5 で塗る（今使っている領域と、塗っている関数自身の積みを塗らないことを、
 * 範囲の形で保証する。open-questions #54）。最大使用量は「下から数えて塗られたまま残っているバイト」を除いた量で、
 * 塗らなかった上の端の分（main の最初の深さ＋余白）より深く使えば、余白の有無で値は変わらない。
 * 塗った値と同じ値をたまたま書いた場合は少なめに出るので、目安として扱う（実測の確認は実機フェーズ）。
 */
#include <stdint.h>

#include "bsp_internal.h"
#include "stm32f4xx.h"

#define STACK_FILL 0xA5U
#ifndef STACK_PAINT_MARGIN
#define STACK_PAINT_MARGIN 64U /* 今の SP から下に空けておくバイト（#54）。値を変えるのは、余白で stack_peak が変わらないことを比べるときだけ */
#endif

extern uint8_t _sstack[]; /* リンカスクリプト */
extern uint8_t _estack[];

/*
 * volatile で書く：ふつうのループだと GCC が memset の呼び出しに置き換え、memset が自分の積んだ値（戻り番地）を
 * 塗りつぶして 0xA5A5A5A4 に戻る（P5 では実行していなかったので捕まえられず、P6 の Renode が見つけた。#54）
 */
void bsp_stack_paint(void)
{
    volatile uint8_t *p = _sstack;
    const uint8_t *end = (const uint8_t *)(__get_MSP() - STACK_PAINT_MARGIN);
    while (p < end) {
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
