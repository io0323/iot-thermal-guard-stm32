#!/usr/bin/env bash
# 禁止項目の検査（詳細設計書 第1章、NFR-04・05・06）。app・service・lib に次のものがあれば失敗する。
#   - 動的メモリ：malloc・calloc・realloc・free
#   - 浮動小数点：float・double
#   - STM32 のヘッダ・名前：stm32・STM32（ハード依存は bsp/stm32f4 だけ）
# 例外の bsp_host の CLI 解析と「温度 → 生値」の変換は bsp にあるので対象外。
set -u
cd "$(dirname "$0")/.."
pattern='\b(malloc|calloc|realloc|free|float|double)\b|stm32|STM32'
if grep -rnE "$pattern" app service lib; then
    echo "check_forbidden: 上の行に禁止項目がある" >&2
    exit 1
fi
echo "check_forbidden: 禁止項目なし（app・service・lib）"
