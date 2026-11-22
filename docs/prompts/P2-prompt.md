# 作品01 フェーズ P2：温度計測と状態遷移

## P1 への回答（先に片付ける）

PR #1 はレビュー済みで、io がマージする。マージを確かめてから `phase/p1-host-skeleton` をローカルとリモートで削除し、main から `phase/p2-temp-fsm` を切ること。

open-questions #16〜#19 は、どれも推奨案 A で「決定（2026-11-21）」にする。
- #16：受信バッファのあふれは rx_overflow
- #17：60秒ごとの STATE は TELEMETRY → HEARTBEAT の後
- #18：EIO 以外の入出力エラーは終了コード 1
- #19：rejected だった GET_STATUS の重複には STATE を送り直さない

`docs/design/02-detailed-design.md` の写しを次の3か所だけ直す（原本は更新済み）。
1. 第4章の箇条書き「連続回数のカウンタは…」の文を、次の文に置き換える：「連続回数のカウンタ（t ≧ warn、t ≧ crit、t < warn−hyst の3つ）は状態に関係なく毎ステップ数え、条件が崩れたときと温度が故障のときだけ 0 に戻す。状態が変わっても戻さない（NORMAL → WARNING で crit のカウントがやり直しになると、急な昇温で CRITICAL が最大1秒遅れるため）。しきい値の変更は次のステップから効き、カウンタはそのまま」
2. 第10章の P2 の行：「ホスト版の `--scenario`（温度のみ）」を「ホスト版の `--scenario`（温度と、センサー・ADC の event）」にする
3. 第10章の P3 の行：「シナリオの event（ボタン・センサー・ADC・hang）」を「シナリオの event（ボタン・hang）」にする

## はじめに読むもの

`CLAUDE.md`、`docs/open-questions.md`、詳細設計書の第2章（起動の順序）・第3章（温度計測）・第4章（状態遷移）・第5章（冷却）・第8章（シナリオ）・第9章（テスト）、基本設計書 第8章（遷移ごとの EVENT の表）。読み終えたら、P1 と同じように「作るもの・作らないもの・不明点」を短く報告してから始める。

## P2 で作るもの

