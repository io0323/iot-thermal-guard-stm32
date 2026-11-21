# CLAUDE.md

作品01「高機能センサー制御・安全監視デバイス」（C11 × STM32F401RE、ベアメタル）のファームウェア。
IoT・組込みポートフォリオ4作品のうちの1つで、上位（作品03）とは UART で通信する。device_id は `mcu-c-01`。

## 正とする書類（上が優先）

1. `external/iot-common-spec`（タグ v0.2.1）の `docs/spec.md` 第7章・`uart/golden/`・`profiles/mcu-c-01.yaml`：UART のフレームの形、コード値、seq
2. `docs/open-questions.md` で「決定」になった項目（設計書より優先）
3. `docs/design/02-detailed-design.md`：数値・型・順序・モジュールの中身・フェーズ分割
4. `docs/design/01-requirements-basic-design.md` 第8章：実装前レビューでの決定、指令の検証、遷移ごとの EVENT
5. `docs/design/01-requirements-basic-design.md` のその他の章
6. `docs/design/03-common-spec-cr-v0.2.2.md`：共通仕様の変更案と、それが決まるまでの暫定対応

食い違いを見つけたら上を優先して進め、`docs/open-questions.md` に記録する。`docs/design/` は Claude Docs からの写しなので、指示がない限り編集しない。

## 守ること

- **仕様や書類にないことは決めない。** `docs/open-questions.md` に1行足して（形式は同ファイルの表）、その件に関わる作業を止め、PR の説明か作業報告で知らせる。共通仕様の問題は区分を「共通仕様」にし、作品01の中で吸収しない
- **iot-common-spec は変更しない。** submodule は v0.2.1 に固定。変更が必要なら open-questions に書く
- **実機は使わない。** 検証はホスト版・単体テスト・仮想時間シナリオ・契約テスト・（P6 以降）Renode で行う。実機でしか確かめられないことは「実機フェーズで確認」と書いて残す
- **フェーズの範囲を越えない。** 今のフェーズの表（詳細設計書 第10章）にないモジュールは作らない。必要なら仮版（スタブ）にとどめ、仮版であることをコメントに書く

## コードの決まり

- 層：app → service → lib、app・service → ports。STM32 のヘッダを include できるのは `bsp/stm32f4/` だけ。CMake のターゲットを層ごとに分け、逆向きのリンクを作らない
- C11、`-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wcast-align -Wswitch-enum -fno-common`。ホスト版は `-funsigned-char`
- app・service・lib では `malloc` 系・`float`・`double` を使わない（bsp_host の CLI と温度→生値の変換だけ例外）
- データに素の `char` を使わない（`uint8_t`）。フレームは構造体を memcpy せず、`put_u16le()` などで1項目ずつ組み立てる
- 時刻の比較は必ず `tick_reached()`（`lib/tick.h`）を使う（uptime_ms は u32 で一周する）
- ISR はフラグを立てるかバッファに入れるだけ。ISR と共有する変数は `volatile`
- 公開関数には Doxygen コメント（`@brief`・`@param`・`@return`）。循環的複雑度は 10 以下を目安にする

## コマンド

```sh
git submodule update --init --recursive
cmake --preset host-debug && cmake --build --preset host-debug
ctest --preset host-debug --output-on-failure
uv tool install "iotspec @ git+https://github.com/io0323/iot-common-spec@v0.2.1"
iotspec-contract --role mcu-c-01 --target-cmd "./build/host/mcu_c_01 {uart}" --junit contract.xml
```

ターゲット版（STM32F401RE）のビルドと検査（サイズ・スタック・ISR）は、手元では CI と同じコンテナで流す（この Mac では Homebrew が使えない）：

```sh
tools/target-docker.sh   # target-debug・target-release、tools/check_size.py・stack_report.py・isr_report.py
```

Renode（エミュレーター）のテストと、Renode での契約テスト（どちらも `antmicro/renode:1.16.1` のコンテナ。ADR-0006）：

```sh
cmake --preset target-renode && cmake --build --preset target-renode   # ADC_SOFTWARE_START=1（実機と ADC の開始方法が違う）
tools/renode-test.sh                                                   # Robot Framework（tests/renode/）
iotspec-contract --role mcu-c-01 --target-cmd "tools/renode-contract.sh {uart}"
```

プリセット名・出力先・実行ファイル名（`build/host/mcu_c_01`）は契約テストが前提にしているので変えない。

## ブランチと PR

- フェーズごとに main から `phase/pN-<短い名前>` を切る。作業は小さなコミットに分ける
- 完了条件をすべて満たしたら `gh pr create` で PR を作る。説明には、完了条件ごとの結果（実行したコマンドと出力の要点）、open-questions に足した項目、次のフェーズへの申し送りを書く
- マージはしない（io が行う）。次のフェーズを始める前に、前のフェーズのブランチがマージ済みであることを確かめ、ローカルとリモートのブランチを削除する
- 秘密情報（トークン、パスワード）をコミットしない
