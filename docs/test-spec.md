# テスト仕様書とトレーサビリティ（P7）

要件定義・基本設計書の FR-01〜14・NFR-01〜10 の1つずつに、それを確かめているテストを対応させる。ホストや Renode では確かめられないものは「実機フェーズで確認」とし、[docs/hardware-phase.md](hardware-phase.md) の項目（HW-nn）へリンクする。

この表は `tools/check_traceability.py` が CI（quality ジョブ）で検査する：①すべての ID が表にある ②表に書いたテストが実在する ③HW-nn のリンク先が hardware-phase.md にある ④どの行も、テストか HW-nn のどちらかを持つ。

## テストの段

| 段 | 何を確かめるか | 件数 | どこで動くか（CI のジョブ） |
| --- | --- | --- | --- |
| 単体 | lib・service・app の各モジュール（Unity、ポートはフェイク）。`test_reset_decode` は bsp/stm32f4 の純粋な関数 | 24 実行ファイル・211 件 | host（CTest）、quality（カバレッジ） |
| ゴールデン | iot-common-spec v0.2.1 の `uart/golden/*.json` 全件の符号化・復号・壊れたフレーム | 1 実行ファイル・5 件 | host（CTest） |
| シナリオ | firmware 全体を仮想時間（1ms 刻み）で CSV から動かす（`sim_harness`）。CSV の読み込み | 2 実行ファイル・26 件 | host（CTest） |
| 契約 | `iotspec-contract --role mcu-c-01` の7チェック | 7 件 × 2（ホスト版・Renode） | host、renode |
| pytest | ホスト版を pty 越しに動かし、hang → 疑似ウォッチドッグの再起動 | 5 件 | host |
| 品質ゲート | カバレッジ・MISRA（cppcheck）・clang-tidy・lizard・Doxygen・禁止項目 | 6 ゲート | quality |
| ターゲット | target-debug・target-release のビルド（警告ゼロ）、サイズ、スタック、ISR | 4 検査 × 2 プリセット | target |
| Renode | target-renode を Renode で動かす（Robot Framework）。塗りの余白の比較、仮想時間と実時間の比 | 7 件＋2 検査 | renode |

件数は `RUN_TEST` の数（CTest は27の実行ファイル）、`tests/host_integration/test_reboot.py` の `test_` 関数、`tests/renode/thermal_guard.robot` のテストケースの数。

### 表の書き方

