# 作品01 フェーズ P1：ホスト版の骨組み・UART・契約テスト

## はじめに読むもの

1. `CLAUDE.md`（このリポジトリの決まり。必ず従う）
2. `docs/design/02-detailed-design.md`（とくに第1・2・6・8・9・10章）
3. `docs/design/01-requirements-basic-design.md` 第8章
4. `docs/design/03-common-spec-cr-v0.2.2.md` と `docs/open-questions.md`
5. iot-common-spec v0.2.1 の `docs/spec.md` 第7章、`docs/contract-tests.md`、`uart/golden/README.md`、`src/iotspec/contract/roles/mcu_c_01.py`（契約テストが何を期待しているか）

読み終えたら、作業に入る前に「P1 で作るもの」「作らないもの」「不明点」を短くまとめて報告し、不明点があれば open-questions に記録してから進めること。

## 準備

- main から `phase/p1-host-skeleton` を切る。`CLAUDE.md`・`docs/design/`・`docs/open-questions.md` が未コミットなら、このブランチの最初のコミットに入れる
- submodule を追加する
  - `external/iot-common-spec`：https://github.com/io0323/iot-common-spec のタグ `v0.2.1` に固定
  - `third_party/unity`：ThrowTheSwitch/Unity の最新の安定タグに固定（タグ名を README に書く）
- CMock は P1 では入れない。ports は関数ポインタの構造体なので、テストのフェイクは手書きにする

## P1 で作るもの

詳細設計書の該当章の仕様どおりに作る。値・型・順序はすべて設計書から取ること。

1. **ビルドの雛形**（第1章）：`CMakeLists.txt`、`CMakePresets.json`（P1 は `host-debug` だけ。出力先 `build/host`、ASan・UBSan 付き、Ninja）、`cmake/` の警告設定、層ごとのライブラリ（app・service・lib・bsp_host）。`.gitignore`
2. **lib**：`cobs`、`crc16`（CCITT-FALSE）、`crc32`（IEEE）、`ring_buffer`（単一生産者・単一消費者、容量は2の累乗）、`byteorder`（`put_u16le` など）
3. **ports/ports.h**：第2章のとおり（P1 で使わないポートも定義だけはする）
4. **service**
   - `link`：フレームの符号化・復号（6種別すべて）、受信の組み立て（224バイト）、送信キュー（16枠、満杯時の扱い、seq は送り出すときに種別ごとに振る）、理由別の破棄カウンタ（第6章）
   - `upstream`：UNKNOWN／ALIVE／LOST の3状態（第6章の表）
   - `scheduler`：静的なタスク表、`next += period`、オーバーラン数、`cycles()` による実行時間の最大値（第2章）
   - `report`：TELEMETRY → HEARTBEAT を1秒ごと（INIT を抜けた時刻が起点）、STATE を最後に送ってから60秒ごと（GET_STATUS で送った STATE は起点を動かさない）
   - `boot_id`：`entropy_port` から集めて CRC-32、0 なら 1（第7章）
   - `temp_sensor`（**仮版**）：ADC の値は毎回受け取るが、温度は 25.00℃（2500）固定・quality 0 を返し、12サンプル目で ready にする。本版は P2。仮版であることをコメントに書く
5. **app**
   - `safety_fsm`（**仮版**）：INIT → 分類(t)（遷移表の行2だけ。EVENT は STATE_CHANGED）。それ以外の行は P2
   - `cooling_policy`：第5章の式どおり（auto・状態の下限・手動指定・強制全開フラグ。強制全開は P1 では常に false）
   - `command_handler`：第6章の表どおりすべて（検証、重複キャッシュ16件、リース、SET_THRESHOLD の範囲と CRITICAL・SENSOR_FAULT 中の unsafe、GET_STATUS は ACK(done) → STATE、GET_DIAG は ACK(done) だけ、INIT 中は捨てる）
