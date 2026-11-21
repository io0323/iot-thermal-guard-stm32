"""ホスト版を pty 越しに動かし、疑似ウォッチドッグによる再起動を確かめる（P3、詳細設計書 第7章・第8章）。

実行（リポジトリの直下で、先に host-debug をビルドしておく）::

    uv run --with "iotspec @ git+https://github.com/io0323/iot-common-spec@v0.2.1" --with pytest pytest tests/host_integration

確かめること（open-questions #34・#35・#38）:

1. 最初のフレームが STATE → EVENT BOOT
2. hang から 400ms 以上 2600ms 以内に、別の boot_id の STATE → BOOT が来る
   （上限＝ウォッチドッグ 500＋health の周期 100＋INIT の最長 2000。下限はウォッチドッグを待たずに再起動していないことの確認）
3. 再起動後の各種別（STATE・EVENT・TELEMETRY・HEARTBEAT）の seq が 0 から始まり、uptime_ms も 0 から数え直す
4. ログの boot の行が power_on・0 → watchdog・1。シナリオの時刻は続くので、hang は1回だけ起きる
"""

from __future__ import annotations

import json
import os
import select
import signal
import subprocess
import time
import tty
from dataclasses import dataclass
from pathlib import Path

import pytest
from iotspec.uart.frames import Discarded, Event, FrameReader, Heartbeat, State, Telemetry

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "build" / "host" / "mcu_c_01"
SCENARIO = ROOT / "tests" / "scenario" / "data" / "hang_reboot.csv"

HANG_AT_MS = 3000  # hang_reboot.csv の hang の時刻
UPPER_MS = 500 + 100 + 2000  # #34
LOWER_MS = 400  # #34
RUN_SECONDS = 8.0  # 再起動後の TELEMETRY・HEARTBEAT が来るまで
EVENT_BOOT = 1
ENV_KEYS = ("MCU_C_01_RESET_REASON", "MCU_C_01_RESET_COUNT", "MCU_C_01_SCENARIO_ELAPSED_MS")


@dataclass(frozen=True)
class Rx:
    t: float  # 受け取った時刻（time.monotonic）
    frame: object


@dataclass(frozen=True)
class Run:
    rx: list[Rx]
    log: list[dict]


def _run(tmp_path: Path) -> Run:
    if not BINARY.exists():
        pytest.fail(f"{BINARY} がない。先に cmake --build --preset host-debug を実行する")
    log_path = tmp_path / "mcu.jsonl"
    master, slave = os.openpty()
    tty.setraw(slave)
    slave_path = os.ttyname(slave)
    env = {k: v for k, v in os.environ.items() if k not in ENV_KEYS}  # 電源投入から始める
    proc = subprocess.Popen(
        [str(BINARY), "--scenario", str(SCENARIO), "--log", str(log_path), slave_path],
        env=env,
        cwd=ROOT,
    )
    reader = FrameReader()  # 0x00 で区切り、decode_frame で復号する
    rx: list[Rx] = []
    try:
        deadline = time.monotonic() + RUN_SECONDS
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.05)
            if not ready:
                continue
            try:
                data = os.read(master, 4096)
            except OSError:
                continue  # 再起動の間に相手側がいなくなる瞬間（Linux は EIO）
            now = time.monotonic()
            rx.extend(Rx(now, f) for f in reader.feed(data))
        assert proc.poll() is None, f"ホスト版が途中で終了した（終了コード {proc.returncode}）"
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=5)
        os.close(slave)
        os.close(master)
    log = [json.loads(line) for line in log_path.read_text().splitlines() if line.strip()]
    return Run(rx, log)


@pytest.fixture(scope="module")
def run(tmp_path_factory: pytest.TempPathFactory) -> Run:
    return _run(tmp_path_factory.mktemp("reboot"))


def _state_indices(run: Run) -> list[int]:
    return [i for i, r in enumerate(run.rx) if isinstance(r.frame, State)]


def _second_boot(run: Run) -> int:
    states = _state_indices(run)
    assert len(states) == 2, f"STATE は2件のはず（起動と再起動）: {len(states)} 件"
    return states[1]


def _t_zero(run: Run) -> float:
    """1回目の起動の uptime 0 にあたる時刻。シナリオの時刻はこのプロセスの uptime と同じ起点。"""
    boot1 = run.rx[1]
    assert isinstance(boot1.frame, Event)
    return boot1.t - boot1.frame.uptime_ms / 1000.0


def test_no_discarded_frames(run: Run) -> None:
    bad = [r.frame for r in run.rx if isinstance(r.frame, Discarded)]
    assert not bad, bad


def test_1_first_frames_are_state_then_boot(run: Run) -> None:
    assert isinstance(run.rx[0].frame, State)
    first = run.rx[1].frame
    assert isinstance(first, Event) and first.code == EVENT_BOOT


def test_2_reboot_within_limits_with_new_boot_id(run: Run) -> None:
    t_zero = _t_zero(run)
    t_hang = t_zero + HANG_AT_MS / 1000.0
    i = _second_boot(run)
    elapsed_ms = (run.rx[i].t - t_hang) * 1000.0
    print(f"\nhang から新しい STATE まで {elapsed_ms:.0f} ms（下限 {LOWER_MS}、上限 {UPPER_MS}）")
    assert LOWER_MS <= elapsed_ms <= UPPER_MS
    nxt = run.rx[i + 1].frame
    assert isinstance(nxt, Event) and nxt.code == EVENT_BOOT
    s1, s2 = run.rx[0].frame, run.rx[i].frame
    assert isinstance(s1, State) and isinstance(s2, State)
    assert s1.boot_id != s2.boot_id
    print(f"boot_id {s1.boot_id:08x} → {s2.boot_id:08x}")


def test_3_seq_and_uptime_restart(run: Run) -> None:
    i = _second_boot(run)
    before = [r.frame for r in run.rx[:i]]
    after = [r.frame for r in run.rx[i:]]
    for cls in (State, Event, Telemetry, Heartbeat):
        first = next((f for f in after if isinstance(f, cls)), None)
        assert first is not None, f"再起動後に {cls.__name__} が来ていない"
        assert first.seq == 0, f"{cls.__name__} の seq が {first.seq}"
    t_zero = _t_zero(run)
    for cls in (Telemetry, Heartbeat):
        last_before = [f for f in before if isinstance(f, cls)][-1]
        first_rx = next(r for r in run.rx[i:] if isinstance(r.frame, cls))
        first_after = first_rx.frame
        # 数え直していなければ、1回目の起動からの経過（hang の時刻より 1.5秒以上あと）になるはず
        continued_ms = (first_rx.t - t_zero) * 1000.0
        print(
            f"{cls.__name__} の uptime_ms：再起動前の最後 {last_before.uptime_ms}、再起動後の最初 {first_after.uptime_ms}"
            f"（続いていれば約 {continued_ms:.0f}）"
        )
        assert first_after.uptime_ms < HANG_AT_MS < continued_ms - 1000


def test_4_log_boot_lines(run: Run) -> None:
    boots = [e for e in run.log if e["kind"] == "boot"]
    assert [(b["reset_reason"], b["reset_count"]) for b in boots] == [("power_on", 0), ("watchdog", 1)]
    states = [run.rx[j].frame for j in _state_indices(run)]
    assert [b["boot_id"] for b in boots] == [f"{s.boot_id:08x}" for s in states]
    # 再起動の前に診断値を書き、次のプロセスは同じファイルに追記している（#38）
    kinds = [e["kind"] for e in run.log]
    second = [k for k, e in enumerate(run.log) if e["kind"] == "boot"][1]
    assert "diag" in kinds[:second]
