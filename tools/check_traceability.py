#!/usr/bin/env python3
"""docs/test-spec.md のトレーサビリティを検査する（P7）。

1. 要件定義・基本設計書（docs/design/01-requirements-basic-design.md）の FR・NFR の ID が、
   すべて docs/test-spec.md の対応表に1回以上ある
2. 表に書いたテストが実在する
   - `tests/…/ファイル::名前`：C は RUN_TEST される関数、Python は def の関数、
     Robot はテストケース名（「：」より前が一致すればよい）
   - `contract:名前`：iotspec-contract の mcu-c-01 のチェック（submodule の roles/mcu_c_01.py）
   - `ci:ジョブ`：.github/workflows/ci.yml のジョブ
   - `tools/…`：リポジトリのファイル
3. 「実機フェーズ」のリンク（hardware-phase.md#hw-nn）の行き先が docs/hardware-phase.md にある
4. FR・NFR の各行が、テストか実機フェーズのリンクのどちらかを持つ（空欄がない）

使い方：python3 tools/check_traceability.py [リポジトリのルート]
失敗があれば理由を1行ずつ出し、終了コード 1 で終わる。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ID_RE = re.compile(r"\b((?:FR|NFR)-\d{2})\b")
ROW_RE = re.compile(r"^\|\s*((?:FR|NFR)-\d{2})\s*\|")
CODE_RE = re.compile(r"`([^`]+)`")
TEST_REF_RE = re.compile(r"^(tests/[^:\s]+)::(.+)$")
CONTRACT_REF_RE = re.compile(r"^contract:([a-z0-9_]+)$")
CI_REF_RE = re.compile(r"^ci:([a-z0-9_-]+)$")
TOOL_REF_RE = re.compile(r"^(tools/[A-Za-z0-9_./-]+)$")
HW_LINK_RE = re.compile(r"hardware-phase\.md#(hw-\d{2})")
HW_ANCHOR_RE = re.compile(r'<a id="(hw-\d{2})"></a>')
REQ_TABLE_RE = re.compile(r"^\|\s*((?:FR|NFR)-\d{2})\s*\|", re.MULTILINE)
CONTRACT_CHECK_RE = re.compile(r'@check\(\s*ROLE,\s*"([a-z0-9_]+)"', re.MULTILINE)
CI_JOB_RE = re.compile(r"^  ([a-z0-9_-]+):\s*$", re.MULTILINE)


def requirement_ids(design: Path) -> list[str]:
    """設計書の表の行頭にある FR・NFR の ID（出てくる順、重複なし）。"""
    seen: list[str] = []
    for m in REQ_TABLE_RE.finditer(design.read_text(encoding="utf-8")):
        if m.group(1) not in seen:
            seen.append(m.group(1))
    return seen


def test_names(path: Path) -> set[str]:
    """ファイルが持つテストの名前。"""
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".c":
        return set(re.findall(r"RUN_TEST\(\s*([A-Za-z0-9_]+)", text))
    if path.suffix == ".py":
        return set(re.findall(r"^def (test_[A-Za-z0-9_]+)", text, re.MULTILINE))
    if path.suffix == ".robot":
        return robot_test_names(text)
    return set()


def robot_test_names(text: str) -> set[str]:
    """Robot の *** Test Cases *** の名前と、その「：」より前。"""
    names: set[str] = set()
    in_cases = False
    for line in text.splitlines():
        if line.startswith("***"):
            in_cases = "Test Cases" in line
            continue
        if in_cases and line and not line[0].isspace():
            names.add(line.strip())
            names.add(line.split("：", 1)[0].strip())
    return names


class Checker:
    """検査の状態と失敗の一覧。"""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.errors: list[str] = []
        self._tests: dict[str, set[str]] = {}
        roles = root / "external/iot-common-spec/src/iotspec/contract/roles/mcu_c_01.py"
        self.contract_checks = set(CONTRACT_CHECK_RE.findall(roles.read_text(encoding="utf-8")))
        ci = (root / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        self.ci_jobs = set(CI_JOB_RE.findall(ci.split("\njobs:", 1)[1]))
        hw = (root / "docs/hardware-phase.md").read_text(encoding="utf-8")
        self.hw_anchors = set(HW_ANCHOR_RE.findall(hw))

    def ref_exists(self, ref: str) -> bool | None:
        """参照が実在すれば True、実在しなければ False、参照の形でなければ None。"""
        m = TEST_REF_RE.match(ref)
        if m:
            path = self.root / m.group(1)
            if not path.is_file():
                return False
            if m.group(1) not in self._tests:
                self._tests[m.group(1)] = test_names(path)
            return m.group(2).strip() in self._tests[m.group(1)]
        m = CONTRACT_REF_RE.match(ref)
        if m:
            return m.group(1) in self.contract_checks
        m = CI_REF_RE.match(ref)
        if m:
            return m.group(1) in self.ci_jobs
        m = TOOL_REF_RE.match(ref)
        if m:
            return (self.root / m.group(1)).is_file()
        return None

    def check_line(self, where: str, line: str) -> tuple[int, int]:
        """1行の参照とリンクを確かめ、（実在する参照の数、実機フェーズのリンクの数）を返す。"""
        refs = 0
        for ref in CODE_RE.findall(line):
            ok = self.ref_exists(ref)
            if ok is False:
                self.errors.append(f"{where}: 実在しない参照 `{ref}`")
            elif ok:
                refs += 1
        links = HW_LINK_RE.findall(line)
        for anchor in links:
            if anchor not in self.hw_anchors:
                self.errors.append(f"{where}: hardware-phase.md に #{anchor} がない")
        return refs, len(links)


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    spec_path = root / "docs/test-spec.md"
    ids = requirement_ids(root / "docs/design/01-requirements-basic-design.md")
    if not ids:
        print("設計書から FR・NFR の ID を読めない")
        return 1
    checker = Checker(root)
    rows: dict[str, int] = {}
    for no, line in enumerate(spec_path.read_text(encoding="utf-8").splitlines(), start=1):
        where = f"docs/test-spec.md:{no}"
        refs, links = checker.check_line(where, line)
        m = ROW_RE.match(line)
        if m:
            rows[m.group(1)] = rows.get(m.group(1), 0) + 1
            if refs + links == 0:
                checker.errors.append(f"{where}: {m.group(1)} にテストも実機フェーズのリンクもない")
    spec_ids = set(ID_RE.findall(spec_path.read_text(encoding="utf-8")))
    for rid in ids:
        if rid not in spec_ids:
            checker.errors.append(f"docs/test-spec.md: {rid} がない")
        elif rid not in rows:
            checker.errors.append(f"docs/test-spec.md: {rid} の対応表の行がない")
    for rid in rows:
        if rid not in ids:
            checker.errors.append(f"docs/test-spec.md: {rid} は設計書にない ID")
    for err in checker.errors:
        print(err)
    print(f"トレーサビリティ：要件 {len(ids)} 件、表の行 {len(rows)} 件、失敗 {len(checker.errors)} 件")
    return 1 if checker.errors else 0


if __name__ == "__main__":
    sys.exit(main())
