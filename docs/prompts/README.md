# Claude Code に渡したプロンプト（P1〜P7）

作品01は、設計書（[docs/design/](../design/)）と [CLAUDE.md](../../CLAUDE.md) をもとに、Claude Code にフェーズごとのプロンプトで実装させた。フェーズの分け方は [詳細設計書 第10章](../design/02-detailed-design.md) の表で、各プロンプトは「前提となる書類、作るもの、作らないもの、完了条件、止まるべき条件」の形で書いた。

| フェーズ | プロンプト | 範囲 | PR | open-questions で決めた番号 |
| --- | --- | --- | --- | --- |
| P1 | [P1-prompt.md](P1-prompt.md) | ホスト版の骨組み、UART（COBS・CRC）、指令処理、上位監視、ゴールデン、契約テスト、CI の host ジョブ | [#1](https://github.com/io0323/iot-thermal-guard-stm32/pull/1) | #1〜#19（#1〜#7 は最初の登録で、未決のまま） |
| P2 | [P2-prompt.md](P2-prompt.md) | 変換表、メディアン・移動平均、temp_sensor・safety_fsm の本版、event_map、sim_harness とシナリオ CSV | [#2](https://github.com/io0323/iot-thermal-guard-stm32/pull/2) | #20〜#33 |
| P3 | [P3-prompt.md](P3-prompt.md) | ボタン、警報、health と疑似ウォッチドッグ、リセット要因・回数、診断値、pytest | [#3](https://github.com/io0323/iot-thermal-guard-stm32/pull/3) | #34〜#39 |
| P4 | [P4-prompt.md](P4-prompt.md) | 品質ゲート（カバレッジ・MISRA・clang-tidy・lizard・Doxygen・禁止項目）、CI の quality ジョブ | [#4](https://github.com/io0323/iot-thermal-guard-stm32/pull/4) | #40〜#41 |
| P5 | [P5-prompt.md](P5-prompt.md) | STM32 ターゲット版（LL、割り込み、起動コード、リンカスクリプト）、サイズ・スタック・ISR の検査、CI の target ジョブ | [#5](https://github.com/io0323/iot-thermal-guard-stm32/pull/5) | #42〜#51 |
| P6 | [P6-prompt.md](P6-prompt.md) | Renode（プラットフォーム定義、DMA のモデル、Robot のテスト、Renode の上での契約テスト）、CI の renode ジョブ | [#6](https://github.com/io0323/iot-thermal-guard-stm32/pull/6) | #52〜#56 |
| P7 | [P7-prompt.md](P7-prompt.md) | 書類の仕上げ（テスト仕様書とトレーサビリティの検査、実機フェーズの手順書、README、CHANGELOG） | P7 の PR | — |

## プロンプトのあとに決めたこと

プロンプトは、フェーズを始める時点の指示である。作業中に仕様や設計書にないことに当たると、Claude Code は実装で決めず、[docs/open-questions.md](../open-questions.md) に選択肢と推奨を書いて、その件の作業を止めた。io が回答で決めた内容は、同じ表の「状態」の列に「決定（日付と内容）」として記録している。プロンプトと食い違う場合は、open-questions の決定が優先する（[CLAUDE.md](../../CLAUDE.md) の「正とする書類」の2番目）。

- 回答で設計書の誤りが分かったときは、io が Claude Docs の原本を直し、リポジトリの写し（`docs/design/`）も同じように直すよう指示した
- 回答がプロンプトの値を直したもの（例：#34 の pytest の上限、#48 のボタンの割り込みを使わないこと、#50・#52 の出力先）も、open-questions の決定が正である
- 未決のまま残っているのは、共通仕様の変更案 CR-1〜5（#1〜#5。作品01の外で決める）と、実機フェーズで決めるもの（#6・#7）

表の「open-questions で決めた番号」は、各フェーズのマージの時点で表にあった番号の範囲である。
