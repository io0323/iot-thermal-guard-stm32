#!/usr/bin/env bash
# 禁止項目の検査（詳細設計書 第1章、NFR-04・05・06、open-questions #49）。次のものがあれば失敗する。
#   1. app・service・lib：動的メモリ（malloc・calloc・realloc・free）、浮動小数点（float・double）、STM32 の名前
#   2. bsp/stm32f4（自作の部分）：動的メモリと浮動小数点
#   3. STM32 と CMSIS のヘッダ（stm32f4xx*.h・system_stm32*.h・core_cm*.h・cmsis_*.h）の include は bsp/stm32f4 だけ
# 例外の bsp_host の CLI 解析と「温度 → 生値」の変換は bsp/host にあるので 1・2 の対象外。
# third_party・external・build は見ない。
set -u
cd "$(dirname "$0")/.."
FAIL=0

if grep -rnE '\b(malloc|calloc|realloc|free|float|double)\b|stm32|STM32' app service lib; then
    echo "check_forbidden: 上の行に禁止項目がある（app・service・lib）" >&2
    FAIL=1
fi

if grep -rnE '\b(malloc|calloc|realloc|free|float|double)\b' bsp/stm32f4 --include='*.c' --include='*.h'; then
    echo "check_forbidden: 上の行に禁止項目がある（bsp/stm32f4）" >&2
    FAIL=1
fi

vendor_inc='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](stm32f4xx[^">]*|system_stm32[^">]*|core_cm[^">]*|cmsis_[^">]*)\.h[">]'
if grep -rnE "$vendor_inc" --include='*.c' --include='*.h' \
    --exclude-dir=third_party --exclude-dir=external --exclude-dir=build --exclude-dir=.git . | grep -v '^\./bsp/stm32f4/'; then
    echo "check_forbidden: STM32・CMSIS のヘッダを include してよいのは bsp/stm32f4 だけ" >&2
    FAIL=1
fi

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
echo "check_forbidden: 禁止項目なし（app・service・lib・bsp/stm32f4、STM32・CMSIS のヘッダは bsp/stm32f4 だけ）"
