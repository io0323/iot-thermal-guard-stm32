# ADR-0002 MISRA C:2012 の逸脱と規則の再分類

- 状態：採用（2026-11-21）
- 関係する書類：要件定義・基本設計書 NFR-07、詳細設計書 第9章、open-questions #40・#41、`misra-suppressions.txt`

## 状況

NFR-07 は「MISRA C:2012 の必須規則に準拠し、逸脱は ADR に理由を記録する」とし、判定を「ツールで検査できる範囲で違反ゼロ」とする。P4 で cppcheck の MISRA アドオンを CI の quality ジョブに入れた。

- ツール：cppcheck 2.13.0（ubuntu-24.04 の apt）の `--addon=misra`。合否は CI の結果を正とする
- 対象：app・service・lib・ports・main。bsp・tests・third_party・external・build は対象外
- 規則の本文はライセンスの都合でリポジトリに入れず、規則番号だけで判定する

P4 の着手時点の指摘は12の規則で200か所あり、必須規則（Mandatory）に当たるものはなかった。

## 決定

1. 直せる指摘はコードを直す（下の「直した指摘」）
2. 規則 15.5 だけは、MISRA Compliance:2020 の規則の再分類（GRP）で Disapplied にする（下の「規則の再分類」、#40）
3. それ以外で直さないものは、1か所ずつ逸脱にする。`misra-suppressions.txt` に「規則:ファイル:行 // ADR-0002 D-番号」の形で1行ずつ書き、この ADR の表に理由と代わりの対策を書く。ファイル単位や規則単位の一括の抑止はしない（15.5 の再分類だけが例外）
4. 必須規則（Mandatory）の逸脱は認めない

### 規則の再分類（GRP）

| 番号 | 規則 | 元の分類 | 新しい分類 | 理由 | 代わりの対策 |
| --- | --- | --- | --- | --- | --- |
| R-01 | 15.5（関数の出口は1つ） | Advisory | Disapplied | 引数の確認や探索での早期 return は、読みやすさと安全性を高める（異常な入力を先に抜けて、本体は正常な場合だけを書ける）。出口を1つにすると入れ子が深くなり、循環的複雑度の上限（10）とぶつかる。着手時点で119か所（18ファイル） | 動的な資源を持たない（malloc 禁止、NFR-04）ので、早期 return で解放漏れが起きない。lizard で循環的複雑度を10以下に保つ（CI の quality ジョブ）。関数の後始末が必要になった場合は、その関数では早期 return を使わない |

cppcheck では `misra-c2012-15.5` だけを、この ADR の番号を付けて全体に抑止する。

### 個別の逸脱

行番号は記録した時点（P4）のもの。行がずれたら `misra-suppressions.txt` とこの表を一緒に直す。

