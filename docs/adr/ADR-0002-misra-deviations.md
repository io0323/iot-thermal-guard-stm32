# ADR-0002 MISRA C:2012 の逸脱と規則の再分類

- 状態：採用（2026-11-21）。P5 の最初のコミットで改訂（時刻の比較を tick_reached() にして D-01〜D-10 を解消、抑止をインラインに移した。open-questions #42）
- 関係する書類：要件定義・基本設計書 NFR-07、詳細設計書 第9章、open-questions #40・#41、`misra-suppressions.txt`

## 状況

NFR-07 は「MISRA C:2012 の必須規則に準拠し、逸脱は ADR に理由を記録する」とし、判定を「ツールで検査できる範囲で違反ゼロ」とする。P4 で cppcheck の MISRA アドオンを CI の quality ジョブに入れた。

- ツール：cppcheck 2.13.0（ubuntu-24.04 の apt）の `--addon=misra`。合否は CI の結果を正とする
- 対象：app・service・lib・ports・main。bsp・tests・third_party・external・build は対象外
- 自作の bsp/stm32f4 も cppcheck（MISRA）と clang-tidy の対象外とする（open-questions #49）。CMSIS のレジスタのマクロ（`GPIOA` などの `((GPIO_TypeDef *) GPIOA_BASE)`）が整数からポインタへの変換で 11.4・11.6 に当たり、レジスタに触れる行のほぼすべてが逸脱になるため。代わりに lizard（複雑度10以下）、コンパイラの警告ゼロ（同じ警告オプション＋-Werror）、`tools/check_forbidden.sh` の対象にし、ISR は `docs/isr-review.md` でレビューする
- 規則の本文はライセンスの都合でリポジトリに入れず、規則番号だけで判定する

P4 の着手時点の指摘は12の規則で200か所あり、必須規則（Mandatory）に当たるものはなかった。

## 決定

1. 直せる指摘はコードを直す（下の「直した指摘」）
2. 規則 15.5 だけは、MISRA Compliance:2020 の規則の再分類（GRP）で Disapplied にする（下の「規則の再分類」、#40）
3. それ以外で直さないものは、1か所ずつ逸脱にする。該当行の直前に `// cppcheck-suppress misra-c2012-X.Y ; ADR-0002 D-番号` を書き（cppcheck は `--inline-suppr` で動かす）、この ADR の表に理由と代わりの対策を書く。ファイル単位や規則単位の一括の抑止はしない（15.5 の再分類だけが例外で、これだけを `misra-suppressions.txt` に書く）
4. 必須規則（Mandatory）の逸脱は認めない

### 規則の再分類（GRP）

| 番号 | 規則 | 元の分類 | 新しい分類 | 理由 | 代わりの対策 |
| --- | --- | --- | --- | --- | --- |
| R-01 | 15.5（関数の出口は1つ） | Advisory | Disapplied | 引数の確認や探索での早期 return は、読みやすさと安全性を高める（異常な入力を先に抜けて、本体は正常な場合だけを書ける）。出口を1つにすると入れ子が深くなり、循環的複雑度の上限（10）とぶつかる。着手時点で119か所（18ファイル） | 動的な資源を持たない（malloc 禁止、NFR-04）ので、早期 return で解放漏れが起きない。lizard で循環的複雑度を10以下に保つ（CI の quality ジョブ）。関数の後始末が必要になった場合は、その関数では早期 return を使わない |

cppcheck では `misra-c2012-15.5` だけを、この ADR の番号を付けて全体に抑止する。

### 個別の逸脱

抑止は該当行の直前のコメントなので、編集で行がずれても外れない。表の「場所」の行番号は P5 の改訂の時点の目安。

