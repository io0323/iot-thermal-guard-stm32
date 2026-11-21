#!/usr/bin/env bash
# #54 の確かめ：スタックの塗りの範囲を「今の SP の 64 バイト下まで」にしても、stack_peak が変わらないこと。
# 余白 0（今の SP まで塗る）の target-renode を別に作り、同じ Robot のテスト（スタック）を両方で流して値を比べる。
#
#   tools/renode-stack-margin.sh [出力先]     （既定は build/renode-stack-margin）
set -u
cd "$(dirname "$0")/.."
OUT=${1:-build/renode-stack-margin}
TEST="スタック：塗りの残りから求めた最大使用量（stack_peak と同じ計算）"
mkdir -p "$OUT"
cmake --preset target-renode -B build/target-renode-margin0 -DTG_STACK_PAINT_MARGIN=0 >"$OUT/build-margin0.txt" 2>&1 &&
    cmake --build build/target-renode-margin0 >>"$OUT/build-margin0.txt" 2>&1 || { tail -20 "$OUT/build-margin0.txt"; exit 1; }
peak() {
    grep -o 'stack_peak（塗りの残りから）：[0-9]* バイト' "$1" | grep -o '[0-9]*' | tail -1
}
tools/renode-test.sh "$OUT/margin64" --fixture "$TEST" | tee "$OUT/margin64.txt"
tools/renode-test.sh "$OUT/margin0" --fixture "$TEST" --variable ELF:/src/build/target-renode-margin0/mcu_c_01.elf | tee "$OUT/margin0.txt"
P64=$(peak "$OUT/margin64.txt")
P0=$(peak "$OUT/margin0.txt")
echo "stack_peak：余白 64 バイト = ${P64:-?} バイト、余白 0 = ${P0:-?} バイト" | tee "$OUT/summary.txt"
[ -n "$P64" ] && [ "$P64" = "$P0" ] || { echo "NG: 余白で stack_peak が変わった" | tee -a "$OUT/summary.txt"; exit 1; }
