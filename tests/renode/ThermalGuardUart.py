"""Robot Framework のキーワード：Renode の USART2 のフレームを読み書きする（P6、ADR-0006）。

Renode の USART2 を TCP のソケット端末（`emulation CreateServerSocketTerminal`）に出し、このライブラリが
つないで、iot-common-spec v0.2.1（submodule）の `FrameReader`・`decode_frame`・`encode_frame` で復号・符号化する。
フレームはバイナリなので、Renode の文字列のテスターは使わない。

iotspec は Python 3.12 のパッケージだが、ここで使う `iotspec.uart`（frames・cobs・crc）と `iotspec.constants` は
標準ライブラリだけに依存するので、Renode のイメージにインストールせず、submodule の src をそのまま読み込む。
"""
from __future__ import annotations

import math
import socket
import sys
import threading
import time
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[2]
_IOTSPEC_SRC = _ROOT / "external" / "iot-common-spec" / "src"
if str(_IOTSPEC_SRC) not in sys.path:
    sys.path.insert(0, str(_IOTSPEC_SRC))

from iotspec.uart.frames import (  # noqa: E402
    Ack,
    Cmd,
    Discarded,
    Event,
    FrameReader,
    Heartbeat,
    State,
    Telemetry,
    encode_frame,
)

OP_SET_FAN_OVERRIDE = 0x01
OP_CLEAR_OVERRIDE = 0x02
EVENT_BOOT = 1
STATE_NAMES = {0: "INIT", 1: "NORMAL", 2: "WARNING", 3: "CRITICAL", 4: "SENSOR_FAULT"}

# NTC の生値（tools/gen_ntc_table.py と同じ式、詳細設計書 第3章）
_R_PULL = 10000.0
_R25 = 10000.0
_BETA = 3435.0