1. **変換表**：`tools/gen_ntc_table.py` が第3章の回路（3.3V → 10kΩ プルアップ → ADC → NTC 10kΩ・B=3435 → GND、12ビット）の式から、−20〜120℃ の1℃刻み141点の「生値」の表を作る。ゴールデンと同じく CMake でビルド時に生成し、生成物は手で編集しない。表の両端の生値を README に書く
2. **lib**：`median`（5点）、`moving_avg`（8点、整数）
3. **temp_sensor 本版**（第3章の表のとおり）：生値で短絡・断線（<50・>4045 が3回連続）、範囲外は −20.00／120.00℃ に丸めて temp_clamped を数える、表の二分探索＋線形補間、メディアン、変化率（前回採用した値との差が 5.00℃ 超なら棄却して temp_rejected を数え、3回連続で故障）、移動平均、ready（12サンプル目）、ADC 無応答（300ms で故障、続く間は1秒ごとに `adc->restart()`）、故障に入ったらフィルタを空にする、valid_run、故障中は temp_c100 = −32768・quality 2
4. **safety_fsm 本版**：第4章の遷移表の全行を、`{現状態, 条件関数, 次状態}` の静的な表で上から評価する。連続回数のカウンタは上の「P1 への回答」1 の新しい決まりに従う。承認（ack）は入力として受けるが、P2 ではボタンがないので常に false で動かす
5. **event_map**（app）：基本設計書 第8章の表どおり、遷移（from・to）から EVENT の code と severity を決める。1遷移1件
6. **しきい値**：command_handler が持つ warn・crit・hyst を fsm の入力に渡す（SET_THRESHOLD が状態遷移に効くようにする）
7. **INIT 本版**：INIT は temp_sensor が ready になったら抜ける。2000ms までに ready にならないか故障なら、行1で SENSOR_FAULT に入る。抜けるときの送信順は P1 と同じ（STATE → BOOT → 遷移の EVENT）
8. **sim_harness**（`tests/scenario/`）：app・service・firmware をフェイクのポートでつなぎ、1ms ずつ仮想時間を進めるテスト用の土台。CSV（第8章の形式 `t_ms,temp_c,event`）を読み、PWM・送信フレーム・状態の時系列を記録して検査できるようにする。P2 で扱う event は `sensor_open`・`sensor_short`・`sensor_ok`・`adc_stall`・`adc_resume`。ボタンと hang は P3
9. **ホスト版の `--scenario`**：同じ CSV を実時間で再生する。`--temp` より優先
10. **テスト**
    - 単体：変換表（両端、生値 49・50・4045・4046、範囲外の丸め 237・238・3627・3628 付近）、median・moving_avg、temp_sensor（ready が12サンプル目、短絡・断線の3回連続、変化率の棄却と3回連続での故障、ADC 無応答の300ms と1秒ごとの restart、故障でフィルタが空になること、valid_run）、safety_fsm（遷移表の全行、状態ごとに「どの行にも当たらない」入力、故障がほかのどの条件より優先されること、カウンタが状態変化で戻らないこと）、event_map（表の全行）
    - シナリオ（sim_harness）：
      - NFR-01：温度が crit を超えてから PWM が 1000 になるまで 2500ms 以内。昇温の速さは 1℃/s・10℃/s・40℃/s の3通りで、それぞれ NORMAL から始める。どれも変化率の上限（5℃/100ms）より遅い。実測した時間をテストの出力に表示する
      - 昇温して警報、冷却して戻る一連：NORMAL → WARNING → CRITICAL のあと温度が下がっても CRITICAL のまま（承認がないため）。WARNING → NORMAL はヒステリシスどおり
      - 故障：sensor_open・sensor_short・adc_stall でそれぞれ SENSOR_FAULT に入り、ファン 100%、TELEMETRY の temp −32768・quality 2、EVENT SENSOR_FAULT が1件
      - 急な跳び：25℃ の段差（変化率の上限を超える）で、3回棄却したあと SENSOR_FAULT
      - INIT：ADC が最初から無応答なら 2000ms で SENSOR_FAULT に入り、STATE → BOOT → SENSOR_FAULT の順に送る
      - SET_THRESHOLD：NORMAL で warn を今の温度より下に変えると、次のステップから数え始めて10サンプル（1秒）後に WARNING に入る

## P2 で作らないもの

button・alarm・health・疑似ウォッチドッグ（P3）、シナリオの button と hang の event（P3）、STM32 と Renode、品質ゲート。承認（ack）による CRITICAL・SENSOR_FAULT からの復帰は、単体テストでは確かめるが、シナリオでは確かめない（ボタンが P3 のため）。

## 完了条件（すべて満たしてから PR）

1. 警告ゼロでビルドし、CTest が全件合格（P1 の15件を含む）
2. 遷移表の全行と「どの行にも当たらない」入力を、状態ごとに単体テストで確かめている
3. 変換表の両端・範囲外・故障の境界（49・50・4045・4046）を単体テストで確かめている
4. シナリオで NFR-01（3通りの昇温速度すべてで 2500ms 以内）と故障の検出が合格。実測値を PR に載せる
5. 契約テストの7チェックが手元と CI で合格（既定の 25℃ で NORMAL に入ること）
6. ASan・UBSan の指摘ゼロ、app・service・lib に malloc・float・double・STM32 のヘッダがない

PR の説明には、完了条件ごとの結果、NFR-01 の実測値の表、open-questions に足した項目、P3 への申し送りを書く。マージはしない。

## 止まって相談すること

- 遷移表・EVENT の表・第3章の値のとおりに作るとシナリオや契約テストが通らないとき
- 変換表の生成で、第3章の概算値（−20℃ で約3627、25℃ で約2048、120℃ で約238）と大きく違う値が出たとき
- 設計書に値や順序がないことを決める必要が出たとき

いずれも open-questions に記録し、その件に関わる作業を止めて報告する。
