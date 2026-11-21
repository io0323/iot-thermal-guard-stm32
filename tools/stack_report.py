#!/usr/bin/env python3
"""ターゲット版のスタックの見積もり（P5、NFR-04）。

    tools/stack_report.py build/target-release

集めるもの（どれもビルドの副産物）：
- .su（-fstack-usage）：関数ごとの自分のフレームの大きさと種類（static／dynamic）
- .ci（-fcallgraph-info=su）：直接の呼び出しと、関数ポインタ越しの呼び出し（__indirect_call）
- .obj の再配置（arm-none-eabi-objdump -r）：番地を取られた関数と、番地を取った翻訳単位（TU）

関数ポインタ越しの呼び出しの行き先は、関数ポインタの型ごとに「その型の表を持つ TU で番地を取られた関数」とし、
その中のいちばん深いものを使う（安全側）。作品01の関数ポインタは次の3種類だけ（INDIRECT_GROUPS）：
- scheduler.c のタスク（sched_fn_t）：main/firmware.c のタスクの表
- safety_fsm.c の遷移表（cond_fn_t・next_fn_t）：app/safety_fsm.c の表
- それ以外（app・service・main からのポートの呼び出し）：bsp_stm32f4 のポートの構造体（g_bsp_* など）
  ホスト版のログの観測フック（fw_observer_t）はターゲット版では NULL なので行き先がない
表にない TU で番地を取られた関数が見つかったら失敗にする（新しい関数ポインタの使い方。この表に足す）。
ポートの関数がさらに関数ポインタ越しに呼ぶ場合も失敗にする（ポートから上の層へ戻る経路を作らない）。

確かめること（どれかが崩れたら終了コード 1）：
1. 可変長配列・alloca がない（.su が static だけ）
2. 再帰がない（直接の呼び出しに輪がない。関数ポインタ越しに戻ってくる経路もない）
3. 最悪の深さ（main からの最深の経路＋割り込みの入れ子）が確保した 4KB に収まる

最悪の深さ = main からの最深の経路
            ＋ 割り込みごとの最深の経路（優先度が違うので入れ子になりうる。全部を足す）
            ＋ 例外の入り口でハードウェアが積む枠（FPU の文脈込みの 104 バイト × 割り込みの数）
ライブラリの関数（memcpy など、.su がない）は LIBC_FRAME バイトとして数える。
"""
import glob
import os
import re
import subprocess
import sys

STACK_BYTES = 4 * 1024
TOP_N = 15
LIBC_FRAME = 64  # .su のないライブラリ関数（memcpy・memset・__libc_init_array）の見積もり
EXC_FRAME = 104  # 例外の入り口の自動の積み（R0〜R3・R12・LR・PC・xPSR＋S0〜S15・FPSCR、整列込み）
ISRS = ("SysTick_Handler", "DMA2_Stream0_IRQHandler", "USART2_IRQHandler")
ROOT = "main"
ROOT_TU = "main_stm32f4"  # ターゲット版の main（CMake のコンパイラ判定の main と取り違えない）
# 呼び出し側の TU → 行き先の番地を取った TU（None は「ポート」＝ bsp の TU）
INDIRECT_GROUPS = {"scheduler": "firmware", "safety_fsm": "safety_fsm"}
BSP_PREFIX = "bsp_"
VECTOR_TU = "startup_stm32f401xe"  # 割り込みのベクタ表（ISR は根として別に数える）


def ours(path):
    """CMake のコンパイラ判定（CMakeFiles/<版>/CompilerIdC）の副産物は数えない。"""
    return "CompilerId" not in path