class ThermalGuardUart:
    ROBOT_LIBRARY_SCOPE = "GLOBAL"

    def __init__(self) -> None:
        self._sock: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._reader = FrameReader()
        self._frames: list = []
        self._last_rx = 0.0
        self._seq: dict[int, int] = {}
        self._cmd_ref = 0x4000

    # ---- つなぐ・切る ----
    def connect_uart(self, port, host="127.0.0.1", timeout=30):
        """Renode の USART2 のソケット端末につなぎ、受信を始める。"""
        self.disconnect_uart()
        deadline = time.monotonic() + float(timeout)
        while True:
            try:
                self._sock = socket.create_connection((host, int(port)), timeout=2)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.2)
        self._sock.settimeout(0.2)
        self._reader = FrameReader()
        self._frames = []
        self._seq = {}
        self._thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._thread.start()

    def disconnect_uart(self):
        sock, self._sock = self._sock, None
        if sock is not None:
            sock.close()
        if self._thread is not None:
            self._thread.join(timeout=2)
            self._thread = None

    def _rx_loop(self):
        while self._sock is not None:
            try:
                data = self._sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            if not data:
                return
            with self._lock:
                self._last_rx = time.monotonic()
                for f in self._reader.feed(data):
                    self._frames.append(f)

    def drain_uart(self, quiet=0.5, timeout=10):
        """受信が quiet 秒止まるまで待つ（RunFor のあと、ソケットに残ったバイトを読み切る）。"""
        deadline = time.monotonic() + float(timeout)
        time.sleep(float(quiet))
        while time.monotonic() < deadline:
            with self._lock:
                idle = time.monotonic() - self._last_rx
            if idle >= float(quiet):
                return
            time.sleep(0.1)

    # ---- 読む ----
    def frame_count(self):
        """今までに受けたフレームの数（あとで「ここから先」を見る印に使う）。"""
        with self._lock:
            return len(self._frames)

    def _since(self, mark):
        with self._lock:
            return list(self._frames[int(mark):])

    def log_frames(self, mark=0):
        """受けたフレームを Robot のログに出す。"""
        for f in self._since(mark):
            print(f)

    def frames_should_start_with_state_then_boot(self, mark=0):
        """mark から先の最初のフレームが STATE で、その次の EVENT が BOOT（code 1）であること。seq は 0 から。"""
        frames = [f for f in self._since(mark) if not isinstance(f, Discarded)]
        if not frames or not isinstance(frames[0], State):
            raise AssertionError(f"最初のフレームが STATE でない：{frames[:3]}")
        events = [f for f in frames if isinstance(f, Event)]
        if not events or events[0].code != EVENT_BOOT:
            raise AssertionError(f"STATE の次の EVENT が BOOT でない：{events[:2]}")
        if frames[0].seq != 0 or events[0].seq != 0:
            raise AssertionError(f"起動直後の seq が 0 でない：STATE {frames[0].seq}、EVENT {events[0].seq}")
        return frames[0].boot_id

    def periodic_frames_should_be_every_second(self, kind, minimum=3, mark=0):
        """TELEMETRY か HEARTBEAT の uptime_ms の間隔が、すべて 1000ms であること。"""
        cls = {"TELEMETRY": Telemetry, "HEARTBEAT": Heartbeat}[kind]
        times = [f.uptime_ms for f in self._since(mark) if isinstance(f, cls)]
        if len(times) < int(minimum):
            raise AssertionError(f"{kind} が {len(times)} 件しかない（{minimum} 件以上を期待）")
        gaps = [b - a for a, b in zip(times, times[1:])]
        if any(g != 1000 for g in gaps):
            raise AssertionError(f"{kind} の間隔が 1000ms でない：{gaps}")
        return gaps

    def last_telemetry(self, mark=0):
        tel = [f for f in self._since(mark) if isinstance(f, Telemetry)]
        if not tel:
            raise AssertionError("TELEMETRY を受けていない")
        return tel[-1]

    def last_state_should_be(self, name, mark=0):
        """最後の TELEMETRY の state が name（NORMAL・CRITICAL など）であること。"""
        got = STATE_NAMES.get(self.last_telemetry(mark).state)
        if got != name:
            raise AssertionError(f"state が {name} でない：{got}")

    def ack_should_be(self, cmd_ref, status=1, applied=None, mark=0):
        """cmd_ref の ACK が status（1 = done）で、applied（指定したとき）であること。"""
        acks = [f for f in self._since(mark) if isinstance(f, Ack) and f.cmd_ref == int(cmd_ref)]
        if not acks:
            raise AssertionError(f"cmd_ref {cmd_ref} の ACK が来ない")
        a = acks[-1]
        if a.status != int(status) or (applied is not None and a.applied != int(applied)):
            raise AssertionError(f"ACK が期待と違う：{a}（status {status}、applied {applied} を期待）")
        return a

    def count_frames(self, kind, mark=0):
        cls = {"STATE": State, "EVENT": Event, "TELEMETRY": Telemetry, "HEARTBEAT": Heartbeat, "ACK": Ack}[kind]
        return sum(1 for f in self._since(mark) if isinstance(f, cls))

    # ---- 送る ----
    def _next_seq(self, frame_type):
        s = self._seq.get(frame_type, 0)
        self._seq[frame_type] = (s + 1) % 0x10000
        return s

    def _send(self, frame):
        if self._sock is None:
            raise AssertionError("UART につながっていない")
        self._sock.sendall(encode_frame(frame))

    def send_heartbeat(self, uptime_ms=0):
        self._send(Heartbeat(self._next_seq(Heartbeat.TYPE), int(uptime_ms)))

    def send_set_fan_override(self, duty_pm, ttl_s=60):
        """SET_FAN_OVERRIDE を送り、cmd_ref を返す。"""
        self._cmd_ref = (self._cmd_ref + 1) % 0x10000
        self._send(Cmd(self._next_seq(Cmd.TYPE), self._cmd_ref, OP_SET_FAN_OVERRIDE, int(ttl_s), (int(duty_pm),)))
        return self._cmd_ref

    # ---- 温度 ----
    def raw_for_temperature(self, temp_c):
        """温度（℃）の ADC の生値（tools/gen_ntc_table.py と同じ式）。"""
        t_k = float(temp_c) + 273.15
        r = _R25 * math.exp(_BETA * ((1.0 / t_k) - (1.0 / 298.15)))
        return int(math.floor(4095.0 * r / (r + _R_PULL) + 0.5))

    def ramp_raw_values(self, start_c, end_c, step_c):
        """start_c から end_c まで step_c ずつの温度の生値の並び（変化率の上限 5.00℃ より遅く上げる）。"""
        out = []
        t = float(start_c)
        step = float(step_c)
        while t < float(end_c):
            out.append(self.raw_for_temperature(t))
            t += step
        out.append(self.raw_for_temperature(end_c))
        return out

    # ---- レジスタの値 ----
    def monitor_value_to_int(self, text):
        """Renode のモニターの出力（"0x00000540" など）を整数にする。"""
        return int(str(text).strip().splitlines()[-1].strip(), 0)

    def stack_peak_from_dump(self, text, size=4096):
        """`sysbus ReadBytes 0x20000000 4096` の出力から、塗りの 0xA5 が残っていない量（stack_peak と同じ計算）。"""
        vals = [int(x, 16) for x in str(text).replace("[", " ").replace("]", " ").replace(",", " ").split()
                if x.lower().startswith("0x")]
        if len(vals) < int(size):
            raise AssertionError(f"スタックの内容が {len(vals)} バイトしか読めない")
        i = 0
        while i < len(vals) and vals[i] == 0xA5:
            i += 1
        return int(size) - i
