/*
 * STM32F401RE のリンカスクリプト（P5、ADR-0005）。
 *
 * - FLASH 512K（0x08000000）、RAM 96K（0x20000000）。要件の上限（Flash 64KB・RAM 16KB、NFR-04）は
 *   tools/check_size.py が .map と arm-none-eabi-size で確かめる
 * - スタックは 4KB を RAM の先頭に置く（下に伸びてあふれると 0x20000000 より下の予約領域に当たり、
 *   BusFault → HardFault → 既定のハンドラの無限ループ → IWDG のリセットになる。.data・.bss を壊さない）
 * - ヒープは 0（malloc 禁止。_sbrk を持ち込まないことを check_size.py が確かめる）
 * - .noinit はリセットをまたいで残す（リセット回数、詳細設計書 第7章）。スタートアップは消さない
 * - スタートアップ（cmsis_device_f4 の startup_stm32f401xe.s）が使う記号：
 *   _estack、_sidata、_sdata、_edata、_sbss、_ebss
 */
ENTRY(Reset_Handler)

_Stack_Size = 4K;
_Min_Heap_Size = 0;

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 96K
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >FLASH

    /* スタック：RAM の先頭の 4KB。起動時に 0xA5 で塗り、塗られたままの量から最大使用量を求める（bsp_stack.c） */
    ._stack (NOLOAD) :
    {
        . = ALIGN(8);
        _sstack = .;
        . = . + _Stack_Size;
        . = ALIGN(8);
        _estack = .;
    } >RAM

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    /* リセットをまたいで残す変数（マジック値付き）。スタートアップは .bss だけを 0 にするので、ここは触られない */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
        _enoinit = .;
    } >RAM

    /* ヒープは置かない。end は newlib の記号の解決のためだけ（_sbrk は持ち込まない） */
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
