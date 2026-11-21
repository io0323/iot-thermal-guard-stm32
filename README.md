# iot-thermal-guard-stm32

作品01「高機能センサー制御・安全監視デバイス」のファームウェアです（C11、STM32F401RE、ベアメタル）。
温度を監視してファンを制御し、異常を知らせます。上位（作品03）とは UART でやり取りし、
フレームは [iot-common-spec](https://github.com/io0323/iot-common-spec) v0.2.1 の第7章に従います。device_id は `mcu-c-01` です。

いま動くのは PC 上のホスト版（`build/host/mcu_c_01`）です。実機は使わず、単体テスト・ゴールデン・仮想時間のシナリオ・契約テストで確かめます。
設計は [docs/design/](docs/design/)、未決の論点と決定は [docs/open-questions.md](docs/open-questions.md) にあります。

## 必要なもの

- CMake 3.25 以上、Ninja、C11 のコンパイラ（clang または gcc）、Python 3
- 契約テストを動かすには [uv](https://docs.astral.sh/uv/)

## ビルドとテスト

```sh
git submodule update --init --recursive
cmake --preset host-debug && cmake --build --preset host-debug   # ASan・UBSan 付き、警告はエラー
ctest --preset host-debug --output-on-failure                     # 単体テスト・ゴールデン・シナリオ
./build/host/tests/test_scenarios                                 # シナリオの実測値（NFR-01 など）を表示
```

## 温度の変換表

`tools/gen_ntc_table.py` がビルド時に `build/generated/ntc_table_data.h` を作ります（生成物は手で編集しない）。
回路は 3.3V → 10kΩ プルアップ → ADC → NTC（10kΩ、B=3435）→ GND、12ビットで、生値 = round(4095 × R ÷ (R + 10kΩ))。

| 温度 | 生値 |
| --- | --- |
| −20℃（表の端） | 3627 |
| 25℃ | 2048 |
| 120℃（表の端） | 238 |

生値 50〜237・3628〜4045 は 120.00／−20.00℃ に丸め、50 未満・4045 超が3回続いたら短絡・断線とします。

## 契約テスト

```sh
uv tool install "iotspec @ git+https://github.com/io0323/iot-common-spec@v0.2.1"
iotspec-contract --role mcu-c-01 --target-cmd "./build/host/mcu_c_01 --log contract-log.jsonl {uart}" --junit contract.xml
```

7つのチェック（boot_order、ack、duplicate_cmd_ref、upstream_lost_restored、frame_encoding、seq_per_type、converted_schema）を行います。
10秒の途絶を含むので、1回におよそ15秒かかります。MQTT ブローカーは要りません。

## ホスト版の使い方

```text
mcu_c_01 [--temp <℃>] [--scenario <file.csv>] [--log <file.jsonl>] [UART_PATH]
```

| 引数 | 既定 | 内容 |
| --- | --- | --- |
| `UART_PATH` | 環境変数 `IOTSPEC_UART_PATH` | 開くシリアル（raw・115200・8N1）。両方なければ終了コード 2 |
| `--temp` | 25.0 | 固定の温度。上の式で ADC の生値に戻して渡す |
| `--scenario` | なし | シナリオ CSV を実時間で再生する。`--temp` より優先。不正な CSV は終了コード 2 |
| `--log` | 標準エラー | JSON Lines のログ |

- 環境変数 `MCU_C_01_RESET_REASON` でリセット要因を指定できます（`power_on`・`pin`・`watchdog`・`software`・`brownout`・`low_power`・`unknown`。既定は `power_on`）
- SIGTERM・SIGINT、または相手が UART を閉じたら、診断値をログに書いて終了コード 0 で終わります。SIGUSR1 で診断値を書き出します

### シナリオ CSV

`t_ms,temp_c,event` の3列です（例は [tests/scenario/data/](tests/scenario/data/)）。仮想時間のテスト（sim_harness）とホスト版で同じ形式を使います。

- 温度は次の行まで線形につなぎ、最後の行のあとは最後の温度のまま。t_ms は起動からのミリ秒で、増える順
- 1行目が `t_ms` で始まればヘッダとして読み飛ばす。`#` の行と空行は読み飛ばす。改行は LF・CRLF のどちらでもよい
- temp_c は全行に必須。event は空か、`sensor_open`（生値 4095）・`sensor_short`（生値 0）・`sensor_ok`（温度からの値に戻す）・`adc_stall`（値を出さない）・`adc_resume`
- `button_down`・`button_up`・`hang` は P3 で扱うので、今は未対応として読み込みを失敗させる

### ログのキー（JSON Lines）

すべての行に `t_ms`（起動からのミリ秒）と `kind` があります。

| kind | キー |
| --- | --- |
| `boot` | `boot_id`（16進8桁の文字列）、`reset_reason`、`reset_count` |
| `state` | `from`、`to`（状態名） |
| `fan` | `pm`（PWM に書いた duty、‰） |
| `tx`・`rx` | `type`（種別名）、`seq`、`fields`（下表）、`hex`（符号化済みのバイト列。末尾の `00` を含む） |
| `rx_error` | `reason`（`cobs_error`・`length_mismatch`・`crc_mismatch`・`unexpected_type`・`rx_overflow`）、`hex` |
| `diag` | `reset_reason`、`reset_count`、`rx_cobs_errors`、`rx_len_errors`、`rx_crc_errors`、`rx_unexpected`、`rx_overflow`、`cmd_dropped_init`、`cmd_duplicates`、`tx_dropped`、`temp_clamped`、`temp_rejected`、`max_loop_cycles`、`max_task_cycles`（sense・control・link・ui・report・health の順の配列）、`overruns`。ホスト版の cycles はナノ秒 |

| type | `fields` のキー |
| --- | --- |
| TELEMETRY | `uptime_ms`、`temp_c100`、`quality`、`state`、`fan_pm` |
| EVENT | `uptime_ms`、`code`、`severity`、`from`、`to`、`temp_c100` |
| HEARTBEAT | `uptime_ms` |
| STATE | `fw_version`、`boot_id`（16進8桁の文字列）、`period_ms` |
| CMD | `cmd_ref`、`op`、`ttl_s`、`args`（16進の文字列） |
| ACK | `cmd_ref`、`status`、`reason`、`limited`、`applied` |

## 依存しているもの（submodule）

| パス | 版 |
| --- | --- |
| `external/iot-common-spec` | タグ `v0.2.1` |
| `third_party/unity` | ThrowTheSwitch/Unity タグ `v2.7.0` |

## フェーズの進み具合

| フェーズ | 内容 | 状態 |
| --- | --- | --- |
| P1 | ホスト版の骨組み・UART・契約テスト | 完了 |
| P2 | 温度計測と状態遷移 | 実装済み（PR 中） |
| P3 | ボタン・警報・ヘルス | 未着手 |
| P4 | 品質ゲート | 未着手 |
| P5 | STM32 ターゲット版 | 未着手 |
| P6 | Renode | 未着手 |
| P7 | 書類の仕上げ | 未着手 |

button・alarm・health（ボタンの承認、LED・ブザー、ウォッチドッグ）は P3 で作ります。今は承認が入らないので、CRITICAL と SENSOR_FAULT からは戻りません。