- tests/ のファイル::名前（例 `tests/unit/test_tick.c::test_across_uptime_wrap`）：C は `RUN_TEST` するテスト関数、Python は `test_` 関数、Robot はテストケース名の「：」より前
- contract:名前（例 `contract:boot_order`）：iotspec-contract のチェック（`external/iot-common-spec/src/iotspec/contract/roles/mcu_c_01.py`）。ホスト版（host ジョブ）と Renode（renode ジョブ）の両方で流す
- ci:ジョブ と tools/ のスクリプト（例 `ci:quality`、`tools/quality.sh`）：CI のジョブと、そこで動くスクリプト
- [HW-nn](hardware-phase.md#hw-01)：実機フェーズで確認する項目

## 機能要件（FR）

| ID | 要件（要約） | 段 | 確かめているテスト | 実機フェーズで確認 |
| --- | --- | --- | --- | --- |
| FR-01 | 100ms 周期の ADC＋DMA、表＋線形補間で 0.01℃ の整数、−20〜120℃ | 単体・シナリオ・Renode | `tests/unit/test_ntc_table.c::test_endpoints_match_design_approximation`、`tests/unit/test_ntc_table.c::test_table_points_convert_exactly`、`tests/unit/test_ntc_table.c::test_interpolation_rounds_half_up`、`tests/unit/test_ntc_table.c::test_ends_and_outside`、`tests/unit/test_temp_sensor.c::test_clamp_boundaries`、`tests/unit/test_temp_sensor.c::test_ready_at_12th_sample`、`tests/scenario/test_scenarios.c::test_nfr01_10c_per_s`、`tests/renode/thermal_guard.robot::DMA`、`tests/renode/thermal_guard.robot::温度と CRITICAL` | [HW-03](hardware-phase.md#hw-03) 変換表の実測、[HW-04](hardware-phase.md#hw-04) TRGO の経路と ADC・DMA の周期 |
| FR-02 | 5点メディアン、8点移動平均 | 単体 | `tests/unit/test_median.c::test_removes_outlier`、`tests/unit/test_median.c::test_sliding_window`、`tests/unit/test_moving_avg.c::test_sliding`、`tests/unit/test_moving_avg.c::test_rounding_half_away_from_zero`、`tests/unit/test_temp_sensor.c::test_slow_ramp_passes` | — |
| FR-03 | 断線・短絡（<50・>4045 が3回連続）、変化率 5℃/100ms 超 | 単体・シナリオ | `tests/unit/test_temp_sensor.c::test_short_needs_three_consecutive_below_50`、`tests/unit/test_temp_sensor.c::test_open_needs_three_consecutive_above_4045`、`tests/unit/test_temp_sensor.c::test_50_and_4045_are_not_faults`、`tests/unit/test_temp_sensor.c::test_rate_rejects_three_then_fault`、`tests/unit/test_temp_sensor.c::test_rate_exactly_5c_is_accepted`、`tests/unit/test_temp_sensor.c::test_adc_timeout_300ms_and_restart_every_second`、`tests/scenario/test_scenarios.c::test_fault_sensor_open`、`tests/scenario/test_scenarios.c::test_fault_sensor_short`、`tests/scenario/test_scenarios.c::test_fault_adc_stall`、`tests/scenario/test_scenarios.c::test_jump_rejected_three_times_then_fault` | [HW-03](hardware-phase.md#hw-03) 実際の断線・短絡の生値 |
| FR-04 | WARNING 45℃・CRITICAL 60℃、上昇は1秒継続、下降はヒステリシス 3℃ | 単体・シナリオ | `tests/unit/test_safety_fsm.c::test_row5_critical_after_10`、`tests/unit/test_safety_fsm.c::test_row6_warning_after_10`、`tests/unit/test_safety_fsm.c::test_row6_broken_run_restarts`、`tests/unit/test_safety_fsm.c::test_row7_clear_after_10_below_warn_minus_hyst`、`tests/scenario/test_scenarios.c::test_warning_hysteresis` | — |
| FR-05 | 5状態をテーブル駆動で管理 | 単体・シナリオ | `tests/unit/test_safety_fsm.c::test_row1_init_not_ready_at_deadline`、`tests/unit/test_safety_fsm.c::test_row2_init_classifies`、`tests/unit/test_safety_fsm.c::test_row3_fault_has_priority`、`tests/unit/test_safety_fsm.c::test_row4_recover_with_ack_and_valid_run`、`tests/unit/test_safety_fsm.c::test_row8_ack_below_crit_minus_hyst`、`tests/unit/test_safety_fsm.c::test_row9_each_state`、`tests/unit/test_event_map.c::test_not_in_table`、`tests/scenario/test_scenarios.c::test_alarm_cycle_stays_critical_without_ack` | — |
| FR-06 | 25kHz PWM、NORMAL 35〜45℃で 0→40%、WARNING 70%、CRITICAL・SENSOR_FAULT 100% | 単体・シナリオ・Renode | `tests/unit/test_cooling_policy.c::test_auto_boundaries`、`tests/unit/test_cooling_policy.c::test_floor_per_state`、`tests/unit/test_cooling_policy.c::test_auto_vs_floor`、`tests/scenario/test_scenarios.c::test_nfr01_1c_per_s`、`tests/renode/thermal_guard.robot::指令と PWM`、`tests/renode/thermal_guard.robot::温度と CRITICAL` | [HW-04](hardware-phase.md#hw-04) PWM の周波数と duty の波形、[HW-01](hardware-phase.md#hw-01) ファンの駆動 |
| FR-07 | 状態ごとの LED（点灯・1Hz・4Hz）とブザー（断続・連続） | 単体・シナリオ | `tests/unit/test_alarm.c::test_init_pattern`、`tests/unit/test_alarm.c::test_normal_solid_green`、`tests/unit/test_alarm.c::test_warning_yellow_1hz`、`tests/unit/test_alarm.c::test_critical_red_4hz_buzzer_250`、`tests/unit/test_alarm.c::test_sensor_fault_red_solid_continuous`、`tests/unit/test_alarm.c::test_silence_and_release`、`tests/scenario/test_scenarios.c::test_ack_flow_silence_then_ack` | [HW-14](hardware-phase.md#hw-14) LED・ブザー・ボタンの実物 |
| FR-08 | 10ms のポーリング、20ms のチャタリング除去、短押し（消音→承認）、3秒長押し | 単体・シナリオ | `tests/unit/test_button.c::test_single_glitch_ignored`、`tests/unit/test_button.c::test_two_reads_confirm`、`tests/unit/test_button.c::test_short_press_2990`、`tests/unit/test_button.c::test_long_press_3000_on_reaching`、`tests/unit/test_button.c::test_release_after_long_is_not_short`、`tests/scenario/test_scenarios.c::test_ack_flow_silence_then_ack`、`tests/scenario/test_scenarios.c::test_ack_not_met_stays_critical`、`tests/scenario/test_scenarios.c::test_sensor_fault_early_ack_is_invalid`、`tests/scenario/test_scenarios.c::test_sensor_fault_recover_silence_then_ack`、`tests/scenario/test_scenarios.c::test_long_press_toggles_force_full`、`tests/scenario/test_scenarios.c::test_long_press_neither_silences_nor_acks` | [HW-14](hardware-phase.md#hw-14) 実物のボタンのチャタリング |
| FR-09 | TELEMETRY・HEARTBEAT 1秒、EVENT は状態変化時、STATE は起動直後と60秒ごと、seq は種別ごと | 単体・ゴールデン・契約・Renode | `tests/unit/test_report.c::test_telemetry_then_heartbeat`、`tests/unit/test_report.c::test_state_every_60s_after_last_state`、`tests/unit/test_report.c::test_one_second_from_init_exit`、`tests/unit/test_firmware.c::test_periodic_from_init_exit`、`tests/unit/test_firmware.c::test_state_every_60s`、`tests/unit/test_link.c::test_seq_per_type_assigned_on_send`、`tests/unit/test_link.c::test_seq_wraps_16bit`、`tests/golden/test_golden.c::test_encode_device_frames_match_hex`、`contract:frame_encoding`、`contract:seq_per_type`、`contract:converted_schema`、`tests/renode/thermal_guard.robot::周期` | [HW-11](hardware-phase.md#hw-11) 実機での契約テスト |
| FR-10 | CMD の受信、必ず ACK、安全側を弱める指令の丸めと拒否、reason、直近16件の重複 | 単体・ゴールデン・契約・Renode | `tests/unit/test_command_handler.c::test_fan_override_done`、`tests/unit/test_command_handler.c::test_fan_override_ranges`、`tests/unit/test_command_handler.c::test_fan_override_limited_by_state_floor`、`tests/unit/test_command_handler.c::test_threshold_unsafe_in_critical_and_fault`、`tests/unit/test_command_handler.c::test_threshold_ranges`、`tests/unit/test_command_handler.c::test_unknown_op`、`tests/unit/test_command_handler.c::test_get_status_sends_state_then_ack`、`tests/unit/test_command_handler.c::test_duplicate_returns_previous_ack_without_reexecuting`、`tests/unit/test_command_handler.c::test_cache_holds_16_and_evicts_oldest`、`tests/golden/test_golden.c::test_receive_path_for_upstream_frames`、`contract:ack`、`contract:duplicate_cmd_ref`、`tests/renode/thermal_guard.robot::指令と PWM` | [HW-11](hardware-phase.md#hw-11) 実機での契約テスト |
| FR-11 | IWDG 500ms、全タスクの生存報告がそろったときだけリフレッシュ | 単体・シナリオ・pytest・Renode | `tests/unit/test_health.c::test_all_present_refreshes_and_clears`、`tests/unit/test_health.c::test_one_missing_each`、`tests/unit/test_health.c::test_report_deadline_1500_inclusive`、`tests/scenario/test_scenarios.c::test_hang_watchdog_reboot`、`tests/host_integration/test_reboot.py::test_2_reboot_within_limits_with_new_boot_id`、`tests/renode/thermal_guard.robot::ウォッチドッグ` | [HW-08](hardware-phase.md#hw-08) IWDG の実際の時間 |
| FR-12 | INIT（最大2秒）、boot_id、自己診断、STATE → BOOT の順、ADC 異常なら SENSOR_FAULT、リセット要因は診断値とログ | 単体・シナリオ・契約・pytest・Renode | `tests/unit/test_firmware.c::test_no_frames_during_init`、`tests/unit/test_firmware.c::test_boot_order_state_boot_state_changed`、`tests/unit/test_firmware.c::test_cmd_during_init_dropped_heartbeat_accepted`、`tests/unit/test_alarm.c::test_init_pattern`、`tests/unit/test_boot_id.c::test_different_entropy_gives_different_id`、`tests/unit/test_boot_id.c::test_zero_becomes_one`、`tests/unit/test_reset_decode.c::test_power_on_sets_por_bor_pin`、`tests/unit/test_reset_decode.c::test_watchdog`、`tests/scenario/test_scenarios.c::test_init_adc_stall`、`contract:boot_order`、`tests/host_integration/test_reboot.py::test_1_first_frames_are_state_then_boot`、`tests/host_integration/test_reboot.py::test_4_log_boot_lines`、`tests/renode/thermal_guard.robot::起動` | [HW-09](hardware-phase.md#hw-09) リセット要因、[HW-10](hardware-phase.md#hw-10) `.noinit` の回数、[HW-13](hardware-phase.md#hw-13) boot_id の雑音 |
| FR-13 | 稼働時間・リセット回数・受信 CRC エラー数・最大ループ時間。GET_DIAG は ACK（done）だけ | 単体・pytest | `tests/unit/test_diag.c::test_collect_copies_all`、`tests/unit/test_diag.c::test_counters_saturate`、`tests/unit/test_diag.c::test_next_reset_count_saturates`、`tests/unit/test_command_handler.c::test_get_diag_ack_only`、`tests/unit/test_scheduler.c::test_measures_max_cycles`、`tests/unit/test_firmware.c::test_bad_frames_counted`、`tests/unit/test_firmware.c::test_bsp_diag_kept_separate`、`tests/host_integration/test_reboot.py::test_4_log_boot_lines` | [HW-07](hardware-phase.md#hw-07) 実機の診断値（DWT のサイクル数）をデバッガで読む |
| FR-14 | 上位の HEARTBEAT が10秒途絶えたら手動指定を解除して UPSTREAM_LOST、戻ったら RESTORED | 単体・契約 | `tests/unit/test_upstream.c::test_unknown_lost_10s_after_boot`、`tests/unit/test_upstream.c::test_unknown_heartbeat_to_alive_without_event`、`tests/unit/test_upstream.c::test_lost_heartbeat_restored`、`tests/unit/test_firmware.c::test_upstream_lost_and_restored`、`tests/unit/test_command_handler.c::test_clear_override_for_upstream_lost`、`contract:upstream_lost_restored` | [HW-11](hardware-phase.md#hw-11) 実機での契約テスト |

## 非機能要件（NFR）

| ID | 要件（要約） | 段 | 確かめているテスト | 実機フェーズで確認 |
| --- | --- | --- | --- | --- |
| NFR-01 | crit を超えてからファン100% まで 2.5秒以内 | シナリオ | `tests/scenario/test_scenarios.c::test_nfr01_1c_per_s`、`tests/scenario/test_scenarios.c::test_nfr01_10c_per_s`、`tests/scenario/test_scenarios.c::test_nfr01_40c_per_s`（実測は3通りとも約1.5秒） | [HW-05](hardware-phase.md#hw-05) GPIO のトグルで実測 |
| NFR-02 | 制御周期 100ms、ジッター ±1ms | 単体・Renode | `tests/unit/test_scheduler.c::test_period_does_not_drift`、`tests/unit/test_scheduler.c::test_overrun_runs_once_and_keeps_phase`、`tests/unit/test_scheduler.c::test_late_but_within_period_is_not_overrun`、`tests/renode/thermal_guard.robot::周期`（仮想時間での周期だけ。ジッターの証拠にはならない） | [HW-06](hardware-phase.md#hw-06) ジッターの実測 |
| NFR-03 | ISR は各 20μs 以内、フラグとバッファだけ | ターゲット・Renode | `ci:target`（`tools/isr_report.py`：ループなし、命令数からの見積もり）、`ci:renode`（target-renode の ISR）、レビュー表 [docs/isr-review.md](isr-review.md) | [HW-07](hardware-phase.md#hw-07) DWT と GPIO のトグルで実測 |
| NFR-04 | malloc なし、Flash 64KB・RAM 16KB 以内（スタック含む） | ターゲット・品質ゲート・Renode | `ci:target`（`tools/check_size.py`、`tools/stack_report.py`）、`ci:quality`（`tools/check_forbidden.sh`）、`tests/renode/thermal_guard.robot::スタック`、`ci:renode`（`tools/renode-stack-margin.sh`） | [HW-12](hardware-phase.md#hw-12) 実機の stack_peak |
| NFR-05 | 整数・固定小数だけ（浮動小数点なし） | 品質ゲート | `ci:quality`（`tools/check_forbidden.sh`：app・service・lib に float・double がない） | — |
| NFR-06 | ハード依存は bsp/ に閉じ込め、app・service は STM32 のヘッダを参照しない | 品質ゲート・ターゲット | `ci:quality`（`tools/check_forbidden.sh`：STM32・CMSIS の include は bsp/stm32f4 だけ）、`ci:host`（同じソースでホスト版をビルド）、`ci:target` | — |
| NFR-07 | C11、MISRA C:2012（逸脱は ADR）、警告ゼロ | 品質ゲート・ターゲット | `ci:quality`（`tools/quality.sh`：cppcheck の MISRA、clang-tidy）、`ci:host`（-Werror）、`ci:target`（`tools/target.sh`：-Werror）。逸脱は [ADR-0002](adr/ADR-0002-misra-deviations.md) | — |
| NFR-08 | app・service の行カバレッジ 80% 以上 | 品質ゲート | `ci:quality`（`tools/quality.sh`：gcovr） | — |
| NFR-09 | CMake＋arm-none-eabi-gcc、同じソースツリーからプリセットで切り替え | ターゲット | `ci:host`（host-debug）、`ci:quality`（host-coverage）、`ci:target`（target-debug・target-release）、`ci:renode`（target-renode） | — |
| NFR-10 | 公開関数の Doxygen、循環的複雑度 10 以下 | 品質ゲート | `ci:quality`（`tools/quality.sh`：doxygen、lizard） | — |

「—」の行は、ホスト・CI の検査で要件そのものを確かめられる（実機で値が変わらない）もの。

## 受け入れ基準（基本設計書 第7章）

- [x] **FR-01〜12 がすべてテスト仕様書の項目に紐付き、ホスト結合テストで合格している**：上の FR の表。どの FR にもホストの結合テスト（シナリオ・契約・pytest）が1件以上ある。FR-01・02 は `tests/scenario/test_scenarios.c::test_nfr01_10c_per_s`（温度の経路の全体）、FR-03 は故障のシナリオ、FR-04 は `test_warning_hysteresis`、FR-05・06 は NFR-01 のシナリオ、FR-07・08 は承認・長押しのシナリオ、FR-09・10・12 は契約テスト、FR-11 は `test_hang_watchdog_reboot` と pytest。CI の host ジョブで合格
- [x] **Renode 上で起動から TELEMETRY 送信、指令と ACK までが自動テストで通る**：`tests/renode/thermal_guard.robot` の「起動」「周期」「指令と PWM」と、Renode の契約テスト（7チェック）。CI の renode ジョブで合格（[ADR-0006](adr/ADR-0006-renode.md)）
- [x] **NFR-04・06・07・08 を CI で確認できる**：上の NFR の表。target（サイズ・スタック）と quality（禁止項目・MISRA・clang-tidy・カバレッジ）のジョブ
- [x] **ホスト版に対して iotspec-contract --role mcu-c-01（v0.2.1）の全チェックが合格し、CI でも実行されている**：CI の host ジョブの「契約テスト」。7チェック（`contract:boot_order`・`contract:ack`・`contract:duplicate_cmd_ref`・`contract:upstream_lost_restored`・`contract:frame_encoding`・`contract:seq_per_type`・`contract:converted_schema`）
- [x] **uart/golden/ の全件で、符号化と復号が一致する単体テストが通る**：`tests/golden/test_golden.c::test_encode_device_frames_match_hex`、`tests/golden/test_golden.c::test_decode_all_match_frame`、`tests/golden/test_golden.c::test_crc_mismatch_vectors_fail`、`tests/golden/test_golden.c::test_count_matches_generation`（ゴールデンの件数が変わったら失敗）

## Renode と実機の違い（テストの読み方）

`target-renode` は実機と ADC の開始方法が違う（TIM2 の更新割り込みからのソフトウェア開始。実機は TIM2 の TRGO）。DMA は循環・半分転送を足したモデルで動かしている。Renode の合格は「同じソースのファームウェアが、Renode のモデルの上で期待どおりに動く」ことの証拠で、ISR の時間・ジッター・波形・リセット要因の証拠ではない。範囲は [ADR-0006](adr/ADR-0006-renode.md) の「Renode で確かめた範囲と、確かめていない範囲」と、[hardware-phase.md の HW-15](hardware-phase.md#hw-15)。
