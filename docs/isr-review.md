# ISR のレビュー表（P5）

対象はターゲット版（`bsp/stm32f4/`）のすべての割り込みハンドラ。NFR-03「ISR の実行時間は各 20μs 以内。ISR の中ではフラグの設定とバッファへの書き込みしかしない」を、コードのレビューと逆アセンブルの命令数で確かめる。

**実測はしていない。** 実行時間の実測（DWT のサイクル数、GPIO のトグル）は実機フェーズで確認する。この表の時間は見積もりである。

## 割り込みの一覧

優先度グループは全ビットをプリエンプションにする（サブ優先度なし）。値が小さいほど優先し、上の割り込みは下の割り込みに割り込める（ADR-0005）。

| 割り込み | 優先度 | 要因 | 場所 |
| --- | --- | --- | --- |
| SysTick | 1 | 1ms ごと | `bsp/stm32f4/bsp_clock.c` `SysTick_Handler` |
| DMA2_Stream0 | 2 | ADC1 の DMA の半分転送・全転送（どちらも100ms ごと）、転送エラー | `bsp/stm32f4/bsp_adc.c` `DMA2_Stream0_IRQHandler` |
| USART2 | 3 | RXNE（1バイト受信）、受信エラー（ORE・FE・NE・PE）、TXE（送信レジスタが空、送るものがある間だけ有効） | `bsp/stm32f4/bsp_uart.c` `USART2_IRQHandler` |
| TIM2（**target-renode だけ**） | 2 | 更新（100Hz） | `bsp/stm32f4/bsp_adc.c` `TIM2_IRQHandler`（`ADC_SOFTWARE_START=1` のときだけある） |

**TIM2 の ISR は Renode 用ビルド（target-renode）だけにある**（ADR-0006、open-questions #53）。Renode の ADC は外部トリガーを持たないので、実機の「TIM2 の TRGO で ADC を起動する」経路の代わりに、TIM2 の更新割り込みから ADC をソフトウェアで開始する。**実機（target-debug・target-release）はこの ISR を持たず、経路が違う。** DMA の転送と平均は共通。

使わない割り込み：

- **EXTI15_10（ボタン）**：使わない（open-questions #48、ADR-0005）。ボタンは task_ui が10ms ごとにレベルを読む
- TIM2（実機のビルドでは TRGO で ADC を起動するだけ。target-renode だけ更新割り込みを使う）、TIM3（PWM の出力だけ）、ADC1（DMA で受ける）、IWDG：割り込みを有効にしない
- HardFault などの例外：スタートアップの既定のハンドラ（無限ループ）のまま。IWDG がリフレッシュされずに約500ms でリセットになる（リセット要因は watchdog）

## ISR ごとのレビュー

「命令」は `tools/isr_report.py` が target-release・target-debug の .elf を逆アセンブルし、入り口から届く命令を数えたもの。どの ISR にもループがない（制御の流れに輪がない）ので、1回の実行で通る命令はこの数以下である。

