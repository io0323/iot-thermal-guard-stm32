# 作品01 フェーズ P3：ボタン・警報・ヘルス

## P2 への回答（先に片付ける）

PR #2 はレビュー済みで、io がマージする。マージを確かめてから `phase/p2-temp-fsm` をローカルとリモートで削除し、main から `phase/p3-ui-health` を切ること。

open-questions #30〜#33 は、どれも案 A で「決定（2026-11-21）」にする。#33 は P3 で button・hang を扱えるようにするので、「P2 時点の決定。P3 で button・hang を読めるようにした」と書き足す。

`docs/design/02-detailed-design.md` の写しを次の3か所直す（原本は更新済み）。
1. 第5章「ボタン」の「短押しの意味：ブザーが鳴っていて消音していなければ」の直後に「（CRITICAL・SENSOR_FAULT で消音していない間。断続の音が止まっている瞬間も含む）」を入れる
2. 第7章「ウォッチドッグ」の「report の最後の実行が1.5秒以内のときだけ」を「report の最後の実行（まだなければ INIT を抜けた時刻）から1.5秒以内のときだけ」にする
3. 第8章のシナリオの表、`hang` の意味を「ファームウェアのループを止める（無限ループの模擬。疑似ウォッチドッグの監視だけは続ける）」にする

## はじめに読むもの

`CLAUDE.md`、`docs/open-questions.md`、詳細設計書の第2章（起動の順序・INIT）、第5章（警報・ボタン）、第7章（ヘルス・リセット・boot_id・診断）、第8章（ホスト版）、第9章、第10章の P3 の行。iot-common-spec の `src/iotspec/uart/frames.py`（`decode_frame`・`FrameReader`）。読み終えたら「作るもの・作らないもの・不明点」を短く報告してから始める。

## P3 で作るもの

1. **button**（service、第5章）：task_ui が10ms ごとに `gpio->button_pressed()` を読む。同じレベルが2回（20ms）続いたら確定。短押しは3000ms 未満で離したとき、長押しは押し続けて3000ms に達した時点（離すのを待たない）。長押しのあとに離しても短押しにしない。イベントは4件のキュー
2. **短押しと長押しの意味**（firmware）
   - 短押し：ブザーが鳴っていて消音していなければ「消音」、それ以外は「承認」として次の fsm_step に1回だけ渡す（当たる行がなくてもそのステップで消費する）。「鳴っている」は上の修正1の定義に従う
   - 長押し：強制全開を切り替え、出力をすぐ計算し直す。再起動で解除され、上位断では解除しない
