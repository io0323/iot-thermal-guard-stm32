# 共通仕様 v0.2.2 変更案（作品01の実装前レビューから）

2026-11-21 · @io

iot-common-spec v0.2.1 の中では解決できない点が5つある。作品01の中では吸収せず、v0.2.2 の候補として iot-common-spec 側で扱う。決まるまで作品01は「暫定対応」の列のとおりに実装する。原本は Claude Docs の同名の書類。

| # | 問題 | 推奨 | 影響 | 作品01の暫定対応 |
| --- | --- | --- | --- | --- |
| CR-1 | GET_DIAG の結果を返すフレームがない（ACK は applied:u16 だけ） | DIAG フレーム（0x05、01→03）を新設。GET_DIAG に DIAG → ACK(done) の順で返す | 01、03 | GET_DIAG に ACK(done) だけを返す。DIAG は送らない（v0.2.1 の契約テストは未知の種別を壊れたフレームとして不合格にするため） |
| CR-2 | リセット要因を UART で伝える欄がない | BOOT の EVENT に限り `from` を reset_reason（0 unknown、1 power_on、2 pin、3 watchdog、4 software、5 brownout、6 low_power）、`to` を 0 とする | 01、03、04 | BOOT の from・to は 0。リセット要因は診断値とホスト版のログに残す |
| CR-3 | INTERLOCK_ACTIVE／RELEASED に UART のコードがない | コード 9・10 を追加 | 01、03、04 | 送らない。丸めたことは ACK の limited で伝える |
| CR-4 | 作品01が expired を返す条件、ttl_s が 0・600 超のときの扱いが未定義 | 01 は expired を返さない。ttl_s は 1〜600、範囲外は invalid_arg | 01、03 | 推奨どおりに実装 |
| CR-5 | 状態遷移で送る EVENT のコードが曖昧（ゴールデン13と代役で食い違う） | 1遷移1件。「遷移→コード」の表を仕様に入れる | 01、02、04、代役 | 01-requirements-basic-design.md 第8章の暫定表で実装 |

## 仕様の変更ではない作業（iot-common-spec 側）

代役 mcu-c-01 の内部の値を作品01の設計に合わせる（しきい値 45/60/3℃、CRITICAL・SENSOR_FAULT の承認、状態ごとのファンの下限と limited、SET_THRESHOLD の拒否、1遷移1件の EVENT）。作品01のリポジトリでは行わない。