| 番号 | 規則（分類） | 場所 | 理由 | 代わりの対策 |
| --- | --- | --- | --- | --- |
| D-11 | 8.7（Advisory） | `app/cooling_policy.c:12` `cooling_auto` | 製品のコードでは同じファイルからしか呼ばないが、auto(t) の境界（3499・3500・3502・3503・4499・4500）を単体テストから直接確かめるために公開している | `test_cooling_policy` の `test_auto_boundaries`。公開ヘッダに Doxygen のコメントがある |
| D-12 | 8.7（Advisory） | `app/cooling_policy.c:25` `cooling_floor` | D-11 と同じ（状態ごとの下限を直接確かめる） | `test_cooling_policy` の `test_floor_per_state` |
| D-13 | 8.7（Advisory） | `app/safety_fsm.c:138` `fsm_classify` | D-11 と同じ（分類の境界を、既定と違うしきい値で直接確かめる） | `test_safety_fsm` の `test_classify_direct` |
| D-14 | 8.7（Advisory） | `lib/ring_buffer.c:19` `rb_count` | D-11 と同じ（リングの件数を直接確かめる）。lib の汎用の部品として公開している | `test_ring_buffer` |
| D-15 | 19.2（Advisory） | `service/link_frame.h:97` `link_frame_t` の `union u` | 復号したフレームの項目を、種別（type）ごとに1つの構造体で持つ。種別ごとに別の型と関数を作ると、受信・送信・ログの経路がすべて種別の数だけ分かれる | 書き込んだメンバーと読むメンバーは必ず `type` で選ぶ（`link_frame_decode` が type を確かめてから該当メンバーだけを埋める）。バイト列への変換は `put_u16le` などで項目ごとに行い、union を通した型の読み替え（type punning）はしない。ゴールデンのテストベクタ全件で符号化・復号を確かめる |
| D-16 | 19.2（Advisory） | `service/link_frame.h:105` 同じ union の閉じ側の行 | cppcheck が同じ union を2行で報告する | D-15 と同じ |

### 解消した逸脱

| 番号 | 規則（分類） | 場所 | 解消 |
| --- | --- | --- | --- |
| D-01〜D-10 | 10.8（Required） | 時刻の比較 `(int32_t)(now - t) >= 0` の10か所（`app/command_handler.c` のリースの期限、`main/firmware.c` の INIT の最長2000ms、`service/button.c` の長押し、`service/health.c` の report の期限、`service/report.c` の STATE の60秒、`service/scheduler.c` の2か所、`service/temp_sensor.c` の2か所、`service/upstream.c` の上位断の10秒） | P5 の最初のコミットで `lib/tick.h` の `tick_reached(now, deadline)`（符号なしの差が 0x80000000 未満なら真）に置き換えて解消（open-questions #42）。符号付きへのキャストを使わないので 10.8 に当たらない。境界（差が 0・0x7FFFFFFF・0x80000000・0xFFFFFFFF）と一周をまたぐ場合を `test_tick` で確かめ、各箇所の一周のテスト（`test_upstream`・`test_scheduler` の `test_wraps_uptime` など）もそのまま通る |

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
- 時刻の比較は、全箇所が同じ形であることが一周のバグを防ぐ要点。P4 では決まりの形のまま逸脱にしたが、P5 で決まりの形そのものを `tick_reached()` に変え、逸脱をなくした（全箇所が1つの関数を通るので、形がそろうことも保たれる）
- テストのための公開関数と union は、それぞれ検証のしやすさと経路の単純さを優先し、代わりの対策をテストで裏付ける

## 影響

- 新しい逸脱を足すときは、該当行の直前にインラインの抑止を書き、この ADR の表に番号・場所・理由・代わりの対策を書く。番号は使い回さない（解消した D-01〜D-10 も欠番のまま）。CI の quality ジョブは、抑止されていない指摘が1件でもあれば失敗する
- 抑止はコードと一緒に動くので、編集で行がずれてもゲートは落ちない。抑止の付いた行を消したり書き換えたりしたときは、この表も同じコミットで直す
- ベンダーのコード（LL・CMSIS）は対象外（NFR-07）。bsp/stm32f4 の自作部分も cppcheck・clang-tidy の対象外（P5、#49。上の「状況」）
