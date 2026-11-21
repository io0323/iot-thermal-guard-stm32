# ADR-0006 Renode

- 状態：採用（2026-11-21）
- 関係する書類：要件定義・基本設計書 第7章（テスト方針・受け入れ基準）、詳細設計書 第3章・第9章・第10章、ADR-0004・ADR-0005、`docs/isr-review.md`、`renode/README.md`、open-questions #52〜#56

## 状況

P6 では、STM32 のターゲット版を Renode で動かし、起動・周期送信・指令と PWM・温度・ウォッチドッグを Robot Framework で確かめる。あわせて契約テスト（iotspec-contract）を Renode につなぐ。本作業の前に試行を行い、その結果（下の「試行で分かったこと」）を io に報告して範囲を決めた（#52〜#56）。

## 決定

### Renode の版と動かし方

| 項目 | 内容 |
| --- | --- |
| 版 | `antmicro/renode:1.16.1`（Renode v1.16.1.16973、build d66b0c2a）。digest `sha256:fc2a8c1bad2296a6d7cbc852bbf5540b22b778bdeb0ad42a45b8c54ea1e6a24c` に固定（Docker Hub の版付きのタグで最新。GitHub の v1.17.0 は Docker Hub に版付きのタグがない） |
| 手元 | Apple M3（arm64）の Mac で、x86-64（linux/amd64）のイメージを Docker のエミュレーション（Rosetta）で動かす。CI（ubuntu-24.04、amd64）はそのまま動く。仮想時間と実時間の比は手元と CI でほぼ同じだった（遅さはエミュレーションではなく、Renode が動かす命令と出来事の量で決まる） |
| SVD | 同梱の `stm32f4.repl` は起動のたびに `STM32F40x.svd.gz` を取る。モニターを TCP で開く起動（`-P`）ではこの取得が止まったので、Renode の設定ディレクトリを Docker のボリューム（`tg-renode-config`）に置き、`--console` の起動で一度だけ取ってから使い回す（`tools/renode-test.sh`） |
| スクリプト | `tools/renode-test.sh`（Robot のテスト）、`tools/renode-contract.sh`（契約テストの `--target-cmd`、`--measure` で比だけを測る）、`tools/renode_uart_relay.py`（pty と USART2 の中継）、`tools/renode-stack-margin.sh`（#54 の確かめ）。CI の renode ジョブも同じものを呼ぶ |

### プラットフォーム定義（`renode/`）

- `nucleo_f401re.repl`：同梱の `stm32f4.repl` を `using` し、Flash 512K・RAM 96K、Flash の 0x00000000 の別名（リセット後のベクタ表）、SysTick・TIM2・TIM3 を 84MHz、ADC1（0x40012000、EOC は nvic@18）、DWT（84MHz）を足す
- `dma2_circular.repl`：DMA2 を下のモデルに置き換え、ADC1 の DMA の要求を Stream0 につなぐ
- `thermal_guard.resc`：モデルを読み込み（`include`）、同梱の dma2 を sysbus から外して上の2つを読み、.elf を載せ、ボタン PC13 を High にする（基板のプルアップの代わり。しないと 0＝押されたままと読まれ、3秒で長押しになる）

### DMA のモデル（#55）

| 項目 | 内容 |
| --- | --- |
| ファイル | `renode/peripherals/STM32DMA_Circular.cs`（クラス `STM32DMA_Circular`） |
| 元 | Renode v1.16.1（renode-infrastructure `add012af003a0f620d3da52828262676f374d121`）の `src/Emulator/Peripherals/Peripherals/DMA/STM32DMA.cs`（MIT、Antmicro） |
| 読み込み | Renode 本体は変えない。`thermal_guard.resc` の `include` で起動時にコンパイルし、dma2 を置き換える |
| 差分 | クラス名の置き換えを除いて、足した行 75・消した行 8。変更した行には `thermal-guard` と書いた |
| ライセンス | ファイルの先頭に元の著作権の表示と出典、`renode/README.md` に MIT の全文と出典 |

変更した箇所：

1. **半分転送**：HTIF（LISR・HISR）を読める旗に、CHTIF（LIFCR・HIFCR）で消せるようにし、HTIE を有効にできるようにした。転送数（NDT）が有効にした時点の半分になったら HTIF を立てる
2. **循環**：CIRC を有効にできるようにし、全転送（NDT が 0）のあと、循環なら NDT を有効にした時点の値に戻して続ける（有効にした時点の NDT を覚える）
3. **割り込み**：Stream の割り込みを「TCIE かつ TCIF」または「HTIE かつ HTIF」にした
4. **周辺機能の読み出しの幅**：周辺機能からメモリへは、周辺機能のレジスタを 32 ビットで読み、MSIZE の幅で書く。Renode の `STM32_ADC` は 32 ビットのアクセスだけを受け、PSIZE が半語の読み出しは 0 になる（本作業で、DMA のバッファがすべて 0 になって見つけた。実機の ADC_DR は半語で読める）。ほかの向きは元のまま `DmaEngine` で写す
5. **テスト用の回数**：`GetHalfTransferCount(stream)`・`GetTransferCompleteCount(stream)`（Robot のテストがモニターから呼ぶ）