def read_su(build):
    frames = {}  # (tu, name) -> bytes
    bad = []
    rows = []
    for path in filter(ours, glob.glob(os.path.join(build, "**", "*.su"), recursive=True)):
        tu = os.path.basename(path).split(".")[0]
        for line in open(path, encoding="utf-8"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 3:
                continue
            loc, size, kind = parts
            name = loc.rsplit(":", 1)[-1]
            src = loc.split(":")[0]
            frames[(tu, name)] = int(size)
            rows.append((int(size), name, os.path.relpath(src) if os.path.isabs(src) else src, kind))
            if kind != "static":
                bad.append(f"{name}（{src}）の種類が {kind}（可変長配列か alloca）")
    return frames, rows, bad


NODE = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"')
EDGE = re.compile(r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s*targetname:\s*"([^"]+)"')


def bare(title):
    """静的な関数の title は「ファイルのパス:名前」なので、名前だけにする。"""
    return title.rsplit(":", 1)[-1] if "/" in title else title


def read_ci(build):
    """TU ごとに、定義された関数と呼び出しの辺を集める。"""
    defined = {}  # tu -> set(name)
    edges = {}  # (tu, name) -> set(callee name)
    for path in filter(ours, glob.glob(os.path.join(build, "**", "*.ci"), recursive=True)):
        tu = os.path.basename(path).split(".")[0]
        text = open(path, encoding="utf-8").read()
        names = set()
        for m in NODE.finditer(text):
            # 定義された関数は label にフレームの大きさ（"N bytes"）を持つ
            block = text[m.start() : text.find("}", m.end())]
            if "bytes" in block:
                names.add(bare(m.group(1)))
        defined[tu] = names
        for m in EDGE.finditer(text):
            edges.setdefault((tu, bare(m.group(1))), set()).add(bare(m.group(2)))
    return defined, edges


def address_taken(build, functions):
    """番地を取られた関数 → 番地を取った TU の集合（データの表か命令の即値の再配置から）。"""
    taken = {}
    objs = list(filter(ours, glob.glob(os.path.join(build, "**", "*.obj"), recursive=True)))
    for obj in objs:
        tu = os.path.basename(obj).split(".")[0]
        out = subprocess.run(["arm-none-eabi-objdump", "-r", obj], capture_output=True, text=True).stdout
        section = ""
        for line in out.splitlines():
            h = re.match(r"^RELOCATION RECORDS FOR \[(\S+)\]:", line)
            if h:
                section = h.group(1)
                continue
            # デバッグ情報（.debug_*）の再配置はすべての関数を指すので数えない。コード（リテラル）とデータの表だけ
            if not section.startswith((".text", ".rodata", ".data", ".isr_vector")):
                continue
            m = re.match(r"^[0-9a-f]+\s+(R_ARM_ABS32|R_ARM_THM_MOVW_ABS_NC|R_ARM_THM_MOVT_ABS)\s+(\S+)", line)
            if not m:
                continue
            sym = m.group(2).split("+")[0]
            if sym.startswith(".text."):
                sym = sym[len(".text.") :]
            if sym in functions:
                taken.setdefault(sym, set()).add(tu)
    return taken


