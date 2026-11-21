#!/usr/bin/env bash
# 契約テストの --target-cmd から Renode のターゲット版を起動する（P6、ADR-0006）。
#
#   iotspec-contract --role mcu-c-01 --target-cmd "tools/renode-contract.sh {uart}"
#   tools/renode-contract.sh --measure 20     # 仮想時間と実時間の比だけを測る（範囲の外なら終了コード 1、#56）
#
# renode/thermal_guard.resc（F401RE、循環・半分転送の DMA）を読み込み、USART2 を TCP のソケット端末に出して、
# tools/renode_uart_relay.py が契約テストの pty とつなぐ。温度は 25℃ の生値を与え続ける（契約テストは NORMAL を前提）。
#
# 契約テストは実時間で待つので、ここでだけ速い設定にする（#56。renode-test は Renode の既定のまま）：
#   TIM3 を 1MHz（Renode では PWM の波形を見ないので CCR の値には影響しない）、時間の刻み 1ms、PerformanceInMips 10
# 契約テストが止めると（SIGTERM）、コンテナと中継を止める。
set -u
cd "$(dirname "$0")/.."
IMAGE=${RENODE_IMAGE:-antmicro/renode:1.16.1@sha256:fc2a8c1bad2296a6d7cbc852bbf5540b22b778bdeb0ad42a45b8c54ea1e6a24c}
ELF=${RENODE_ELF:-build/target-renode/mcu_c_01.elf}
PORT=${RENODE_UART_PORT:-3456}
MON=${RENODE_MONITOR_PORT:-3457}
MIPS=${RENODE_MIPS:-10}
NAME=tg-renode-contract-$$

if [ "${1:-}" = "--measure" ]; then
    RELAY_ARGS=(--measure "${2:-20}")
else
    RELAY_ARGS=("$1")
fi

docker run --rm --name "$NAME" --platform linux/amd64 -p "127.0.0.1:$PORT:$PORT" -p "127.0.0.1:$MON:$MON" \
    -v "${RENODE_CONFIG_VOLUME:-tg-renode-config}":/home/developer/.config/renode -v "$PWD":/src:ro "$IMAGE" \
    renode --disable-gui -P "$MON" -e "\$elf=@/src/$ELF; include @/src/renode/thermal_guard.resc; emulation CreateServerSocketTerminal $PORT \"uart\" false; connector Connect sysbus.usart2 uart; sysbus.adc1 FeedSample 2048 0 -1; sysbus.timer3 Frequency 1000000; emulation SetGlobalQuantum \"0.001\"; cpu PerformanceInMips $MIPS; logLevel 3" \
    >"${RENODE_LOG:-/dev/null}" 2>&1 &

cleanup() {
    [ -n "${RELAY:-}" ] && kill -TERM "$RELAY" 2>/dev/null && wait "$RELAY" 2>/dev/null
    docker stop -t 1 "$NAME" >/dev/null 2>&1
}
trap 'cleanup; exit 0' TERM INT

python3 tools/renode_uart_relay.py --uart-port "$PORT" --monitor-port "$MON" "${RELAY_ARGS[@]}" 2>>"${RENODE_RELAY_LOG:-/dev/stderr}" &
RELAY=$!
wait "$RELAY"
STATUS=$?
RELAY=
cleanup
exit "$STATUS"