TEIF・DMEIF・FEIF・二重バッファ（DBM）は元のまま（タグだけ）。作品01は使わない（転送エラーは temp_sensor の無応答の検出で拾う）。

### ADC の開始方法（実機との違い）

Renode の `STM32_ADC` は外部トリガーを持たないので、`target-renode`（`build/target-renode`、#52）は `ADC_SOFTWARE_START=1` で、TIM2 の更新割り込み（`TIM2_IRQHandler`、優先度 2、#53）から ADC をソフトウェアで開始する。実機（target-debug・target-release）は TIM2 の TRGO で起動し、この ISR を持たない。DMA の転送と、確定した半分の平均は共通のコード。コード（`bsp/stm32f4/bsp_adc.c`）・README・`docs/isr-review.md` にも書いた。

### テスト（`tests/renode/thermal_guard.robot`）

時間の設定は Renode の既定のまま（PerformanceInMips・時間の刻み・TIM3 84MHz）。仮想時間で判定するので、実時間の速さに左右されない。

| テスト | 確かめること |
| --- | --- |
| 起動 | 最初のフレームが STATE、次の EVENT が BOOT（どちらも seq 0）。3秒後に NORMAL |
| 周期 | TELEMETRY・HEARTBEAT の uptime_ms の間隔がすべて 1000ms |
| DMA（#55 ②） | 2秒で半分転送・全転送が各9回以上（循環で止まらない）、交互に来る、100ms ごとに割り込みがちょうど1回、NDT が 0 で止まらない。そのうえで NORMAL（温度が ready になる） |
| 指令と PWM | SET_FAN_OVERRIDE 400 に ACK（done・applied 400）、TIM3 の CCR1 が 1344（400 × 3360 ÷ 1000） |
| 温度と CRITICAL | ADC に 25℃ から 65℃ まで 0.5℃ ずつ（1値は変換10回＝100ms 分）与えると CRITICAL、CCR1 が 3360 |
| ウォッチドッグ | `g_renode_hang` に 1 を書くと IWDG がリセットし、STATE → BOOT（seq 0 から）を送り直す |
| スタック（#54） | 塗りの残りから最大使用量（stack_peak と同じ計算）を求めてログに出す |

フレームはバイナリなので、`tests/renode/ThermalGuardUart.py` が USART2 のソケット端末につなぎ、iot-common-spec v0.2.1（submodule）の `FrameReader`・`decode_frame`・`encode_frame` で復号・符号化する。これらは標準ライブラリだけに依存するので、Renode のイメージ（Python 3.10.12）にインストールせず submodule の src を読み込む。

**ウォッチドッグのテストの変数**：`g_renode_hang`（`bsp/stm32f4/main_stm32f4.c`）は `RENODE_TEST_HOOKS=1`（target-renode だけ、CMake の `TG_RENODE_TEST_HOOKS`）のときだけある。モニターの `sysbus GetSymbolAddress "g_renode_hang"` で番地を引いて書く。.bss なのでリセットで 0 に戻る。

### 契約テストを Renode で（#56）

- `iotspec-contract --role mcu-c-01 --target-cmd "tools/renode-contract.sh {uart}"` で7チェックすべてを流す（`--target-cmd` で起動するので `boot_order` も省略されない）
- USART2 を TCP のソケット端末に出し、`tools/renode_uart_relay.py` が契約テストの pty とつなぐ（Renode はコンテナの中なので pty を外に出さない）。ソケット端末はつながっていない間の出力を捨てるので、Renode は start せずに待たせ、モニターに機械の名前のプロンプトが出てから USART2 につなぎ、モニター（telnet、行末は CR LF）に start を送る
- 温度は 25℃ の生値を与え続ける（契約テストは NORMAL を前提にする）
- **速い設定は契約テストのときだけ**：TIM3 を 1MHz（Renode では PWM の波形を見ないので CCR の値には影響しない）、時間の刻み（global quantum）1ms、PerformanceInMips 10
- **仮想時間と実時間の比**：契約テストは実時間で待つので、比が約0.7倍（上位断の10秒を14秒以内に）〜約9倍（1秒ごとの HEARTBEAT の間が10秒を超えない）の間にある必要がある。CI は契約テストの前に `tools/renode-contract.sh --measure 20` で比を測り、範囲の外なら失敗させる。契約テストの最中も、中継が10秒ごとと終わるときに比をログ（`contract-relay.log`）に出す

### 仮想時間と実時間

20秒の仮想時間にかかった実時間（起動の数秒を除く、試行の測定）：

| 設定 | 手元（エミュレーション） | CI（amd64） |
| --- | --- | --- |
| 既定（PerformanceInMips 100） | 約182秒（0.11 倍） | 約184秒（0.11 倍） |
| MIPS 10 | 約44秒（0.45 倍） | 約40秒（0.49 倍） |
| MIPS 2 | 約25秒（0.79 倍） | — |
| TIM3 1MHz・時間の刻み 1ms・MIPS 10（契約テストの設定） | 約18秒（1.1 倍） | 0.88 倍（`--measure 20`） |

