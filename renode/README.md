# renode/：Renode のプラットフォーム定義とモデル（P6、ADR-0006）

| ファイル | 内容 |
| --- | --- |
| `nucleo_f401re.repl` | Nucleo-F401RE（STM32F401RE）。Renode 同梱の `stm32f4.repl` を `using` し、メモリ（Flash 512K・RAM 96K、Flash を 0x0 にも）、クロック（SysTick・TIM2・TIM3 を 84MHz）、ADC1、DWT を足す |
| `dma2_circular.repl` | DMA2 を、循環・半分転送を足したモデル（`peripherals/STM32DMA_Circular.cs`）に置き換え、ADC1 の DMA の要求をつなぐ |
| `thermal_guard.resc` | 上の2つとモデルを読み込み、`target-renode` の .elf を載せる。ボタン（PC13）を High（基板のプルアップの代わり）にする |
| `peripherals/STM32DMA_Circular.cs` | Renode の `STM32DMA` に、循環（CIRC）・半分転送（HTIE・HTIF・CHTIF）・周辺機能の 32 ビットの読み出し・テスト用の回数を足したもの。変更した行には `thermal-guard` と書いた |

動かし方（どれも `antmicro/renode:1.16.1` のコンテナ。先に `cmake --preset target-renode && cmake --build --preset target-renode`）：

```sh
tools/renode-test.sh                                   # Robot Framework のテスト（tests/renode/）
tools/renode-contract.sh --measure 20                  # 契約テスト用の設定での、仮想時間と実時間の比
iotspec-contract --role mcu-c-01 --target-cmd "tools/renode-contract.sh {uart}"   # 契約テスト
```

**target-renode は実機と ADC の開始方法が違う**：Renode の ADC は外部トリガー（TIM2 の TRGO）を持たないので、TIM2 の更新割り込みからソフトウェアで変換を開始する（`ADC_SOFTWARE_START=1`）。DMA の転送と平均は実機と同じコード。

## 出典とライセンス

`peripherals/STM32DMA_Circular.cs` は、Renode v1.16.1（renode-infrastructure `add012af003a0f620d3da52828262676f374d121`）の
`src/Emulator/Peripherals/Peripherals/DMA/STM32DMA.cs` をもとにした。元のファイルは Antmicro の MIT ライセンスで、
ファイルの先頭に元の著作権の表示を残している。Renode 本体は変えておらず、`thermal_guard.resc` が起動時に読み込んで
コンパイルする。ライセンスの全文（Renode v1.16.1 の LICENSE から）：

```text
The MIT License (MIT)

Copyright (c) Antmicro <www.antmicro.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
