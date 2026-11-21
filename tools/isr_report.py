#!/usr/bin/env python3
"""ISR の命令数の目安（P5、NFR-03、docs/isr-review.md の根拠）。

    tools/isr_report.py build/target-release/mcu_c_01.elf

ISR ごとに、本体と、そこから bl（または関数の外への b）で呼ぶ関数を逆アセンブルして制御の流れをたどり、
- 入り口から届く命令の数（ループがなければ、1回の実行で通る命令の数はこれ以下）
- 制御の流れに輪（ループ）があるか（後ろへの分岐でも、合流するだけなら輪ではない）
を数える。ループがあれば終了コード 1（ISR は「フラグとバッファだけ」で、繰り返しを持たない、NFR-03）。

サイクルの目安 = 例外の入り口と出口（12＋10 サイクル、FPU の文脈の遅延積みは使わない前提）
              ＋ 命令の数 × 3（Flash の待ち 2WS。ART のキャッシュに外れたときの最悪として1命令3サイクル）
これは見積もりで、実測（DWT）は実機フェーズで行う。
"""
import re
import subprocess
import sys

ISRS = ("SysTick_Handler", "DMA2_Stream0_IRQHandler", "USART2_IRQHandler")
SYSCLK_MHZ = 84
ENTRY_EXIT_CYCLES = 22
CYCLES_PER_INSN = 3
LIMIT_US = 20


def disasm(elf, sym):
    out = subprocess.run(
        ["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--disassemble={sym}", elf],
        check=True, capture_output=True, text=True,
    ).stdout
    insns = []
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
        if m:
            insns.append((int(m.group(1), 16), m.group(2), m.group(3)))
    return insns


COND = ("eq", "ne", "cs", "cc", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le")


def successors(i, insns, index):
    """命令 i の次に実行されうる命令の番地（関数の外への分岐は外の関数の名前で返す）。"""
    addr, op, args = insns[i]
    nxt = [insns[i + 1][0]] if i + 1 < len(insns) else []
    base = op.split(".")[0]
    t = re.search(r"([0-9a-f]+) <([^>+]+)", args)
    target = None
    if t:
        target = int(t.group(1), 16) if int(t.group(1), 16) in index else ("call", t.group(2))
    if base in ("bx",) or (base.startswith("pop") and "pc" in args) or (base == "ldr" and args.startswith("pc")):
        return nxt if base[-2:] in COND else []  # IT ブロックの中の条件付きの戻りなら、次へも進む
    if base == ".word":
        return []
    if base == "bl":
        return nxt + ([target] if isinstance(target, tuple) else [])
    if base == "b" and target is not None:
        return [target]
    if (base in ("cbz", "cbnz") or (base.startswith("b") and base[1:] in COND)) and target is not None:
        return [target] + nxt
    return nxt


def walk(elf, sym, seen):
    """命令の数（到達できるものだけ）、ループ（制御の流れの輪）、呼ぶ関数を数える。"""
    seen.add(sym)
    insns = disasm(elf, sym)
    index = {a: k for k, (a, _, _) in enumerate(insns)}
    loops = []
    callees = []
    state = {}  # 番地 → 1：探索中、2：済み

    def dfs(addr):
        state[addr] = 1
        for nx in successors(index[addr], insns, index):
            if isinstance(nx, tuple):
                callees.append(nx[1])
            elif state.get(nx) == 1:
                loops.append(f"{sym} 0x{addr:x}→0x{nx:x}")
            elif nx not in state:
                dfs(nx)
        state[addr] = 2

    if insns:
        dfs(insns[0][0])
    total = len(state)
    for c in callees:
        if c not in seen:
            n, lp = walk(elf, c, seen)
            total += n
            loops += lp
    return total, loops


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    elf = sys.argv[1]
    bad = False
    print(f"{'ISR':<26}{'命令':>6}{'サイクルの目安':>14}{'μs の目安':>10}  呼ぶ関数・ループ")
    for isr in ISRS:
        seen = set()
        n, loops = walk(elf, isr, seen)
        cycles = ENTRY_EXIT_CYCLES + n * CYCLES_PER_INSN
        us = cycles / SYSCLK_MHZ
        others = sorted(seen - {isr})
        note = ("呼ぶ：" + "・".join(others)) if others else "呼ぶ関数なし"
        if loops:
            note += "　ループ：" + "・".join(loops)
            bad = True
        if us > LIMIT_US:
            bad = True
        print(f"{isr:<26}{n:>6}{cycles:>14}{us:>10.2f}  {note}")
    print(f"上限 {LIMIT_US}μs（NFR-03）。ループ{'あり' if bad else 'なし'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