TIM3（PWM 25kHz）の周期ごとの出来事が重く、MIPS を下げるだけでは頭打ちになる。Renode には実時間に合わせる設定がない。

### Renode で確かめた範囲と、確かめていない範囲

| 周辺機能・項目 | Renode | 備考 |
| --- | --- | --- |
| クロックの設定（HSI・PLL・Flash の待ち） | 動く（準備完了で止まらない） | `STM32F4_RCC` は ON の値を準備完了として返す。周波数そのものは確かめていない |
| SysTick 1ms・DWT | 動く | DWT は repl に足した。命令数ベースの近似 |
| TIM2 → ADC → DMA（循環・半分転送） | 動く（ADC の開始は TIM2 の割り込みからのソフトウェア開始、DMA は足したモデル） | 実機の TRGO の経路は確かめていない |
| 温度の変換・フィルタ・状態遷移 | 動く | ADC の値は `FeedSample` で与える |
| TIM3 の PWM | CCR の値まで | 周波数・duty の波形は確かめていない |
| USART2 の送受信 | 動く（契約テスト7チェック） | ボーレートの誤差・ORE は確かめていない |
| IWDG | 動く（ループを止めるとリセット） | LSI の個体差（340〜940ms）は確かめていない |
| `.noinit` | リセットをまたいで残る | — |
| RCC_CSR（リセット要因） | **固定値**（いつも 0x0E000000＝POR・PIN・BOR） | リセット要因はいつも power_on、回数は 0。判定はホストの単体テスト（`test_reset_decode`） |
| UID・内部温度・VREFINT | UID は 0、内部温度・VREFINT は与えなければ 0 | boot_id の雑音は確かめていない（「再起動で変わる」はホスト版で確かめる、設計書のとおり） |
| ISR の実行時間・ジッター | 確かめない | 命令数の近似で、時間の証拠にならない。実機フェーズで実測（NFR-02・03） |
| ボタン（PC13） | High に固定 | 押す操作は Renode のテストに入れていない（ホストのシナリオで確かめている） |

## 試行で分かったこと（本作業の前の報告の要約）

- 同梱の `stm32f4.repl` だけでは、DWT がなく起動の待ちで止まり、Flash の 0x0 の別名がなく IWDG のリセットのあとに止まった（どちらも repl で直した）
- P5 の `bsp_stack_paint` を GCC が `memset` の呼び出しに置き換え、`memset` が自分の戻り番地を塗って 0xA5A5A5A4 に飛んでいた。P5 では実行していなかったので捕まえられず、Renode が見つけた（#54。volatile で書き、塗る範囲を今の SP の 64 バイト下までにした。余白 0 の版と stack_peak が同じことを `tools/renode-stack-margin.sh` で確かめる）
- 同梱の `STM32DMA` は循環・半分転送を持たず（v1.17.0・master も同じ）、温度が ready にならなかった（#55）
- 既定の時間の設定では仮想時間が実時間の約0.11倍で、契約テストの上位断の判定が成り立たなかった（#56）

## 理由

- DMA のモデルをリポジトリに持つのは、Renode 本体を変えずに、実機と同じ DMA のコード（循環・半分転送・確定した半分の平均）を Renode で動かすため。元のファイルとの差分を小さく保ち、足したモードを Robot のテストで直接確かめる
- 速い設定を契約テストだけにするのは、Robot のテストを Renode の既定（ADR-0005 の 84MHz など）のまま仮想時間で判定し、契約テストだけが実時間の制約を持つため
- ウォッチドッグのテストの変数を target-renode だけにするのは、実機のビルドにテスト用の抜け道を残さないため

## 影響

- `target-renode` の ADC の経路は実機と違う。Renode で合格しても、実機の TRGO の経路と ADC のタイミングは実機フェーズで確かめる
- 契約テストの比（CI で 0.88〜0.98 倍）は下限（0.7）に近い。CI のマシンが遅くなると `--measure` の段で落ちる。そのときは PerformanceInMips を 10 から下げる（1命令あたりに進む仮想時間が増え、比が上がる。上の表では既定の設定で MIPS 10 が 0.45 倍、2 が 0.79 倍。TIM3 1MHz・時間の刻み 1ms と組み合わせればさらに上がる見込み）。環境変数 `RENODE_MIPS` で試し（`RENODE_MIPS=5 tools/renode-contract.sh --measure 20`）、CI で変えるなら `tools/renode-contract.sh` の既定を変えて、上の表に測った比を書き足す（P7、README の「仮想時間の比が 0.7 を下回って契約テストが落ちたとき」）
- Renode の版を上げるときは、DMA のモデルの元のファイルとの差分を見直す（上流が循環・半分転送を入れたら、このモデルは要らなくなる）
- 同梱の `stm32f4.repl` の SVD の取得は外部（dl.antmicro.com）に依存する。キャッシュのボリュームがあれば2回目からは取らない
