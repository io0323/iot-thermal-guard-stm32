#!/usr/bin/env bash
# 品質ゲート（P4、詳細設計書 第9章の quality ジョブ）。CI と tools/quality-docker.sh の両方がこのスクリプトを呼ぶ。
#
#   tools/quality.sh [出力先]      既定の出力先は build/quality
#
# ゲートを順に流し、1つ落ちても残りを流してから、どれか1つでも落ちていれば終了コード 1 にする。
# 報告ファイル（カバレッジの HTML、各ツールの出力、summary.txt）は出力先に置く。
#   forbidden  : tools/check_forbidden.sh
#   lizard     : 循環的複雑度 10 以下（app・service・lib・main・bsp/host・bsp/stm32f4。#49）
#   cppcheck   : MISRA アドオン＋warning・style・portability。抑止は 15.5 の再分類（misra-suppressions.txt）と、該当行の直前のインライン抑止（ADR-0002）
#   clang-tidy : .clang-tidy。host-debug の compile_commands.json を使う（app・service・lib・ports・main）
#   doxygen    : Doxyfile。app・service・lib・ports の公開ヘッダ
#   coverage   : host-coverage で CTest を流し、gcovr で app・service の行カバレッジ 80% 以上
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=${1:-build/quality}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
SUMMARY="$OUT/summary.txt"
: > "$SUMMARY"
FAILED=0
CHECK_DIRS="app service lib ports main"

record() { # 名前 結果 補足
    printf '%-10s %-4s %s\n' "$1" "$2" "$3" | tee -a "$SUMMARY"
}

run_gate() { # 名前 関数
    local name=$1 fn=$2
    echo "==== $name ===="
    if "$fn"; then
        record "$name" "合格" "${NOTE:-}"
    else
        record "$name" "失敗" "${NOTE:-}（$OUT/$name.txt）"
        FAILED=1
    fi
    NOTE=""
}

tool_versions() {
    {
        echo "cppcheck:   $(cppcheck --version 2>&1)"
        echo "clang-tidy: $(clang-tidy --version 2>&1 | grep -m1 -i version)"
        echo "doxygen:    $(doxygen --version 2>&1)"
        echo "lizard:     $(lizard --version 2>&1)"
        echo "gcovr:      $(gcovr --version 2>&1 | head -1)"
        echo "gcc:        $(gcc --version 2>&1 | head -1)"
    } | tee "$OUT/versions.txt"
}

build_host_debug() { # compile_commands.json と生成ヘッダ（変換表・ゴールデン）を用意する
    cmake --preset host-debug >"$OUT/build-host-debug.txt" 2>&1 &&
        cmake --build --preset host-debug >>"$OUT/build-host-debug.txt" 2>&1
}

gate_forbidden() {
    tools/check_forbidden.sh >"$OUT/forbidden.txt" 2>&1
}

gate_lizard() {
    lizard -C 10 -w app service lib main bsp/host bsp/stm32f4 >"$OUT/lizard.txt" 2>&1
    local rc=$?
    NOTE="超過 $(grep -c 'warning:' "$OUT/lizard.txt" || true) 件"
    return $rc
}

gate_cppcheck() {
    # shellcheck disable=SC2086
    cppcheck --std=c11 --addon=misra --enable=warning,style,portability --error-exitcode=1 \
        --suppressions-list=misra-suppressions.txt --inline-suppr \
        --template='{file}:{line}:{column}: {severity}: {message} [{id}]' \
        -I ports -I lib -I service -I app -I main -I config -I build/host/generated \
        $CHECK_DIRS >/dev/null 2>"$OUT/cppcheck.txt"
    local rc=$?
    NOTE="抑止されていない指摘 $(grep -c '\[' "$OUT/cppcheck.txt" || true) 件"
    return $rc
}

gate_clang_tidy() {
    local files
    files=$(ls app/*.c service/*.c lib/*.c main/*.c)
    # shellcheck disable=SC2086
    clang-tidy -p build/host --quiet $files >"$OUT/clang-tidy.txt" 2>&1
    local rc=$?
    NOTE="指摘 $(grep -cE '(warning|error): .*\[[a-z0-9.,-]+\]$' "$OUT/clang-tidy.txt" || true) 件"
    return $rc
}

gate_doxygen() {
    { cat Doxyfile; echo "OUTPUT_DIRECTORY = $OUT/doxygen"; echo "WARN_LOGFILE = $OUT/doxygen.txt"; } | doxygen - >/dev/null 2>&1
    local rc=$?
    NOTE="警告 $(grep -cE '(warning|error):' "$OUT/doxygen.txt" 2>/dev/null || true) 件"
    return $rc
}

gate_coverage() {
    {
        cmake --preset host-coverage && cmake --build --preset host-coverage &&
            ctest --preset host-coverage
    } >"$OUT/coverage-build.txt" 2>&1 || { NOTE="ビルドか CTest が失敗"; return 1; }
    mkdir -p "$OUT/coverage"
    gcovr -r "$ROOT" --filter 'app/' --filter 'service/' --exclude-unreachable-branches \
        --html-details "$OUT/coverage/index.html" --txt "$OUT/coverage.txt" \
        --fail-under-line 80 build/host-cov >/dev/null 2>"$OUT/coverage-gcovr.txt"
    local rc=$?
    local app svc
    app=$(gcovr -r "$ROOT" --filter 'app/' build/host-cov 2>/dev/null | awk '/^TOTAL/{print $4}')
    svc=$(gcovr -r "$ROOT" --filter 'service/' build/host-cov 2>/dev/null | awk '/^TOTAL/{print $4}')
    NOTE="行 $(awk '/^TOTAL/{print $4}' "$OUT/coverage.txt")（app $app、service $svc、基準 80%）"
    return $rc
}

tool_versions
if ! build_host_debug; then
    record "build" "失敗" "host-debug のビルド（$OUT/build-host-debug.txt）"
    exit 1
fi
run_gate forbidden gate_forbidden
run_gate lizard gate_lizard
run_gate cppcheck gate_cppcheck
run_gate clang-tidy gate_clang_tidy
run_gate doxygen gate_doxygen
run_gate coverage gate_coverage
echo "==== まとめ（$SUMMARY） ===="
cat "$SUMMARY"
exit $FAILED
