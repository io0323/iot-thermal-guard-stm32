# 作品01 フェーズ P4：品質ゲート

## P3 の片付け（先に済ませる）

PR #3 はレビュー済みで、io がマージする。マージを確かめてから `phase/p3-ui-health` をローカルとリモートで削除し、main から `phase/p4-quality` を切ること。`.python-version`（3.12）はそのまま残してよい。

## はじめに読むもの

`CLAUDE.md`、`docs/open-questions.md`、要件定義・基本設計書の非機能要件（NFR-04〜NFR-10）、詳細設計書の第1章（コンパイルオプション）・第9章（CI）・第10章の P4 の行。読み終えたら「作るもの・作らないもの・不明点」を短く報告してから始める。

## ツールの準備

- macOS：`brew install cppcheck doxygen llvm`（clang-tidy は llvm に入っている）、`uv tool install lizard`、`uv tool install gcovr`
- CI（ubuntu-24.04）：apt の `cppcheck`・`clang-tidy`・`doxygen`、`uv tool install` の `lizard`・`gcovr`
- 使った版を README に書く。ルールの結果は版で変わるので、**合否は CI（ubuntu-24.04、gcc）の結果を正とする**
- ツールの導入や実行が権限で止められたら、回り道をせず、どのコマンドを許可してほしいか io に伝えて止まること（P3 で lizard が止まった件）

## P4 で作るもの

1. **カバレッジ**：`CMakePresets.json` に `host-coverage`（出力先 `build/host-cov`、`--coverage`、サニタイザーなし）を足す。CTest を流したあと `gcovr` で app と service の行カバレッジを集計し、80% 未満なら失敗。HTML の報告を CI の成果物にする。80% に届かないときは、除外を足すのではなくテストを足す
2. **cppcheck（MISRA アドオン）**：対象は app・service・lib・ports・main。bsp・tests・third_party・external・build は対象外。MISRA の規則の本文はライセンスの都合でリポジトリに入れない（規則番号だけで判定する）。逸脱は `misra-suppressions.txt` に1件ずつ書き、各行に ADR の番号を付ける。逸脱の理由は `docs/adr/ADR-0002-misra-deviations.md` に「規則・場所・理由・代わりの対策」の表で書く。まとめて抑止（ファイル単位や規則単位の一括の抑止）はしない。必須規則（Mandatory）の逸脱は認めない
3. **clang-tidy**：`.clang-tidy` を置く。`bugprone-*`・`cert-*`・`clang-analyzer-*`・`misc-*`・`readability-*`（`readability-magic-numbers` と `readability-identifier-length` は外してよい）、`WarningsAsErrors: '*'`。`host-debug` で `CMAKE_EXPORT_COMPILE_COMMANDS` を有効にし、その compile_commands.json を使う。対象は cppcheck と同じ。外した検査と理由は `.clang-tidy` のコメントに書く
4. **lizard**：`lizard -C 10` を app・service・lib・main・bsp/host に対して実行し、1件でも超えたら失敗。今わかっている超過は bsp/host の5関数（`main.c` の `parse_args`・`main`、`scenario.c` の `parse_row`・`scenario_parse`・`scenario_player_advance`）。これらは**振る舞いを変えずに**関数を分けて直す
5. **Doxygen**：`Doxyfile` を置き、app・service・lib・ports の公開ヘッダを対象に `WARN_IF_UNDOCUMENTED = YES`、`WARN_AS_ERROR = FAIL_ON_WARNINGS`。足りないコメントを書き足して警告をゼロにする
6. **禁止項目の検査**：P1 から手で行っている grep（app・service・lib の malloc 系・float・double・STM32 のヘッダ）を `tools/check_forbidden.sh` にまとめ、CI で実行する
7. **CI**：`quality` ジョブを足す（カバレッジ、cppcheck、clang-tidy、lizard、Doxygen、禁止項目）。host ジョブと並行して動かし、どれか1つでも落ちたら失敗。報告ファイルは失敗時も成果物にする
8. **ADR**：`docs/adr/` を作り、次の3件を書く
   - ADR-0001 回路の前提（詳細設計書 第3章：プルアップと NTC の向き、B 定数、生値の式。実機フェーズで実測の表に置き換える前提であること）
   - ADR-0002 MISRA の逸脱（上記2）
   - ADR-0003 テストの基盤（CMake＋CTest＋Unity、Ceedling と CMock を使わない理由、仮想時間のシナリオと契約テストの役割分担）
   - 形式は「状況・決定・理由・影響」。`docs/adr/README.md` に一覧を置く
9. **README**：品質ゲートの一覧（ツール・版・合格の基準・手元での実行コマンド）を足す

## P4 で作らないもの

新しい機能、STM32 のコード、Renode。リファクタリングは、lizard・cppcheck・clang-tidy・Doxygen の指摘を直すのに必要な範囲だけにする。

## 完了条件（すべて満たしてから PR）

1. CI の quality ジョブがすべて合格：app・service の行カバレッジ 80% 以上、cppcheck（MISRA）の抑止されていない指摘ゼロ、clang-tidy の指摘ゼロ、lizard の超過ゼロ、Doxygen の警告ゼロ、禁止項目ゼロ
2. MISRA の逸脱がすべて ADR-0002 に1件ずつ載っていて、必須規則の逸脱がない
3. リファクタリングで振る舞いが変わっていない：CTest 全件、契約テストの7チェック、pytest の結合テストが手元と CI で合格
4. ADR-0001〜0003 と README の品質ゲートの一覧がある

PR の説明には、各ゲートの結果（カバレッジの数値、逸脱の件数と規則の内訳、直した関数の一覧と直したあとの複雑度）、open-questions に足した項目、P5 への申し送りを書く。マージはしない。

## 止まって相談すること

- MISRA の必須規則に当たり、ADR で逸脱にできない指摘が出たとき
- 指摘を直すために設計（モジュールの分け方や関数の形）を変える必要が出たとき
- カバレッジを上げるのに、テストからは通せないコード（防御的な分岐など）が多く残るとき
- ツールの導入や実行が権限で止められたとき

いずれも open-questions に記録し、その件に関わる作業を止めて報告する。