class Graph:
    def __init__(self, frames, defined, edges, taken):
        self.frames = frames
        self.defined = defined
        self.edges = edges
        self.tus_of = {}
        for (tu, name) in frames:
            self.tus_of.setdefault(name, []).append(tu)
        self.groups = {}  # 番地を取った TU（bsp はまとめて "bsp"）→ 関数の (tu, name)
        self.unknown = []
        for name, takers in sorted(taken.items()):
            for taker in sorted(takers):
                if taker == VECTOR_TU:
                    continue
                group = "bsp" if taker.startswith(BSP_PREFIX) else taker
                if group not in ("bsp", *INDIRECT_GROUPS.values()):
                    self.unknown.append(f"{name}（{taker} で番地を取られている）")
                    continue
                key = self.resolve(taker, name)
                self.groups.setdefault(group, set()).update(key)
        self.memo = {}
        self.cycles = []

    def targets(self, caller_tu):
        return sorted(self.groups.get(INDIRECT_GROUPS.get(caller_tu, "bsp"), set()))

    def resolve(self, tu, name):
        if name in self.defined.get(tu, set()) and (tu, name) in self.frames:
            return [(tu, name)]
        return [(t, name) for t in self.tus_of.get(name, [])]

    def worst(self, key, path):
        """key からの最深の経路（バイト数と関数の並び）。"""
        if key in path:
            self.cycles.append(" → ".join(n for _, n in path[path.index(key) :]) + " → " + key[1])
            return 0, []
        if key in self.memo:
            return self.memo[key]
        tu, name = key
        own = self.frames.get(key, LIBC_FRAME)
        best = (0, [])
        for callee in sorted(self.edges.get(key, set())):
            if callee == "__indirect_call":
                cands = self.targets(tu)
            else:
                cands = self.resolve(tu, callee) or [("libc", callee)]
            for c in cands:
                if c[0] == "libc":
                    got = (LIBC_FRAME, [f"{c[1]}（ライブラリ、{LIBC_FRAME} と見積もる）"])
                else:
                    got = self.worst(c, path + [key])
                if got[0] > best[0]:
                    best = got
        res = (own + best[0], [f"{name} {own}"] + best[1])
        self.memo[key] = res
        return res


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    build = sys.argv[1]
    frames, rows, bad = read_su(build)
    defined, edges = read_ci(build)
    taken = address_taken(build, {n for (_, n) in frames})
    g = Graph(frames, defined, edges, taken)
    errors = list(bad)
    for u in g.unknown:
        errors.append(f"関数ポインタの行き先の表（INDIRECT_GROUPS）にない：{u}")
    for key in g.groups.get("bsp", set()):
        if "__indirect_call" in edges.get(key, set()):
            errors.append(f"ポートの関数 {key[1]} が関数ポインタ越しに呼んでいる")

    print(f"自分のフレームの大きい関数（上位{TOP_N}、.su）")
    print(f"{'バイト':>6}  {'関数':<32}{'種類':<8}ファイル")
    for size, name, src, kind in sorted(rows, key=lambda r: (-r[0], r[1]))[:TOP_N]:
        print(f"{size:>6}  {name:<32}{kind:<8}{src}")
    print()
    for group, keys in sorted(g.groups.items()):
        print(f"関数ポインタの行き先（{group}）：{len(keys)} 個（{'・'.join(sorted(n for _, n in keys))}）")
    print()

    roots = [k for k in frames if k == (ROOT_TU, ROOT)]
    if not roots:
        errors.append("main の .su がない")
        total_main = (0, [])
    else:
        total_main = g.worst(roots[0], [])
    print(f"main からの最深の経路：{total_main[0]} バイト")
    print("  " + " → ".join(total_main[1]))
    isr_sum = 0
    for isr in ISRS:
        keys = [k for k in frames if k[1] == isr]
        if not keys:
            errors.append(f"{isr} の .su がない")
            continue
        w = g.worst(keys[0], [])
        isr_sum += w[0]
        print(f"{isr} の最深の経路：{w[0]} バイト（{' → '.join(w[1])}）")
    exc = EXC_FRAME * len(ISRS)
    total = total_main[0] + isr_sum + exc
    print(f"例外の入り口の自動の積み：{EXC_FRAME} × {len(ISRS)} = {exc} バイト")
    print()
    for c in sorted(set(g.cycles)):
        errors.append(f"再帰（関数ポインタ越しを含む）：{c}")
    if total > STACK_BYTES:
        errors.append(f"最悪の深さ {total} バイトが確保した {STACK_BYTES} を超えた")
    for e in errors:
        print("NG:", e)
    print(
        f"最悪の深さ {total} / {STACK_BYTES} バイト（{100 * total / STACK_BYTES:.1f}%。main {total_main[0]}"
        f"＋割り込み {isr_sum}＋例外の枠 {exc}）、可変長配列 {len(bad)} 件、再帰 {len(set(g.cycles))} 件"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