3. **alarm**（app、第5章の表）：状態ごとの LED とブザーのパターン。点滅の位相は状態に入った時刻が起点。消音は CRITICAL か SENSOR_FAULT に新しく入ったときに解除。INIT は 赤→黄→緑 を100ms ずつ、ブザーは最初の100ms だけ鳴らし、そのあと3色とも消灯。強制全開中、NORMAL なら緑を 2Hz で点滅
4. **health**（service、第7章）：sense・control・link・ui が実行の最後に生存報告し、task_health（100ms）が全員の報告と report の期限（上の修正2）を確かめてから `wdt->refresh()` を呼ぶ。INIT の間は起動処理が直接リフレッシュする
5. **疑似ウォッチドッグとリセット（bsp_host）**：最後のリフレッシュから500ms たったら、診断値をログに書き、環境変数 `MCU_C_01_RESET_REASON=watchdog` と `MCU_C_01_RESET_COUNT=<回数+1>` を付けて、同じ引数で自分自身を `execv()` する。UART は `O_CLOEXEC` で開き、起動し直したプロセスが開き直す。環境変数がなければ power_on・回数 0。ログには boot の行で reset_reason と reset_count を出す
6. **diag**（第7章の表）：診断値を1か所に集め、飽和させる。ホスト版は終了時と SIGUSR1 でログに書く。GET_DIAG はこれまでどおり ACK(done) だけ（CR-1 待ち）
7. **シナリオの event**：`button_down`・`button_up`・`hang` を sim_harness とホスト版の `--scenario` の両方で読めるようにする。`hang` は「ファームウェアのループを止め、ウォッチドッグの監視だけ続ける」。sim_harness では、ウォッチドッグが切れたら firmware を初期化し直して再起動を模擬する（reset_reason は watchdog）
8. **ホスト版の結合テスト（pytest）**：`tests/host_integration/test_reboot.py`。pty の組を作り、`mcu_c_01 --scenario <hang を含む CSV> <slave>` を起動し、iotspec の `FrameReader`・`decode_frame` でフレームを読む。確かめること：①最初の STATE → BOOT、② hang のあと 1000ms 以内に、別の boot_id の STATE → BOOT が来る、③ 再起動後の各種別の seq が 0 から始まる、④ ログの boot の行が reset_reason watchdog・reset_count 1。実行は `uv run --with "iotspec @ git+https://github.com/io0323/iot-common-spec@v0.2.1" --with pytest pytest tests/host_integration`。CI の host ジョブにこの手順を足す
9. **テスト**
   - 単体：button（20ms 未満の揺れを無視、短押し・長押しの境界 2990・3000ms、長押しのあとの離しが短押しにならない、キューが満杯のとき）、alarm（状態ごとのパターンと周期、位相の起点、消音と解除、INIT のパターン、強制全開の表示）、health（全員そろったときだけリフレッシュ、1つ欠けたら呼ばない、report の期限）、diag（飽和）
   - シナリオ（sim_harness）：
     - 承認の流れ：CRITICAL のあと温度を下げ、1回目の短押しで消音（ブザーが止まる）、2回目で承認して WARNING か NORMAL に戻る。EVENT は THRESHOLD_CLEARED が1件
     - 条件未達の承認：温度が crit−hyst 以上のまま承認しても CRITICAL のまま
     - SENSOR_FAULT からの復帰：sensor_open → sensor_ok のあと、valid_run が10になる前の承認は無効、10以上で1回目消音・2回目承認で分類先に戻る。EVENT は SENSOR_RECOVERED が1件
     - 長押し：NORMAL で3秒押すとファン1000、もう一度3秒押すと元に戻る。離したときに承認や消音が起きない
     - hang：ウォッチドッグが切れて再起動し、STATE → BOOT が送られ、reset_reason が watchdog になる
   - P2 までのテストと契約テストはすべて通り続けること

## P3 で作らないもの

STM32 のコード（EXTI・IWDG・RCC_CSR・`.noinit`・DWT などの実機の実装は P5）、Renode、品質ゲート、GET_DIAG の応答（CR-1 待ち）、INTERLOCK の EVENT（CR-3 待ち）。

## 完了条件（すべて満たしてから PR）

1. 警告ゼロでビルドし、CTest が全件合格（P2 までの21件を含む）
2. シナリオで、承認の流れ・条件未達の承認・SENSOR_FAULT からの復帰・長押し・hang による再起動が合格
3. pytest の結合テスト（①〜④）が手元と CI で合格
4. 契約テストの7チェックが手元と CI で合格
5. ASan・UBSan の指摘ゼロ、app・service・lib に malloc・float・double・STM32 のヘッダがない

PR の説明には、完了条件ごとの結果、open-questions に足した項目、P4 への申し送り（品質ゲートで引っかかりそうな箇所が分かっていれば、それも）を書く。マージはしない。

## 止まって相談すること

- 承認・消音・強制全開の組み合わせで、第5章と状態遷移表から振る舞いが1つに決まらないとき
- execv による再起動で、契約テストや pty の扱いに問題が出たとき
- 設計書に値や順序がないことを決める必要が出たとき

いずれも open-questions に記録し、その件に関わる作業を止めて報告する。
