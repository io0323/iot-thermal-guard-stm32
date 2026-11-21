# 品質ゲートを手元で流すためのコンテナ（CI の quality ジョブと同じ ubuntu-24.04・apt の版）。tools/quality-docker.sh が使う。
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc cmake ninja-build python3 cppcheck clang-tidy doxygen curl ca-certificates git \
    && rm -rf /var/lib/apt/lists/*
RUN curl -LsSf https://astral.sh/uv/install.sh | env UV_INSTALL_DIR=/usr/local/bin sh \
    && UV_TOOL_BIN_DIR=/usr/local/bin uv tool install lizard \
    && UV_TOOL_BIN_DIR=/usr/local/bin uv tool install gcovr
