# 作品01 フェーズ P5：STM32 ターゲット版

## P4 の片付けと追補（先に済ませる）

PR #4 はレビュー済みで、io がマージする。マージを確かめてから `phase/p4-quality` をローカルとリモートで削除し、main から `phase/p5-target` を切ること。

P5 の最初のコミットとして、P4 の申し送りの2点を片付ける。

1. **時刻の比較を `tick_reached()` にまとめる**（open-questions に「決定（2026-11-21）」として記録）
   - `lib/tick.h` に `static inline bool tick_reached(uint32_t now, uint32_t deadline)` を置く。中身は「`(uint32_t)(now - deadline) < 0x80000000UL` なら真」。符号付きへのキャストを使わないので MISRA 10.8 に当たらない
   - `(int32_t)(now - t) >= 0` の10か所をすべて置き換え、ADR-0002 の D-01〜D-10 と `misra-suppressions.txt` の該当行を消す（ADR には「tick_reached() に置き換えて解消」と履歴を残す）
   - 単体テスト：境界（差が 0、0x7FFFFFFF、0x80000000、0xFFFFFFFF）と、uptime の一周をまたぐ場合
   - `CLAUDE.md` の「時刻の比較は必ず `(int32_t)(now - t) >= 0` の形」を「時刻の比較は必ず `tick_reached()` を使う」にする。`docs/design/02-detailed-design.md` 第2章の冒頭の段落にも、原本と同じく「実際のコードでは lib/tick.h の tick_reached(now, deadline)（符号なしの差が 0x80000000 未満なら真）だけを使い、符号付きへのキャストを各所に書かない（MISRA 10.8 の逸脱を作らないため）」を足す
2. **MISRA の抑止を行番号からインラインに移す**：残る逸脱（8.7 と 19.2）を、該当行の直前の `// cppcheck-suppress misra-c2012-X.Y ; ADR-0002 D-nn` コメントに移し、cppcheck を `--inline-suppr` で動かす。`misra-suppressions.txt` には 15.5 の再分類（R-01）だけを残す。行がずれてもゲートが落ちないようにするため

## はじめに読むもの

`CLAUDE.md`、`docs/open-questions.md`、要件定義・基本設計書のアーキテクチャと割り込みの表・NFR-03・NFR-04、詳細設計書の第1章（プリセット・コンパイルオプション）、第2章（ports・タスク）、第3章（取得）、第5章（PWM・ボタン）、第6章（受信・送信）、第7章（IWDG・リセット要因・boot_id）、第10章の P5 の行。読み終えたら「作るもの・作らないもの・不明点」を短く報告してから始める。

## ツールと取り込み

- **ツールチェーン**：ubuntu-24.04 の apt の `gcc-arm-none-eabi` を使う。手元でも CI と同じコンテナで動かす（`tools/target-docker.sh` を作る）。版は ADR-0004 に書く
- **ベンダーのコード**：submodule で取り込み、STM32CubeF4 の同じリリースにそろったタグに固定する（タグとそのリリース名を ADR-0004 に書く）
  - `third_party/cmsis_core`：STMicroelectronics/cmsis_core
  - `third_party/cmsis_device_f4`：STMicroelectronics/cmsis_device_f4（`startup_stm32f401xe.s` もここから使う）
  - `third_party/stm32f4xx_hal_driver`：STMicroelectronics/stm32f4xx_hal_driver（LL だけを使う。HAL のソースはビルドに入れない）
- これらは MISRA・clang-tidy・lizard・Doxygen の対象外のまま。`tools/check_forbidden.sh` に「STM32 と CMSIS のヘッダを include してよいのは `bsp/stm32f4/` だけ」の検査を足す

## 決めてある値（ADR-0005 に表で書く）

| 項目 | 値 |
|---|---|
| クロック | HSI 16MHz → PLL（M=16、N=336、P=4）で SYSCLK 84MHz。APB1 42MHz（タイマーは 84MHz）、APB2 84MHz。Flash の待ち 2WS。ST-LINK の MCO（HSE）には頼らない |
| SysTick | 1ms。tick_port の now_ms。DWT の CYCCNT を有効にして cycles() にする |
| 温度の ADC | PA0（Arduino A0）＝ADC1_IN0。TIM2 の TRGO 100Hz で変換、DMA2 Stream0 Channel0 で循環20語、半分転送・全転送の割り込みで10語ずつ平均（第3章） |
| ファン PWM | PA6（D12）＝TIM3_CH1、PSC 0・ARR 3359 で 25kHz、CCR = duty × 3360 ÷ 1000 |
| UART | USART2、PA2・PA3（ST-LINK の仮想 COM）、115200・8N1。RXNE で受信リング256、TXE で送信リング128 |
| ボタン | PC13（B1、押すと Low）。EXTI15_10 は「押されたかもしれない」フラグだけを立て、レベルは task_ui が読む |
| LED | 緑 PA5（LD2、D13）、黄 PB4（D5）、赤 PB5（D4） |
| ブザー | PB10（D6）、GPIO の出力 |
| IWDG | LSI を 1/32、リロード 500（公称500ms） |
| リセット要因 | RCC_CSR を第7章の優先順で読み、RMVF で消す。回数は `.noinit` のマジック値付きの変数 |
| boot_id の材料 | ADC をソフトウェア開始で使い、内部温度（ADC1_IN18）と VREFINT（ADC1_IN17）を32回ずつ。DWT のサイクル数、UID（0x1FFF7A10 から96ビット）、RCC_CSR。TIM2 の起動より前に集める |
| 割り込みの優先度 | NVIC の優先度グループは全ビットをプリエンプションに。SysTick 1、DMA2_Stream0 2、USART2 3、EXTI15_10 4 |
| スタック | リンカスクリプトで 4KB を確保し、起動時に 0xA5 で塗る。使われた最大量を診断値に足す（stack_peak） |

