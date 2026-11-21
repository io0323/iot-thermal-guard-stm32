#!/usr/bin/env python3
"""iot-common-spec の uart/golden/*.json から C のテストベクタ（golden_vectors.h）を作る（詳細設計書 第9章）。

C 側で JSON を読まないために、frame の項目・バイト列・復号エラーの期待値をヘッダに展開する。
使い方: gen_golden_vectors.py <golden_dir> <out.h>
"""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

TYPE_CODES = {"TELEMETRY": 0x01, "EVENT": 0x02, "HEARTBEAT": 0x03, "STATE": 0x04, "CMD": 0x10, "ACK": 0x11}
KNOWN_DISCARDED = {None, "awaiting_state", "crc_mismatch"}


def cmd_args(frame: dict) -> bytes:
    raw = frame.get("raw_args") or ""
    if raw:
        return bytes.fromhex(raw)
    args = frame.get("args") or []
    op = frame["op"]
    if op == 0x01:  # SET_FAN_OVERRIDE duty_pm:u16
        return struct.pack("<" + "H" * len(args), *args)
    if op == 0x03:  # SET_THRESHOLD warn,crit,hyst:i16
        return struct.pack("<" + "h" * len(args), *args)
    if args:
        raise SystemExit(f"op 0x{op:02x} の args を詰められない: {args}")
    return b""


def c_bytes(data: bytes) -> str:
    return "{" + ", ".join(f"0x{b:02x}" for b in data) + "}"


def frame_init(frame: dict) -> str:
    t = frame["type"]
    code = TYPE_CODES[t]
    if frame["type_code"] != code:
        raise SystemExit(f"type_code が種別と合わない: {frame}")
    head = f".type = 0x{code:02x}u, .seq = {frame['seq']}u"
    if t == "TELEMETRY":
        body = (
            f".u.telemetry = {{.uptime_ms = {frame['uptime_ms']}u, .temp_c100 = {frame['temp_c100']}, "
            f".quality = {frame['quality']}u, .state = {frame['state']}u, .fan_pm = {frame['fan_pm']}u}}"
        )
    elif t == "EVENT":
        body = (
            f".u.event = {{.uptime_ms = {frame['uptime_ms']}u, .code = {frame['code']}u, "
            f".severity = {frame['severity']}u, .from = {frame['from_state']}u, .to = {frame['to_state']}u, "
            f".temp_c100 = {frame['temp_c100']}}}"
        )
    elif t == "HEARTBEAT":
        body = f".u.heartbeat = {{.uptime_ms = {frame['uptime_ms']}u}}"
    elif t == "STATE":
        body = (
            f".u.state = {{.fw_version = {frame['fw_version']}u, .boot_id = {frame['boot_id']}u, "
            f".period_ms = {frame['period_ms']}u}}"
        )
    elif t == "CMD":
        args = cmd_args(frame)
        body = (
            f".u.cmd = {{.cmd_ref = {frame['cmd_ref']}u, .op = {frame['op']}u, .ttl_s = {frame['ttl_s']}u, "
            f".args_len = {len(args)}u, .args = {c_bytes(args) if args else '{0}'}}}"
        )
    elif t == "ACK":
        body = (
            f".u.ack = {{.cmd_ref = {frame['cmd_ref']}u, .status = {frame['status']}u, "
            f".reason = {frame['reason']}u, .limited = {frame['limited']}u, .applied = {frame['applied']}u}}"
        )
    else:
        raise SystemExit(f"知らない種別: {t}")
    return "{" + head + ", " + body + "}"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    golden_dir = Path(sys.argv[1])
    out = Path(sys.argv[2])
    files = sorted(golden_dir.glob("*.json"))
    if not files:
        raise SystemExit(f"ゴールデンがない: {golden_dir}")

    arrays: list[str] = []
    rows: list[str] = []
    for i, path in enumerate(files):
        g = json.loads(path.read_text(encoding="utf-8"))
        data = bytes.fromhex(g["hex"])
        hex_file = path.with_suffix(".hex")
        if hex_file.exists() and bytes.fromhex(hex_file.read_text(encoding="utf-8")) != data:
            raise SystemExit(f"{hex_file.name} と json の hex が違う")
        if not data or data[-1] != 0:
            raise SystemExit(f"{path.name}: 末尾が 0x00 でない")
        direction = g["direction"]
        if direction not in ("01->03", "03->01"):
            raise SystemExit(f"{path.name}: 知らない向き {direction}")
        discarded = (g.get("expected") or {}).get("discarded")
        if discarded not in KNOWN_DISCARDED:
            raise SystemExit(f"{path.name}: 知らない discarded {discarded}")
        arrays.append(f"static const uint8_t golden_bytes_{i:02d}[] = {c_bytes(data)};")
        rows.append(
            "    {"
            f'.name = "{path.stem}", .from_device = {"true" if direction == "01->03" else "false"}, '
            f'.crc_mismatch = {"true" if discarded == "crc_mismatch" else "false"}, '
            f".frame = {frame_init(g['frame'])}, "
            f".bytes = golden_bytes_{i:02d}, .len = sizeof golden_bytes_{i:02d}"
            "},"
        )

    text = "\n".join(
        [
            "/* 生成物：tools/gen_golden_vectors.py が external/iot-common-spec/uart/golden の JSON から作る。手で編集しない。 */",
            "#ifndef GOLDEN_VECTORS_H",
            "#define GOLDEN_VECTORS_H",
            "",
            "#include <stdbool.h>",
            "#include <stddef.h>",
            "#include <stdint.h>",
            "",
            '#include "link_frame.h"',
            "",
            "typedef struct {",
            "    const char *name;",
            "    bool from_device;  /* 01->03 */",
            "    bool crc_mismatch; /* expected.discarded が crc_mismatch（わざと壊したフレーム） */",
            "    link_frame_t frame;",
            "    const uint8_t *bytes; /* 符号化済み（末尾の 0x00 を含む） */",
            "    size_t len;",
            "} golden_vector_t;",
            "",
            *arrays,
            "",
            "static const golden_vector_t golden_vectors[] = {",
            *rows,
            "};",
            "",
            f"#define GOLDEN_GENERATED_COUNT {len(files)}u",
            "",
            "#endif /* GOLDEN_VECTORS_H */",
            "",
        ]
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