6. **main/firmware.c**：起動の順序（第2章。P1 の INIT は「temp_sensor が ready になるまで」。その間はフレームを送らず CMD を捨て、HEARTBEAT は受け付ける）。INIT を抜けたら STATE → BOOT（from・to は 0）→ STATE_CHANGED の順に送る。上位断・復帰の EVENT（code 7・8）も送る
7. **bsp_host**（第8章）：`mcu_c_01 [--temp <℃>] [--log <file.jsonl>] [UART_PATH]`。UART_PATH がなければ `IOTSPEC_UART_PATH`、どちらもなければ終了コード 2。raw・115200・8N1・ノンブロッキング（pty でボーレート設定が失敗しても続行）。1スレッドの `poll()` ループ、CLOCK_MONOTONIC、`getentropy()`（macOS は `<sys/random.h>`）。SIGTERM・SIGINT・read の 0／EIO で終了コード 0、終了前に診断値をログへ。ログは JSON Lines（P1 は boot・state・fan・tx・rx・rx_error・diag）。`--temp` は温度を生値に戻して ADC ポートに渡す（P1 の temp_sensor は使わないが、P2 のために実装しておく）。wdt と LED・ブザーは何もしない実装でよい。リセット要因は環境変数 `MCU_C_01_RESET_REASON`（なければ power_on）
8. **ゴールデン**（第9章）：`tools/gen_golden_vectors.py` で `external/iot-common-spec/uart/golden/*.json` から `build/generated/golden_vectors.h` を作る（CMake の add_custom_command）。テストは ① 01→03 の全件で符号化が hex と一致（`expected.discarded` が crc_mismatch のものを除く） ② 全件で復号が frame の項目と一致 ③ crc_mismatch のものは復号が crc_mismatch で失敗 ④ 件数が生成時と一致
9. **単体テスト**（Unity、手書きのフェイク）：lib 全部、link（符号化・復号・受信の組み立て・破棄理由ごと・送信キューの満杯と押し出し・seq が種別ごと）、upstream（3状態の全遷移、起動から10秒、最初の HEARTBEAT で RESTORED を出さない）、command_handler（表の全検証条件、重複キャッシュの押し出し、rejected もキャッシュ、リースの期限ちょうど、INIT 中は捨てる、状態の下限での limited）、cooling_policy（auto の境界 3499・3500・4500、下限、手動指定、limited）、scheduler（周期のずれがないこと、オーバーラン）、report（1秒・60秒の周期と順序）
10. **CI**（`.github/workflows/ci.yml`、ubuntu-24.04）：submodule ごと取り出し → host-debug でビルド → CTest → `astral-sh/setup-uv` → `uv tool install "iotspec @ git+https://github.com/io0323/iot-common-spec@v0.2.1"` → `iotspec-contract --role mcu-c-01 --target-cmd "./build/host/mcu_c_01 --log contract-log.jsonl {uart}" --junit contract.xml` → `contract.xml` と `contract-log.jsonl` を成果物としてアップロード（失敗時も）。MQTT ブローカーは使わない
11. **README.md**（最小）：何をするものか、ビルド・テスト・契約テストの手順、Unity のタグ、フェーズの進み具合

## P1 で作らないもの

STM32 のコード・ターゲットのプリセット・Renode、温度の変換表とフィルタ（temp_sensor 本版）、遷移表の行2以外、button・alarm・health・疑似ウォッチドッグ、シナリオ（`--scenario`）と sim_harness、品質ゲート（カバレッジ・cppcheck・clang-tidy・lizard・Doxygen チェック）、CMock。これらに手を付けたくなったら、仮版で済ませるか open-questions に書く。

## 完了条件（すべて満たしてから PR）

1. `cmake --preset host-debug && cmake --build --preset host-debug` が警告ゼロで通り、`ctest --preset host-debug --output-on-failure` が全件合格
2. ゴールデン全件で上記 ①〜④ のテストが合格
3. 契約テスト `iotspec-contract --role mcu-c-01 --target-cmd "./build/host/mcu_c_01 {uart}"` の7チェック（boot_order・ack・duplicate_cmd_ref・upstream_lost_restored・frame_encoding・seq_per_type・converted_schema）が、この Mac と CI の両方で合格
4. ASan・UBSan の指摘ゼロ（CTest と契約テストの両方の実行で）
5. 上記9の単体テストがそろっている
6. app・service・lib に `malloc`・`float`・`double` と STM32 のヘッダがない（grep の結果を PR に載せる）

PR は `gh pr create` で作る。説明には、完了条件ごとの結果（実行したコマンドと出力の要点、契約テストの結果表）、open-questions に足した項目、P2 への申し送り（仮版にしたものの一覧）を書く。マージはしない。

## 止まって相談すること

- 契約テストの期待と設計書が食い違うとき（例：設計書どおりに作ると不合格になる）
- ゴールデンと設計書の値が食い違うとき
- 設計書に値や順序が書かれていないことを決める必要が出たとき

いずれも `docs/open-questions.md` に記録し、その件に関わる作業を止めて報告する。iot-common-spec 側は変更しない。