## P5 で作るもの

1. **ビルド**：`cmake/arm-none-eabi.cmake`、プリセット `target-debug`（`-Og -g3`）と `target-release`（`-Os -g`）、出力先 `build/target-debug`・`build/target-release`。第1章のオプション（`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -ffunction-sections -fdata-sections -fstack-usage`、`--gc-sections`、`-Wl,-Map`）。app・service・lib はホスト版と同じソースをそのまま使う
2. **リンカスクリプト**：`bsp/stm32f4/STM32F401RETx.ld`。FLASH 512K（0x08000000）、RAM 96K（0x20000000）、`.noinit`、スタック 4KB、ヒープ 0
3. **bsp_stm32f4**：上の表のとおりに ports をすべて実装する。`bsp/stm32f4/pinmap.h` にピンを集約する。ISR はフラグを立てるかリングに入れるだけ。ISR と共有する変数は `volatile`、複数ワードの読み出しは PRIMASK を保存・復元する短い禁止区間で守る
4. **ターゲットの main**：`bsp_init` → `firmware_init` → ループで `firmware_step`。ホスト版の main とは別のファイル
5. **サイズの検査**：`tools/check_size.py` が `arm-none-eabi-size` と .map から、Flash 64KB 以内・RAM（.data＋.bss＋スタック）16KB 以内を確かめる。超えたら失敗
6. **スタックの見積もり**：`-fstack-usage` の .su を集め、関数ごとの使用量の上位を報告する（`tools/stack_report.py`）。再帰と可変長配列がないことも確かめる
7. **ISR のレビュー表**：`docs/isr-review.md`。ISR ごとに「やること・触る共有変数・volatile か・禁止区間・最悪の命令数の目安」を表にし、NFR-03（各20μs 以内）を満たす根拠を書く。実測は実機フェーズで行うことも明記する
8. **CI**：`target` ジョブ（apt で gcc-arm-none-eabi → 2つのプリセットでビルド → check_size → stack_report → .elf・.map・報告を成果物に）
9. **ADR**：ADR-0004（ツールチェーンとベンダーのコードの版）、ADR-0005（クロック・ピン・周辺機能の割り当て、上の表）
10. **README**：ターゲット版のビルド方法、Flash・RAM の使用量、「実機では未確認」であること

## P5 で作らないもの

実機への書き込みと実機での動作確認（実機フェーズ）、Renode（P6）、新しい機能。ホスト版の振る舞いは変えない。

## 完了条件（すべて満たしてから PR）

1. 追補の2点（tick_reached、インラインの抑止）が済み、quality ジョブがすべて合格したまま
2. `target-debug`・`target-release` が警告ゼロでビルドできる（コンテナと CI の両方）
3. check_size が合格。Flash・RAM の使用量を PR に載せる
4. stack_report の上位と、確保した 4KB で足りる根拠を PR に載せる
5. `docs/isr-review.md` がすべての ISR を網羅している
6. ホスト側が変わっていない：CTest 全件、契約テスト7件、pytest が手元と CI で合格。STM32・CMSIS のヘッダが bsp/stm32f4 の外にない

PR の説明には、完了条件ごとの結果、ベンダーのコードのタグ、open-questions に足した項目、P6 への申し送り（Renode で再現できない周辺機能の見込みを含む）を書く。マージはしない。

## 止まって相談すること

- 上の表の値ではリファレンスマニュアル上成り立たない（例：ピンの機能が重なる、クロックの設定が範囲外）と分かったとき
- app・service・ports の変更が必要になったとき（ターゲットのために共通部分を変えない。変えるなら理由を添えて相談）
- サイズやスタックが上限を超えたとき
- 設計書に値や順序がないことを決める必要が出たとき

いずれも open-questions に記録し、その件に関わる作業を止めて報告する。
