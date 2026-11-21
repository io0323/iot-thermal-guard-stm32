# ADR-0004 ターゲット版のツールチェーンとベンダーのコードの版

- 状態：採用（2026-11-21）
- 関係する書類：要件定義・基本設計書 NFR-07・NFR-09、詳細設計書 第1章・第9章、open-questions #49・#50

## 状況

P5 で STM32F401RE 向けのターゲット版を作る。ホスト版と同じソースツリーから、プリセットの切り替えだけでビルドする（NFR-09）。ツールチェーンとベンダーのコード（CMSIS・LL）の版を固定しないと、同じコミットから同じバイナリができず、サイズやスタックの検査の結果も変わる。

この Mac（Intel）では Homebrew が使えない（信頼の設定がない tap と Tier 3 の扱いのため）。

## 決定

### ツールチェーン

| 項目 | 版 | 入手先 |
| --- | --- | --- |
| arm-none-eabi-gcc | 13.2.1（13.2.rel1、Debian・Ubuntu のパッケージ 15:13.2.rel1-2） | ubuntu-24.04 の apt の `gcc-arm-none-eabi` |
| binutils | 2.42 | ubuntu-24.04 の apt の `binutils-arm-none-eabi`（gcc-arm-none-eabi の依存） |
| newlib（nano） | 4.4.0.20231231 | ubuntu-24.04 の apt の `libnewlib-arm-none-eabi` |

- CI の target ジョブは ubuntu-24.04 で apt から入れる。手元では `tools/target-docker.sh` が CI と同じ ubuntu:24.04 のコンテナ（`tools/docker/Dockerfile`、品質ゲートと同じイメージ）で `tools/target.sh` を流す。合否は CI を正とし、実際の版は報告の `versions.txt` に残す
- プリセットは `target-debug`（`-Og -g3`、`build/target-debug`）と `target-release`（`-Os -g`、`build/target-release`）。出力先を分けるのは #50 の決定
- コンパイルオプションは詳細設計書 第1章のとおり（`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -ffunction-sections -fdata-sections -fstack-usage`、リンクは `--gc-sections`、`-Wl,-Map`）。スタックの見積もりのために `-fcallgraph-info=su` も付ける
- リンクは `-nostartfiles --specs=nano.specs --specs=nosys.specs`。newlib の crt0 は `exit` を通して stdio の後始末と malloc を持ち込むので使わず、cmsis_device_f4 のスタートアップから始める（`tools/check_size.py` が malloc・_sbrk を持ち込んでいないことを確かめる）

### ベンダーのコード

STM32CubeF4 の同じリリース（**v1.29.0**、2026-11-18）にそろったタグに固定する。v1.29.0 の `Drivers/` の submodule の指すコミットと、`Drivers/CMSIS/Core/Include` の中身が、次のタグと一致することを確かめた。

| submodule | タグ | 使うもの |
| --- | --- | --- |
| `third_party/cmsis_core`（STMicroelectronics/cmsis_core） | `v5.9.0_20250520` | `Include/`（core_cm4.h など） |
| `third_party/cmsis_device_f4`（STMicroelectronics/cmsis_device_f4） | `v2.6.12` | `Include/`（stm32f401xe.h）、`Source/Templates/gcc/startup_stm32f401xe.s`、`Source/Templates/system_stm32f4xx.c` |
| `third_party/stm32f4xx_hal_driver`（STMicroelectronics/stm32f4xx_hal_driver） | `v1.8.6` | `Inc/stm32f4xx_ll_*.h` の inline 関数だけ。HAL のソースも LL のソース（.c）もビルドに入れない |

- ベンダーのヘッダは CMake で `SYSTEM` のインクルードにし、自分のコードの警告オプション（`-Werror` を含む）を当てない。スタートアップと `system_stm32f4xx.c` はテンプレートのままで、警告オプションを付けない
- ベンダーのコードは MISRA・clang-tidy・lizard・Doxygen の対象外（NFR-07）。STM32・CMSIS のヘッダを include してよいのは `bsp/stm32f4/` だけで、`tools/check_forbidden.sh` が確かめる

## 理由

- apt の版は ubuntu-24.04 のイメージで固定され、CI と手元のコンテナで同じものが入る。Arm の配布物を別に落とすより手順が少なく、Homebrew の問題を避けられる
- CubeF4 の1つのリリースにそろえると、CMSIS のデバイスヘッダと LL の組み合わせを ST が確かめた状態で使える。最新のリリース（v1.29.0）にしたのは、3つの submodule のタグがそのリリースと過不足なく一致したため
- LL のヘッダの inline 関数だけを使うと、レジスタの操作が自分のコードの中に見え、割り込みの流れを自分で設計できる（要件定義・基本設計書 第1章の前提）

## 影響

- ubuntu-24.04 のイメージが更新されて apt の版が変わると、警告やサイズが変わることがある。そのときはこの表と `versions.txt` を見比べる
- ベンダーのタグを上げるときは、CubeF4 の同じリリースの3つをまとめて上げ、この表を直す
- 手元の Docker の空き容量が足りないとコンテナのイメージを作れない（P5 の作業中に起きた）。そのときは CI の target ジョブの結果を正とする
