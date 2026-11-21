#!/usr/bin/env python3
"""契約テストの pty と、Renode の USART2（TCP のソケット端末）を中継する（P6、ADR-0006）。

    tools/renode_uart_relay.py --uart-port 3456 --monitor-port 3457 PTY_PATH      # 中継（契約テストの --target-cmd から）
    tools/renode_uart_relay.py --uart-port 3456 --monitor-port 3457 --measure 20  # 仮想時間と実時間の比だけを測る

Renode はコンテナの中で動くので、pty をコンテナの外に出さず、`emulation CreateServerSocketTerminal` の TCP を使う。
socat がない環境でも動くよう Python で書く（標準ライブラリだけ。Python 3.8 以上）。

ソケット端末は、つながっていない間の出力を捨てる。起動の順序（STATE → BOOT）を落とさないよう、Renode は
start せずに待たせ、(1) モニターに機械の名前のプロンプト（(nucleo)）が出る（＝読み込みが済み、USART2 の
ソケットができた）のを待ち、(2) USART2 のソケットにつないでから、(3) モニターに start を送る。Docker のポートの
転送は Renode より先に接続を受け、モニターは -e の命令より先に開くので、どちらも「つながった」だけでは判定しない。

仮想時間と実時間の比（#56）：契約テストは実時間で待つので、比が約0.7〜9倍の外だと判定が成り立たない
（0.7 倍未満だと上位断の10秒が14秒以内に来ない、9倍を超えると1秒ごとの HEARTBEAT の間が10秒を超える）。
--measure は比を測って範囲の外なら終了コード 1 にする（CI は契約テストの前にこれを流す）。中継のときも、10秒ごとと
終わるときに比を標準エラーに出す。
"""
import argparse
import os
import re
import select
import signal
import socket
import sys
import termios
import time
import tty

RATIO_MIN = 0.7
RATIO_MAX = 9.0
PROMPT = b"(nucleo)"


class Monitor:
    """Renode のモニター（telnet）。行末は CR LF。"""

    def __init__(self, host, port):
        self.host, self.port = host, port
        self.sock = None

    def wait_ready(self, timeout=90):
        """機械の名前のプロンプトが出るまで、接続をやり直す。モニターは -e の命令より先に開くので、応答だけでは足りない。"""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                m = socket.create_connection((self.host, self.port), timeout=2)
            except OSError:
                time.sleep(0.3)
                continue
            m.settimeout(1)
            seen = b""
            while time.monotonic() < deadline:
                try:
                    data = m.recv(4096)
                except socket.timeout:
                    m.sendall(b"\r\n")
                    continue
                if not data:
                    break  # Docker のポートの転送だけが受けて、Renode がまだいない
                seen += data
                if PROMPT in seen:
                    self.sock = m
                    return True
            m.close()
            time.sleep(0.3)
        return False

    def command(self, cmd, timeout=10):
        """命令を送り、次のプロンプトまでの出力を返す。"""
        self.sock.settimeout(0.5)
        try:
            while self.sock.recv(4096):
                pass
        except (socket.timeout, BlockingIOError):
            pass
        self.sock.sendall(cmd.encode() + b"\r\n")
        out = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            out += data
            if PROMPT in out:  # 前の出力は読み捨ててあるので、プロンプトが出たら命令の出力は終わり
                break
        return re.sub(r"\x1b\[[0-9;]*m", "", out.decode(errors="replace"))

    def virtual_seconds(self):
        """`emulation GetTimeSourceInfo` の経過した仮想時間（秒）。"""
        text = self.command("emulation GetTimeSourceInfo")
        m = re.search(r"Elapsed Virtual Time:\s*(\d+):(\d+):(\d+(?:\.\d+)?)", text)
        if not m:
            raise RuntimeError(f"仮想時間が読めない：{text!r}")
        h, mi, s = m.groups()
        return int(h) * 3600 + int(mi) * 60 + float(s)


def log(msg):
    print(f"relay: {msg}", file=sys.stderr, flush=True)


def ratio_line(v0, t0, mon):
    v = mon.virtual_seconds() - v0
    t = time.monotonic() - t0
    r = v / t if t > 0 else 0.0
    return r, f"仮想時間 {v:.2f} 秒 / 実時間 {t:.2f} 秒 = {r:.2f} 倍（契約テストの許容範囲 {RATIO_MIN}〜{RATIO_MAX} 倍）"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pty", nargs="?")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--uart-port", type=int, default=3456)
    ap.add_argument("--monitor-port", type=int, default=3457)
    ap.add_argument("--measure", type=float, help="中継せず、この秒数（実時間）だけ動かして比を測る")
    a = ap.parse_args()

    mon = Monitor(a.host, a.monitor_port)
    if not mon.wait_ready():
        log("Renode のモニターが応答しない")
        return 1

    if a.measure:
        v0 = mon.virtual_seconds()
        t0 = time.monotonic()
        mon.command("start")
        time.sleep(a.measure)
        r, line = ratio_line(v0, t0, mon)
        print(line, flush=True)
        if not RATIO_MIN <= r <= RATIO_MAX:
            print("NG: 仮想時間と実時間の比が契約テストの許容範囲の外", flush=True)
            return 1
        return 0

    fd = os.open(a.pty, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd, termios.TCSANOW)
    sock = socket.create_connection((a.host, a.uart_port), timeout=5)
    time.sleep(0.2)
    v0 = mon.virtual_seconds()
    t0 = time.monotonic()
    mon.command("start")
    sock.setblocking(False)

    def finish(*_):
        try:
            log("終了時 " + ratio_line(v0, t0, mon)[1])
        except Exception as e:  # noqa: BLE001  終わりぎわの計測の失敗は中継の結果に関係しない
            log(f"終了時の比を測れなかった：{e}")
        sys.exit(0)

    signal.signal(signal.SIGTERM, finish)
    next_log = time.monotonic() + 10
    while True:
        r, _, _ = select.select([fd, sock], [], [], 1.0)
        if fd in r:
            data = os.read(fd, 4096)
            if not data:
                finish()
            sock.sendall(data)
        if sock in r:
            data = sock.recv(4096)
            if not data:
                finish()
            os.write(fd, data)
        if time.monotonic() >= next_log:
            next_log += 10
            log(ratio_line(v0, t0, mon)[1])


if __name__ == "__main__":
    sys.exit(main())
