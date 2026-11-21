#!/usr/bin/env bash
# ターゲット版のビルドと検査を、CI の target ジョブと同じ ubuntu:24.04 のコンテナで流す（この Mac では
# Homebrew が使えないため、ツールチェーンは手元でもコンテナに置く）。
#
#   tools/target-docker.sh
#
# 作業ツリーを読み取り専用でマウントし、コンテナの中の複製で tools/target.sh を流す。
# 報告と .elf・.map は build/target-docker/ に置く。終了コードは tools/target.sh と同じ。
set -eu
cd "$(dirname "$0")/.."
IMAGE=iot-thermal-guard-quality
OUT=build/target-docker
mkdir -p "$OUT"
docker build -q -t "$IMAGE" -f tools/docker/Dockerfile tools/docker >/dev/null
docker run --rm -v "$PWD":/src:ro -v "$PWD/$OUT":/out "$IMAGE" bash -c '
    set -e
    mkdir /w && cd /src && tar --exclude=./build -cf - . | tar -C /w -xf -
    cd /w && tools/target.sh /out
'
