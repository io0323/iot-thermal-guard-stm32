/*
 * USART2（PA2・PA3、ST-LINK の仮想 COM、115200・8N1）と受信・送信のリング（詳細設計書 第6章、ADR-0005）。
 *
 * - 受信：RXNE の割り込みで1バイトずつ256バイトのリングに入れる。満杯なら捨てて uart_ring_dropped を数える（#45）
 * - 受信エラー（ORE・FE・NE・PE）：SR → DR の順に読んで消し、そのバイトはリングに入れる（壊れたフレームは
 *   COBS と CRC が捨てる）。種類を問わず uart_errors に数える（#46）
 * - 送信：write は1フレーム分の空きがあるときだけリング（128バイト）に入れて TXE の割り込みを有効にする。
 *   ISR は1バイトずつ DR に書き、空になったら TXE の割り込みを止める
 * - リングは単一生産者・単一消費者。添字は volatile で、書き手だけが自分の添字を進める（ロックなし）。
 *   データを書いてから添字を進める順序は __DMB() で守る
 */
#include <stddef.h>
#include <stdint.h>

#include "bsp_internal.h"
#include "pinmap.h"
#include "ports.h"
#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"

#define UART_BAUD 115200U
#define PCLK1_HZ 42000000U
#define RX_RING_SIZE 256U /* 2 のべき乗 */
#define TX_RING_SIZE 128U /* 2 のべき乗 */
#define UART_ERROR_FLAGS (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)

static uint8_t s_rx[RX_RING_SIZE];
static volatile uint32_t s_rx_head; /* ISR が進める */
static volatile uint32_t s_rx_tail; /* タスクが進める */
static uint8_t s_tx[TX_RING_SIZE];
static volatile uint32_t s_tx_head; /* タスクが進める */
static volatile uint32_t s_tx_tail; /* ISR が進める */
static volatile uint16_t s_ring_dropped; /* ISR だけが書く */
static volatile uint16_t s_errors;       /* ISR だけが書く */

static void sat_inc_volatile(volatile uint16_t *c)
{
    if (*c != UINT16_MAX) {
        *c = (uint16_t)(*c + 1U);
    }
}

void bsp_uart_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
    LL_GPIO_SetPinMode(PIN_UART_PORT, PIN_UART_TX_PIN, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(PIN_UART_PORT, PIN_UART_TX_PIN, PIN_UART_AF);
    LL_GPIO_SetPinMode(PIN_UART_PORT, PIN_UART_RX_PIN, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(PIN_UART_PORT, PIN_UART_RX_PIN, PIN_UART_AF);
    LL_GPIO_SetPinPull(PIN_UART_PORT, PIN_UART_RX_PIN, LL_GPIO_PULL_UP);

    LL_USART_SetTransferDirection(USART2, LL_USART_DIRECTION_TX_RX);
    LL_USART_ConfigCharacter(USART2, LL_USART_DATAWIDTH_8B, LL_USART_PARITY_NONE, LL_USART_STOPBITS_1);
    LL_USART_SetHWFlowCtrl(USART2, LL_USART_HWCONTROL_NONE);
    LL_USART_SetOverSampling(USART2, LL_USART_OVERSAMPLING_16);
    LL_USART_SetBaudRate(USART2, PCLK1_HZ, LL_USART_OVERSAMPLING_16, UART_BAUD);
    LL_USART_EnableIT_RXNE(USART2);
    LL_USART_EnableIT_ERROR(USART2);
    NVIC_SetPriority(USART2_IRQn, BSP_PRIO_USART2);
    NVIC_EnableIRQ(USART2_IRQn);
    LL_USART_Enable(USART2);
}

static void rx_isr(uint32_t sr)
{
    uint8_t b = LL_USART_ReceiveData8(USART2); /* SR を読んだあとに DR を読むと RXNE とエラーのフラグが消える */
    if ((sr & UART_ERROR_FLAGS) != 0U) {
        sat_inc_volatile(&s_errors);
    }
    uint32_t head = s_rx_head;
    if ((head - s_rx_tail) >= RX_RING_SIZE) {
        sat_inc_volatile(&s_ring_dropped);
        return;
    }
    s_rx[head & (RX_RING_SIZE - 1U)] = b;
    __DMB();
    s_rx_head = head + 1U;
}

static void tx_isr(void)
{
    uint32_t tail = s_tx_tail;
    if (tail == s_tx_head) {
        /* ISR の中では LL_USART_DisableIT_TXE（LDREX・STREX のやり直しのループ）を使わず、ふつうの読み書きで消す。
         * タスクはこの ISR に割り込めず、タスク側の ATOMIC_SET_BIT の途中に ISR が入ると例外の出入りで排他の監視が
         * 消えてタスク側がやり直すので、競合しない。CR1 に触る割り込みはほかにない */
        CLEAR_BIT(USART2->CR1, USART_CR1_TXEIE);
        return;
    }
    LL_USART_TransmitData8(USART2, s_tx[tail & (TX_RING_SIZE - 1U)]);
    s_tx_tail = tail + 1U;
}

/* ISR：1バイトをリングと入れ替えるだけ */
void USART2_IRQHandler(void);
void USART2_IRQHandler(void)
{
    uint32_t sr = LL_USART_ReadReg(USART2, SR);
    if ((sr & (USART_SR_RXNE | UART_ERROR_FLAGS)) != 0U) {
        rx_isr(sr);
    }
    if (((sr & USART_SR_TXE) != 0U) && (LL_USART_IsEnabledIT_TXE(USART2) != 0U)) {
        tx_isr();
    }
}

static size_t uart_read(uint8_t *buf, size_t cap)
{
    size_t n = 0U;
    uint32_t tail = s_rx_tail;
    uint32_t head = s_rx_head;
    __DMB();
    while ((n < cap) && (tail != head)) {
        buf[n] = s_rx[tail & (RX_RING_SIZE - 1U)];
        tail++;
        n++;
    }
    __DMB();
    s_rx_tail = tail;
    return n;
}

static size_t uart_write(const uint8_t *buf, size_t len)
{
    uint32_t head = s_tx_head;
    uint32_t used = head - s_tx_tail;
    if (len > (size_t)(TX_RING_SIZE - used)) {
        return 0U; /* 1フレーム分の空きがないときは受け取らない（ホスト版と同じ） */
    }
    for (size_t i = 0U; i < len; i++) {
        s_tx[(head + (uint32_t)i) & (TX_RING_SIZE - 1U)] = buf[i];
    }
    __DMB();
    s_tx_head = head + (uint32_t)len;
    LL_USART_EnableIT_TXE(USART2); /* ATOMIC_SET_BIT（LDREX・STREX）なので ISR の書き換えと競合しない */
    return len;
}

void bsp_uart_counters(uint16_t *ring_dropped, uint16_t *errors)
{
    *ring_dropped = s_ring_dropped;
    *errors = s_errors;
}

const uart_port_t g_bsp_uart = {uart_read, uart_write};
