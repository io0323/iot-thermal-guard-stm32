#!/usr/bin/env bash
# Renode のテスト（Robot Framework）を、antmicro/renode のコンテナで流す（P6、ADR-0006）。CI の renode ジョブも使う。
#
#   tools/renode-test.sh [出力先] [renode-test の引数...]     （既定の出力先は build/renode-test）
#
# 先に target-renode をビルドしておく（build/target-renode/mcu_c_01.elf）。作業ツリーを読み取り専用でマウントし、
# 結果（log.html・report.html・robot_output.xml）を出力先に置く。時間の設定は Renode の既定のまま（#56）。
set -u
cd "$(dirname "$0")/.."
IMAGE=${RENODE_IMAGE:-antmicro/renode:1.16.1@sha256:fc2a8c1bad2296a6d7cbc852bbf5540b22b778bdeb0ad42a45b8c54ea1e6a24c}
OUT=${1:-build/renode-test}
shift || true
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
chmod a+rwx "$OUT" # コンテナの中の Renode は developer（uid 1000）で結果を書く
docker volume create tg-renode-config >/dev/null
docker run --rm -v tg-renode-config:/c ubuntu:24.04 chown 1000:1000 /c
# SVD（stm32f4.repl が起動のたびに取る）を、取得が止まらない --console の起動で一度だけキャッシュに入れる
docker run --rm --platform linux/amd64 -v tg-renode-config:/home/developer/.config/renode -v "$PWD":/src:ro "$IMAGE" \
    renode --disable-gui --console -e "mach create; machine LoadPlatformDescription @/src/renode/nucleo_f401re.repl; quit" \
    </dev/null >/dev/null 2>&1
docker run --rm --platform linux/amd64 -v tg-renode-config:/home/developer/.config/renode \
    -v "$PWD":/src:ro -v "$OUT":/out "$IMAGE" \
    bash -c 'python3 --version; renode-test /src/tests/renode/thermal_guard.robot --results-dir /out "$@"' _ "$@"
