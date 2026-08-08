#!/usr/bin/env python3
"""
시리얼 포트 열거 (Windows / macOS / Linux 공통)

  python3 list_ports.py                포트 목록을 사람이 읽는 형태로 출력
  python3 list_ports.py --first        가장 가능성 높은 포트 하나만 출력 (스크립트용)
  python3 list_ports.py --vscode       VS Code tasks.json 의 command 입력용
                                       'value|label|detail' 한 줄에 하나

보드는 CP2102N(VID 0x10C4 / PID 0xEA60)을 통해 붙는다. 그 장치를 최우선으로 정렬하고
블루투스나 디버그 프로브의 가상 포트처럼 대상이 아닌 것은 뒤로 보낸다.

macOS 는 /dev/cu.* 를 쓴다. /dev/tty.* 는 DCD 를 기다려 블록된다.
"""

import argparse
import sys

try:
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial 이 필요하다:  python3 -m pip install pyserial")


# CP2102N — NU87-TinyDK 의 USB-UART 브리지
CP210X_VID = 0x10C4
CP210X_PID = 0xEA60

# 대상이 아닌 포트 (블루투스, 디버그 프로브 VCP 등)
EXCLUDE_HINTS = ("bluetooth", "wlan-debug", "debug-console")


def score(p):
    """작을수록 우선. 보드일 가능성이 높은 순으로 정렬한다."""
    name = (p.device or "").lower()
    desc = (p.description or "").lower()

    if any(h in name or h in desc for h in EXCLUDE_HINTS):
        return 90

    if p.vid == CP210X_VID and p.pid == CP210X_PID:
        return 0                      # 보드의 CP2102N
    if p.vid == CP210X_VID:
        return 1                      # 다른 CP210x
    if "usbserial" in name or "ttyusb" in name:
        return 10                     # 일반 USB-UART
    if "usbmodem" in name or "ttyacm" in name:
        return 20                     # CDC (ST-LINK VCP 등)
    if name.startswith("com"):
        return 15                     # Windows
    return 50


def collect():
    ports = [p for p in list_ports.comports()]
    ports.sort(key=lambda p: (score(p), p.device))
    return ports


def detail(p):
    bits = []
    if p.description and p.description != "n/a":
        bits.append(p.description)
    if p.vid is not None and p.pid is not None:
        bits.append(f"{p.vid:04X}:{p.pid:04X}")
    if p.serial_number:
        bits.append(f"SN {p.serial_number}")
    return "  ".join(bits) if bits else "-"


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--first", action="store_true", help="가장 가능성 높은 포트 하나만 출력")
    g.add_argument("--vscode", action="store_true", help="VS Code 입력용 'value|label|detail'")
    args = ap.parse_args()

    ports = collect()

    if args.first:
        if not ports:
            sys.exit("시리얼 포트를 찾을 수 없다. 케이블과 드라이버를 확인할 것")
        print(ports[0].device)
        return 0

    if args.vscode:
        # 목록이 비어도 최소 한 줄은 내보내야 선택 UI 가 뜬다
        if not ports:
            print("|(포트 없음)|USB 케이블이 데이터 전송용인지 확인할 것")
            return 0
        for p in ports:
            mark = " ★" if (p.vid == CP210X_VID and p.pid == CP210X_PID) else ""
            print(f"{p.device}|{p.device}{mark}|{detail(p)}")
        return 0

    if not ports:
        print("시리얼 포트 없음")
        print()
        print("  - USB 케이블이 충전 전용이 아닌지 확인 (가장 흔한 원인)")
        print("  - Windows 는 CP210x VCP 드라이버가 필요하다")
        print("  - Linux 는 dialout 그룹 가입 후 재로그인이 필요하다")
        return 1

    print(f"{'포트':<28} {'설명'}")
    print("-" * 72)
    for p in ports:
        mark = " ★" if (p.vid == CP210X_VID and p.pid == CP210X_PID) else ""
        print(f"{p.device + mark:<28} {detail(p)}")
    print()
    print("★ = NU87-TinyDK 의 CP2102N")
    return 0


if __name__ == "__main__":
    sys.exit(main())
