#!/usr/bin/env bash
# ターゲット版（STM32F401RE）のビルドと検査をまとめて流す（P5）。CI の target ジョブと tools/target-docker.sh が使う。
#
#   tools/target.sh [out]   （既定の out は build/target-report）
#
# 1. target-debug・target-release を警告ゼロ（-Werror）でビルド
# 2. tools/check_size.py：Flash 64KB・RAM 16KB（.data＋.bss＋.noinit＋スタック）以内か（NFR-04）
# 3. tools/stack_report.py：-fstack-usage の .su から上位の関数、再帰・可変長配列がないこと、最悪の深さ
# 4. tools/isr_report.py：ISR の命令数とループがないこと（docs/isr-review.md の根拠）
# .elf・.bin・.map・報告を out に置く。1つ落ちても残りを流し、最後に失敗にする。
set -u
cd "$(dirname "$0")/.."
OUT=${1:-build/target-report}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
FAIL=0
: >"$OUT/summary.txt"

{
    echo "arm-none-eabi-gcc: $(arm-none-eabi-gcc --version | head -1)"
    echo "binutils: $(arm-none-eabi-size --version | head -1)"
    echo "cmake: $(cmake --version | head -1)"
    for m in third_party/cmsis_core third_party/cmsis_device_f4 third_party/stm32f4xx_hal_driver; do
        echo "$m: $(git -C "$m" describe --tags 2>/dev/null || echo '?')"
    done
} >"$OUT/versions.txt"

note() { echo "$1" | tee -a "$OUT/summary.txt"; }

for p in target-debug target-release; do
    echo "==== $p ===="
    if cmake --preset "$p" >"$OUT/$p-configure.txt" 2>&1 &&
        cmake --build --preset "$p" >"$OUT/$p-build.txt" 2>&1; then
        warn=$(grep -c "warning:" "$OUT/$p-build.txt" || true)
        if [ "$warn" -ne 0 ]; then
            note "$p 失敗 警告 $warn 件（$OUT/$p-build.txt）"
            FAIL=1
        else
            note "$p 合格 警告 0 件"
        fi
        cp "build/$p/mcu_c_01.elf" "build/$p/mcu_c_01.bin" "build/$p/mcu_c_01.map" "$OUT/" 2>/dev/null &&
            for f in elf bin map; do mv "$OUT/mcu_c_01.$f" "$OUT/mcu_c_01-$p.$f"; done
        if python3 tools/check_size.py "build/$p/mcu_c_01.elf" "build/$p/mcu_c_01.map" >"$OUT/$p-size.txt" 2>&1; then
            note "$p サイズ 合格 $(tail -1 "$OUT/$p-size.txt")"
        else
            note "$p サイズ 失敗（$OUT/$p-size.txt）"
            FAIL=1
        fi
        (cd "build/$p" && find . \( -name '*.su' -o -name '*.ci' \) -print0 | tar --null -czf "$OUT/$p-stack-usage.tgz" -T -) || true
        if python3 tools/stack_report.py "build/$p" >"$OUT/$p-stack.txt" 2>&1; then
            note "$p スタック 合格 $(tail -1 "$OUT/$p-stack.txt")"
        else
            note "$p スタック 失敗（$OUT/$p-stack.txt）"
            FAIL=1
        fi
        if python3 tools/isr_report.py "build/$p/mcu_c_01.elf" >"$OUT/$p-isr.txt" 2>&1; then
            note "$p ISR 合格 $(tail -1 "$OUT/$p-isr.txt")"
        else
            note "$p ISR 失敗（$OUT/$p-isr.txt）"
            FAIL=1
        fi
        for s in SysTick_Handler DMA2_Stream0_IRQHandler USART2_IRQHandler; do
            arm-none-eabi-objdump -d --disassemble="$s" "build/$p/mcu_c_01.elf"
        done >"$OUT/$p-isr-disasm.txt" 2>&1
    else
        note "$p 失敗 ビルド（$OUT/$p-build.txt）"
        FAIL=1
    fi
done

echo "==== まとめ（$OUT/summary.txt） ===="
cat "$OUT/summary.txt"
exit "$FAIL"
