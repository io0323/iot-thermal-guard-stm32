# 作品01 フェーズ P6：Renode

## P5 の片付け（先に済ませる）

PR #5 はレビュー済みで、io がマージする。マージを確かめてから `phase/p5-target` をローカルとリモートで削除し、main から `phase/p6-renode` を切ること。

## はじめに読むもの

`CLAUDE.md`、`docs/open-questions.md`、`docs/isr-review.md`、ADR-0004・ADR-0005、PR #5 の説明にある P6 への申し送り、要件定義・基本設計書の第7章（テスト方針・受け入れ基準）、詳細設計書の第9章と第10章の P6 の行、iot-common-spec の `docs/contract-tests.md`（`--uart` と `--target-cmd`、`needs_target_control` で省略されるチェック）。読み終えたら「作るもの・作らないもの・不明点」を短く報告してから始める。

## 進め方：最初に試行（spike）、その結果を見てから本作業

本作業の前に、次の4点を短い試行で確かめ、結果を `docs/adr/ADR-0006-renode.md` の下書きと PR の説明に書く。**試行の結果を io に報告し、返事を待ってから本作業に入ること。**

1. **Renode の入手と版**：手元も CI も `antmicro/renode` の Docker イメージ（版を固定）で動かせるか。この Mac ではツールをコンテナで動かす前提（P4・P5 と同じ）
2. **プラットフォーム定義**：`renode/nucleo_f401re.repl` を Renode 同梱の `stm32f4.repl` をもとに作れるか。メモリを F401RE（Flash 512K、RAM 96K）に合わせ、`Analog.STM32_ADC` を 0x40012000 に足す。target-release の .elf が起動して、`SystemClock` の設定（HSI・PLL・Flash の待ち）でクロックの準備完了フラグを待ったまま止まらないか。止まるなら、**bsp のコードを変えずに** repl の tag か Python のペリフェラルで準備完了のビットを返す方法を試す
3. **周辺機能**：PR #5 の申し送りの5点（ADC の外部トリガー、DMA の半分転送、クロックの準備完了フラグ、`.noinit` がリセットをまたいで残るか、チップ内部の材料）について、Renode のモデルで動くもの・動かないものを1つずつ確かめる。ADC の外部トリガーは動かない見込み（設計書のとおり）なので、`target-renode` で使う「TIM2 の更新割り込みから ADC をソフトウェアで開始する」経路（`ADC_SOFTWARE_START=1`）で、DMA の半分転送・全転送が今までどおり動くかを確かめる
4. **UART と契約テスト**：Renode の USART2 をホストの pty に出し（`emulation CreateUartPtyTerminal`）、iotspec の契約テストをつなげるか。`--target-cmd` で Renode を起動するラッパー（`tools/renode-contract.sh {uart}`）を作り、契約テストが作った pty と Renode の pty を socat などで中継すれば、`boot_order` を含む7チェックすべてを流せる見込み。あわせて、Renode の仮想時間が実時間とどれだけずれるか（契約テストの2秒・10秒のタイムアウトに収まるか）と、実時間に合わせる設定を確かめる

## 本作業で作るもの（試行の結果で io が範囲を決める）

1. **`target-renode` プリセット**：出力先 `build/target-renode`。target-release と同じソースで、違いは `ADC_SOFTWARE_START=1` だけ（TIM2 の更新割り込みが ADC をソフトウェアで開始する。DMA と平均は共通）。この経路は Renode 用で実機とは違うことを、コードのコメント・README・ADR-0006・`docs/isr-review.md`（Renode 用ビルドだけにある TIM2 の ISR）に書く
2. **`renode/nucleo_f401re.repl` と `renode/thermal_guard.resc`**
3. **テスト（renode-test、Robot Framework）**：`tests/renode/`。フレームはバイナリなので、pty を読んで iotspec の `FrameReader`・`decode_frame` で復号する小さな Python のキーワードライブラリを `tests/renode/` に置く（pytest の結合テストの手順を参考にする）。確かめること：
   - 起動：最初のフレームが STATE、次の EVENT が BOOT
   - 周期：TELEMETRY と HEARTBEAT が仮想時間で1秒ごと
   - 指令：SET_FAN_OVERRIDE 400 に ACK(done・applied 400) が返り、TIM3 の CCR1 が 1344（400 × 3360 ÷ 1000）になる
   - 温度：ADC の値を `FeedSample` で 25℃ の生値から 65℃ 相当まで変化率の上限より遅く上げると、CRITICAL に入り CCR1 が 3360 になる
   - ウォッチドッグ：ファームウェアのループを止めると IWDG がリセットし、STATE → BOOT が送られ直す。ループを止める方法は、`target-renode` だけにある変数を Renode のモニタから書き換える形にする（`sysbus GetSymbolAddress` で番地を引く）。target-debug・target-release にはこの変数を入れない
4. **契約テストを Renode で**：試行4が成り立てば、`tools/renode-contract.sh` を使って7チェックを流す CI の手順を足す。成り立たなければ、その理由と、`--uart` で流せた分（`boot_order` を除く6チェック）の結果を ADR-0006 に書く
5. **CI**：`renode` ジョブ（target-renode をビルド → renode-test → 成り立てば契約テスト → ログと結果を成果物に）
6. **書類**：ADR-0006（Renode の版、プラットフォーム定義、モデルで動くもの・動かないもの、ADC の開始方法の違い、時間の扱い、ウォッチドッグのテストの方法）。README と基本設計書の写しのテスト方針に、Renode で確かめた範囲と確かめていない範囲を書く

## P6 で作らないもの

実機での確認、Wokwi、新しい機能。app・service・ports は変えない。bsp/stm32f4 の変更は `ADC_SOFTWARE_START` とウォッチドッグのテスト用の変数（どちらも target-renode だけ）に限る。

## 完了条件（すべて満たしてから PR）

1. renode-test の5項目（起動・周期・指令と PWM・温度と CRITICAL・ウォッチドッグ）が手元のコンテナと CI で合格
2. 契約テスト：Renode で7チェックすべて合格、または、成り立たない理由と `--uart` で流せた分の結果が ADR-0006 にある
3. ADC の開始方法が実機と違うことが、コード・README・ADR-0006・`docs/isr-review.md` に書かれている
4. target-debug・target-release のサイズと警告ゼロ、ホスト側（CTest・契約テスト・pytest・品質ゲート）がすべて変わらず合格

PR の説明には、完了条件ごとの結果、Renode の版、モデルで動いたもの・動かなかったものの表、仮想時間と実時間のずれ、open-questions に足した項目、P7 への申し送りを書く。マージはしない。

## 止まって相談すること

- 試行が終わったとき（必ず報告して返事を待つ）
- Renode で動かすために、`ADC_SOFTWARE_START` とテスト用の変数以外で bsp・app・service・ports を変える必要が出たとき
- Renode のモデルの不足を補うために、Renode 本体（C#）の改造が必要になりそうなとき
- 設計書に値や順序がないことを決める必要が出たとき

いずれも open-questions に記録し、その件に関わる作業を止めて報告する。
