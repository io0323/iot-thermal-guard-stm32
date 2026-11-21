#!/usr/bin/env bash
# 品質ゲートを、CI と同じ ubuntu:24.04 のコンテナで流す（open-questions #40 の回答：手元でもコンテナで流す）。
#
#   tools/quality-docker.sh
#
# 作業ツリーを読み取り専用でマウントし、コンテナの中の複製で tools/quality.sh を流す（手元の build/host を壊さない）。
# 報告は build/quality-docker/ に置く。終了コードは tools/quality.sh と同じ。
set -eu
cd "$(dirname "$0")/.."
IMAGE=iot-thermal-guard-quality
OUT=build/quality-docker
mkdir -p "$OUT"
docker build -q -t "$IMAGE" -f tools/docker/Dockerfile tools/docker >/dev/null
docker run --rm -v "$PWD":/src:ro -v "$PWD/$OUT":/out "$IMAGE" bash -c '
    set -e
    mkdir /w && cd /src && tar --exclude=./build -cf - . | tar -C /w -xf -
    cd /w && tools/quality.sh /out
'
