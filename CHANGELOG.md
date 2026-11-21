# CHANGELOG

## v0.1.0（2026-11-21）

最初の版。ファームウェアが STATE で送る `fw_version` は `0x0001`（上位バイト＝メジャー 0、下位バイト＝マイナー 1、つまり "0.1"。`config/version.h`、open-questions #8）で、この版に当たる。タグとリリースは io が作る。

**実機では確かめていない。** ホスト版・単体テスト・仮想時間のシナリオ・契約テスト・Renode で確かめた範囲は [docs/test-spec.md](docs/test-spec.md)、実機で確かめることは [docs/hardware-phase.md](docs/hardware-phase.md)。

### P1 ホスト版の骨組み・UART・契約テスト（#1）

- CMake のプリセット（host-debug、ASan・UBSan）、層ごとのライブラリ（app・service・lib・ports）
- lib：cobs・crc16・crc32・ring_buffer。service：link（フレームの組み立て・解析、種別ごとの seq、送信キュー）・upstream（上位断10秒）・scheduler・report。app：command_handler（全検証条件、直近16件の重複キャッシュ）・cooling_policy
- ホスト版（bsp_host）：pty（raw・115200・8N1）、実時間、`--temp`、JSON Lines のログ、SIGTERM・EOF での終了
- iot-common-spec v0.2.1 のゴールデンの符号化・復号テスト、`iotspec-contract --role mcu-c-01` の7チェック、CI の host ジョブ

### P2 温度計測と状態遷移（#2）

- 変換表の生成（`tools/gen_ntc_table.py`、141点）、median（5点）・moving_avg（8点）
- temp_sensor の本版（短絡・断線、範囲外の丸め、変化率、ADC 無応答、ready）、safety_fsm の本版（遷移表の全行）、event_map
- sim_harness（1ms 刻みの仮想時間）とシナリオ CSV、ホスト版の `--scenario`。NFR-01 は3通りの昇温で約1.5秒

### P3 ボタン・警報・ヘルス（#3）

- button（20ms のチャタリング除去、短押し・3秒の長押し）、alarm（LED・ブザーのパターン、消音）、health（生存報告とウォッチドッグのリフレッシュ）、diag
- ホスト版の疑似ウォッチドッグ（`execv` で再起動）、リセット要因・回数、シナリオの `button_down`・`button_up`・`hang`
- pytest（`tests/host_integration`）：hang → 再起動 → 新しい boot_id の STATE → BOOT

### P4 品質ゲート（#4）

- カバレッジ（host-coverage・gcovr、app・service 80% 以上）、cppcheck の MISRA、clang-tidy、lizard（複雑度10以下）、Doxygen、禁止項目の検査。CI の quality ジョブ
- MISRA の逸脱と規則の再分類（ADR-0002）

### P5 STM32 ターゲット版（#5）

- 時刻の比較を `tick_reached()` にまとめた（#42）
- arm-none-eabi-gcc 13.2.1、STM32CubeF4 v1.29.0 の CMSIS と LL（ADR-0004）。プリセット target-debug・target-release
- bsp_stm32f4：84MHz、SysTick、TIM2 → ADC1 → DMA2（循環・半分転送）、TIM3 PWM 25kHz、USART2、IWDG、リセット要因（`reset_decode.c`）、`.noinit` のリセット回数、boot_id の材料、DWT、スタックの塗り（ADR-0005）
- サイズ・スタック・ISR の検査（`tools/check_size.py`・`stack_report.py`・`isr_report.py`）、ISR のレビュー表（`docs/isr-review.md`）、CI の target ジョブ

### P6 Renode（#6）

- プラットフォーム定義（`renode/`）と、循環・半分転送を足した DMA のモデル（Renode v1.16.1 の `STM32DMA.cs` がもと、MIT）。プリセット target-renode（ADC はソフトウェア開始で、実機と経路が違う）
- Robot Framework のテスト7件、Renode の上での契約テスト（7チェック）、CI の renode ジョブ（ADR-0006）
- 修正：P5 の `bsp_stack_paint` が起動で落ちる不具合（GCC が塗りを `memset` に置き換えていた。#54）

### P7 書類の仕上げ

- テスト仕様書とトレーサビリティ（`docs/test-spec.md`）と、その検査（`tools/check_traceability.py`、CI の quality ジョブ）
- 実機フェーズの手順書（`docs/hardware-phase.md`）
- README（構成図・状態遷移図・検証の全体像・主な数値・書類の地図・開発の進め方）、`docs/prompts/README.md`、この CHANGELOG
