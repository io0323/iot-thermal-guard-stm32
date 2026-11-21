#!/usr/bin/env python3
"""ターゲット版のサイズの検査（NFR-04、P5）。

    tools/check_size.py build/target-release/mcu_c_01.elf build/target-release/mcu_c_01.map

- arm-none-eabi-size -A の区画ごとの大きさから
  - Flash：Flash に置く区画（0x08000000〜）＋ .data の初期値（Flash に置いて起動時に写す）
  - RAM：RAM に置く区画（.data・.bss・.noinit・スタック）
  を求め、Flash 64KB・RAM 16KB 以内かを確かめる（リンカスクリプトの FLASH 512K・RAM 96K は器の大きさで、要件の上限ではない）
- .map から、スタックが 4KB 確保されていること、ヒープの区画がないこと、malloc 系と _sbrk が持ち込まれていないことを確かめる
- 超えたら・欠けていたら終了コード 1
"""
import re
import subprocess
import sys

FLASH_BASE, FLASH_END = 0x08000000, 0x08080000
RAM_BASE, RAM_END = 0x20000000, 0x20018000
FLASH_LIMIT = 64 * 1024
RAM_LIMIT = 16 * 1024
STACK_BYTES = 4 * 1024
FORBIDDEN_SYMBOLS = ("malloc", "calloc", "realloc", "free", "_sbrk", "_malloc_r", "_free_r")


def sections(elf):
    out = subprocess.run(["arm-none-eabi-size", "-A", "-d", elf], check=True, capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        m = re.match(r"^(\.\S+)\s+(\d+)\s+(\d+)\s*$", line)
        if m:
            rows.append((m.group(1), int(m.group(2)), int(m.group(3))))
    return rows


def check_map(path):
    errors = []
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"0x([0-9a-f]+)\s+_Stack_Size = ", text)
    if not m or int(m.group(1), 16) != STACK_BYTES:
        errors.append("スタックの確保（_Stack_Size = 4K）が .map にない")
    m = re.search(r"0x([0-9a-f]+)\s+_Min_Heap_Size = ", text)
    if not m or int(m.group(1), 16) != 0:
        errors.append("ヒープが 0 でない（_Min_Heap_Size）")
    if re.search(r"^\s*\._?heap\b", text, re.MULTILINE):
        errors.append("ヒープの区画がある")
    # 持ち込まれたアーカイブのメンバーと、その理由になった記号（.map の先頭の一覧）
    head = text.split("Discarded input sections")[0]
    for sym in FORBIDDEN_SYMBOLS:
        if re.search(r"\(" + re.escape(sym) + r"\)", head):
            errors.append(f"{sym} が持ち込まれている（動的メモリは禁止、NFR-04）")
    return errors


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    elf, mapfile = sys.argv[1], sys.argv[2]
    flash = ram = 0
    print(f"{'区画':<16}{'大きさ':>8}  {'番地':>10}  内訳")
    for name, size, addr in sections(elf):
        if size == 0:
            continue
        where = []
        if FLASH_BASE <= addr < FLASH_END:
            flash += size
            where.append("Flash")
        if RAM_BASE <= addr < RAM_END:
            ram += size
            where.append("RAM")
            if name == ".data":
                flash += size  # 初期値は Flash に置く
                where.append("初期値は Flash")
        if where:
            print(f"{name:<16}{size:>8}  0x{addr:08x}  {'・'.join(where)}")
    errors = check_map(mapfile)
    if flash > FLASH_LIMIT:
        errors.append(f"Flash {flash} バイトが上限 {FLASH_LIMIT} を超えた")
    if ram > RAM_LIMIT:
        errors.append(f"RAM {ram} バイトが上限 {RAM_LIMIT} を超えた")
    for e in errors:
        print("NG:", e)
    print(
        f"Flash {flash} / {FLASH_LIMIT} バイト（{100 * flash / FLASH_LIMIT:.1f}%）、"
        f"RAM {ram} / {RAM_LIMIT} バイト（{100 * ram / RAM_LIMIT:.1f}%、スタック {STACK_BYTES} を含む）"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
