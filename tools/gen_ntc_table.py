#!/usr/bin/env python3
"""NTC の変換表（−20〜120℃、1℃刻み141点の ADC 生値）を作る（詳細設計書 第3章、open-questions #20）。

回路：3.3V → 10kΩ プルアップ → ADC 入力 → NTC（10kΩ、B=3435）→ GND。ADC は12ビット、レシオメトリック。
生値 = round(4095 × R ÷ (R + 10kΩ))、R = 10kΩ × exp(B × (1/T − 1/298.15))。round は四捨五入。
実機フェーズで実測した表に置き換えられるように、表は生成物として扱い手で編集しない。

使い方: gen_ntc_table.py <out.h>
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

R_PULL = 10000.0
R25 = 10000.0
BETA = 3435.0
T_MIN_C = -20
T_MAX_C = 120
FULL_SCALE = 4095.0

# 第3章の概算値。大きく違えば生成を止める（P2 の止まる条件）
APPROX = {-20: 3627, 25: 2048, 120: 238}
APPROX_TOLERANCE = 2


def ntc_ohm(temp_c: float) -> float:
    t_k = temp_c + 273.15
    return R25 * math.exp(BETA * ((1.0 / t_k) - (1.0 / 298.15)))


def raw_at(temp_c: float) -> int:
    r = ntc_ohm(temp_c)
    return int(math.floor(FULL_SCALE * r / (r + R_PULL) + 0.5))


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    table = [raw_at(t) for t in range(T_MIN_C, T_MAX_C + 1)]
    for t, expect in APPROX.items():
        got = table[t - T_MIN_C]
        if abs(got - expect) > APPROX_TOLERANCE:
            raise SystemExit(f"{t}℃ の生値 {got} が第3章の概算 {expect} と大きく違う")
    for i in range(1, len(table)):
        if table[i] >= table[i - 1]:
            raise SystemExit(f"表が単調に減っていない: {T_MIN_C + i - 1}℃ {table[i - 1]} → {T_MIN_C + i}℃ {table[i]}")

    rows = []
    for i in range(0, len(table), 10):
        chunk = ", ".join(f"{v}u" for v in table[i : i + 10])
        rows.append(f"    {chunk}, /* {T_MIN_C + i}℃〜 */ \\")
    text = "\n".join(
        [
            "/* 生成物：tools/gen_ntc_table.py が第3章の回路の式から作る。手で編集しない。 */",
            "#ifndef NTC_TABLE_DATA_H",
            "#define NTC_TABLE_DATA_H",
            "",
            f"#define NTC_T_MIN_C ({T_MIN_C})",
            f"#define NTC_T_MAX_C {T_MAX_C}",
            f"#define NTC_POINTS {len(table)}u",
            f"#define NTC_RAW_COLD {table[0]}u /* {T_MIN_C}℃ */",
            f"#define NTC_RAW_HOT {table[-1]}u /* {T_MAX_C}℃ */",
            "",
            "/* 添字 i が (NTC_T_MIN_C + i)℃。温度が上がるほど生値が下がる。 */",
            "#define NTC_TABLE_INIT { \\",
            *rows,
            "}",
            "",
            "#endif /* NTC_TABLE_DATA_H */",
            "",
        ]
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