| ISR | やること | 触る共有変数（読み手） | volatile か | 禁止区間 | 最悪の命令数の目安（release／debug） |
| --- | --- | --- | --- | --- | --- |
| SysTick_Handler | `s_ms` を1つ増やす | `s_ms`（u32。`tick_now_ms` が読む） | volatile | なし。u32 の1ワードは一度の読み出しで読めるので、読み手にも要らない | 5／5 |
| DMA2_Stream0_IRQHandler | 半分転送なら HTIF を消して `s_done_half = 0`、全転送なら TCIF を消して `s_done_half = 1`、転送エラーなら TEIF を消すだけ（ストリームは止まり、300ms で temp_sensor が `restart()` を呼ぶ） | `s_done_half`（u8。`adc_take_sample` が読んで消す）。`s_dma_buf[20]` は DMA が書き、ISR は触らない | どちらも volatile | 読み手（`adc_take_sample`）が「読んで消す」の2手を PRIMASK の保存・復元で守る（ISR が間に入ると新しい通知を消してしまうため）。平均は禁止区間の外で、確定した半分（次の100ms は DMA に上書きされない）を読む | 25／28 |
| USART2_IRQHandler | SR を1回読む。RXNE かエラーなら DR を1回読み（フラグが消える）、エラーなら `s_errors` を1つ増やし、受信リングに空きがあれば1バイト入れて `s_rx_head` を進め、なければ `s_ring_dropped` を1つ増やす。TXE が有効で立っていれば、送信リングから1バイトを DR に書いて `s_tx_tail` を進め、空なら TXEIE を消す | 受信：`s_rx[256]`・`s_rx_head`（ISR が書く）・`s_rx_tail`（`uart_read` が書く）。送信：`s_tx[128]`・`s_tx_head`（`uart_write` が書く）・`s_tx_tail`（ISR が書く）。診断値：`s_ring_dropped`・`s_errors`（u16。`bsp_diag_read` が読む） | 添字と診断値は volatile。リングの中身は volatile にせず、書き手がデータを書いてから `__DMB()` を置いて添字を進める | なし。リングは単一生産者・単一消費者で、どの添字も書き手は1つだけ。TXEIE は、タスク側は `ATOMIC_SET_BIT`（LDREX・STREX）、ISR 側はふつうの読み書きで消す（タスクは ISR に割り込めず、タスクの LDREX〜STREX の間に ISR が入ると例外の出入りで排他の監視が消え、タスク側がやり直す） | 67／75 |

| TIM2_IRQHandler（target-renode だけ） | 更新フラグ（UIF）を消して、ADC の変換をソフトウェアで1回開始する（CR2 の SWSTART を書く）だけ | なし（ADC のレジスタだけ） | — | なし | 11（target-renode） |

どの ISR も、フラグを立てるか、1バイトをリングと入れ替えるか、カウンタを1つ増やすだけで、待ち、ループ、関数ポインタ越しの呼び出しを持たない（NFR-03 の「フラグ設定とバッファ書き込みしかしない」）。

## 実行時間の見積もり（NFR-03：各 20μs 以内）

見積もりの式（`tools/isr_report.py`）：例外の入り口と出口 22 サイクル＋命令の数 × 3 サイクル（Flash の待ち 2WS で、ART のキャッシュに外れたときの最悪として1命令3サイクル）。SYSCLK は 84MHz。

| ISR | 命令（release） | サイクルの目安 | 時間の目安 | 上限 |
| --- | --- | --- | --- | --- |
| SysTick_Handler | 5 | 37 | 0.44μs | 20μs |
| DMA2_Stream0_IRQHandler | 25 | 97 | 1.15μs | 20μs |
| USART2_IRQHandler | 67 | 223 | 2.65μs | 20μs |
| TIM2_IRQHandler（target-renode だけ） | 11 | 55 | 0.65μs | 20μs |

最悪の入れ子：USART2 の中に DMA2_Stream0、その中に SysTick が入ると、USART2 の終わりは約 0.44＋1.15＋2.65 ≒ 4.2μs 遅れる。どの ISR も単独で 20μs を大きく下回る。

- 115200bps の1バイトは約87μs なので、受信の ISR が次のバイトより先に終わり、ORE は起きない見込み（起きた場合は `uart_errors` に数える、#46）
- FPU の文脈の積み（遅延積み）は ISR が浮動小数点の命令を使わないので起きない前提。スタックの見積もりでは、安全側に FPU の文脈込みの枠（104 バイト）を取っている（`tools/stack_report.py`）

## 実機フェーズで確認すること

- 各 ISR の実行時間の実測（入り口と出口で DWT の CYCCNT を読む、または GPIO をトグルしてロジックアナライザで測る）と、上の見積もりとの照合
- 115200bps の連続受信で ORE が起きないこと（`uart_errors` が増えないこと）
- ADC の DMA の半分転送・全転送が 100ms ごとに来ること、`adc_take_sample` が毎回 true を返すこと
- スタックの最大使用量（`stack_peak`、塗りの方式）が見積もり（最悪 1560 バイト）を下回ること