| 番号 | 規則（分類） | 場所 | 理由 | 代わりの対策 |
| --- | --- | --- | --- | --- |
| D-01 | 10.8（Required） | `app/command_handler.c:180` `cmd_lease_tick`（リースの期限） | 時刻の比較を `(int32_t)(now - t) >= 0` の形で書く（詳細設計書 第2章・CLAUDE.md の決まり）。u32 の差を符号付きで読むことで、uptime_ms の一周（約49.7日）をまたいでも正しく比べられる。差を一度変数に入れる書き方より、決まりの形のまま全箇所をそろえるほうが見落としを防げる | 一周をまたぐ単体テスト（`test_command_handler` の `test_lease_expires_exactly_at_ttl`・`test_lease_across_uptime_wrap`、`test_upstream` と `test_scheduler` の `test_wraps_uptime` など）。gcc・clang の符号付きへの変換は2の補数の切り詰めで、ターゲット（arm-none-eabi-gcc）も同じ |
| D-02 | 10.8（Required） | `main/firmware.c:150` `task_control`（INIT の最長2000ms） | D-01 と同じ | D-01 と同じ。INIT の2000ms は `test_scenarios` の `test_init_adc_stall` |
| D-03 | 10.8（Required） | `service/button.c:28` `button_update`（長押しの3000ms） | D-01 と同じ | `test_button` の `test_time_wraps`・`test_long_press_3000_on_reaching` |
| D-04 | 10.8（Required） | `service/health.c:33` `health_check`（report の期限） | D-01 と同じ | `test_health` の `test_deadline_across_wrap`・`test_report_deadline_1500_inclusive` |
| D-05 | 10.8（Required） | `service/report.c:46` `report_tick`（STATE の60秒） | D-01 と同じ | `test_report` の `test_state_every_60s_after_last_state`・`test_state_period_across_uptime_wrap` |
| D-06 | 10.8（Required） | `service/scheduler.c:37` `run_task`（オーバーランの追いつき） | D-01 と同じ | `test_scheduler` の `test_overrun_runs_once_and_keeps_phase` |
| D-07 | 10.8（Required） | `service/scheduler.c:51` `sched_run`（タスクの期限） | D-01 と同じ | `test_scheduler` の `test_period_does_not_drift`・`test_wraps_uptime` |
| D-08 | 10.8（Required） | `service/temp_sensor.c:156` `temp_tick`（ADC 無応答の300ms） | D-01 と同じ | `test_temp_sensor` の `test_adc_timeout_300ms_and_restart_every_second` |
| D-09 | 10.8（Required） | `service/temp_sensor.c:168` `temp_tick`（restart の1000ms） | D-01 と同じ | 同上 |
| D-10 | 10.8（Required） | `service/upstream.c:24` `upstream_tick`（上位断の10秒） | D-01 と同じ | `test_upstream` の `test_wraps_uptime`・`test_unknown_lost_10s_after_boot`、契約テストの upstream_lost_restored |
| D-11 | 8.7（Advisory） | `app/cooling_policy.c:11` `cooling_auto` | 製品のコードでは同じファイルからしか呼ばないが、auto(t) の境界（3499・3500・3502・3503・4499・4500）を単体テストから直接確かめるために公開している | `test_cooling_policy` の `test_auto_boundaries`。公開ヘッダに Doxygen のコメントがある |
| D-12 | 8.7（Advisory） | `app/cooling_policy.c:23` `cooling_floor` | D-11 と同じ（状態ごとの下限を直接確かめる） | `test_cooling_policy` の `test_floor_per_state` |
| D-13 | 8.7（Advisory） | `app/safety_fsm.c:137` `fsm_classify` | D-11 と同じ（分類の境界を、既定と違うしきい値で直接確かめる） | `test_safety_fsm` の `test_classify_direct` |
| D-14 | 8.7（Advisory） | `lib/ring_buffer.c:18` `rb_count` | D-11 と同じ（リングの件数を直接確かめる）。lib の汎用の部品として公開している | `test_ring_buffer` |
| D-15 | 19.2（Advisory） | `service/link_frame.h:96` `link_frame_t` の `union u` | 復号したフレームの項目を、種別（type）ごとに1つの構造体で持つ。種別ごとに別の型と関数を作ると、受信・送信・ログの経路がすべて種別の数だけ分かれる | 書き込んだメンバーと読むメンバーは必ず `type` で選ぶ（`link_frame_decode` が type を確かめてから該当メンバーだけを埋める）。バイト列への変換は `put_u16le` などで項目ごとに行い、union を通した型の読み替え（type punning）はしない。ゴールデンのテストベクタ全件で符号化・復号を確かめる |
| D-16 | 19.2（Advisory） | `service/link_frame.h:103` 同じ union の閉じ側の行 | cppcheck が同じ union を2行で報告する | D-15 と同じ |

### 直した指摘（P4）

| 規則 | 件数 | 直し方 |
| --- | --- | --- |
| 12.1 | 44 | 演算の優先順位を括弧で明示した |
| 10.8 | 2 | 時刻の比較以外の2か所（`lib/ntc_table.c`）は、差を一度変数に入れてから変換する |
| 12.3 | 8 | 1行に複数の宣言を書かない |
| 8.9 | 5 | 1つの関数でしか使わない静的な表・変数を、その関数の中に移した |
| 2.3 | 3 | 使わない列挙の型名を外した（値だけを使う） |
| 5.9 | 1 | 内部結合の名前の重複（`k_table`）を、`k_ntc_raw`・`k_fsm_table` に分けた |
| 15.7 | 1 | `else if` の終わりに `else` を足した |
| 13.4 | 1 | 代入の結果を条件に使わない |

## 理由

- 早期 return を残すのは、MISRA Compliance:2020 が Advisory 規則に認める正式な手続き（GRP）であり、119か所に同じ理由を並べるより、判断が1か所にまとまって見直しやすい
- 時刻の比較は、全箇所が同じ形であることが一周のバグを防ぐ要点なので、形を変えずに逸脱として記録する
- テストのための公開関数と union は、それぞれ検証のしやすさと経路の単純さを優先し、代わりの対策をテストで裏付ける

## 影響

- 新しい逸脱を足すときは、`misra-suppressions.txt` に1行足し、この ADR の表に番号・場所・理由・代わりの対策を書く。CI の quality ジョブは、抑止されていない指摘が1件でもあれば失敗する
- 行番号で抑止しているので、該当のファイルを編集して行がずれたら、抑止の行とこの表を同じコミットで直す
- 実機フェーズで bsp/stm32f4 を足しても、ベンダーコード（LL・CMSIS）は対象外（NFR-07）。bsp の自作部分を対象に入れるかは P5 で決める
