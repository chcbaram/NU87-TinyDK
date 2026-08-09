#!/usr/bin/env python3
"""실행 중인 보드에 펌웨어를 밀어 넣는다.

flash.py 와 다르다. flash.py 는 다운로드 모드로 진입시켜 플래시를 통째로
굽고, 이쪽은 돌고 있는 펌웨어의 CLI 로 반대편 OTA 슬롯에 쓴다. 실패해도
지금 슬롯이 그대로 남아 있어 보드가 죽지 않는다.

펌웨어 쪽은 hw/driver/ota.c 의 otaReceive() 다.

    python3 ota.py --port /dev/tty.usbserial-0001 --image km0_km4_image2.bin
    python3 ota.py --host nu87-tinydk.local      --image km0_km4_image2.bin

USB 는 선이 하나라 CLI 와 데이터가 같은 곳으로 흐른다. WiFi 는 CLI(텔넷 23)와
데이터(5000)를 따로 열어 굵은 흐름이 CLI 에코에 끼지 않게 한다.
"""

import argparse
import socket
import sys
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("pyserial 이 필요하다: pip3 install pyserial")


CHUNK = 1024          # 펌웨어의 OTA_CHUNK_MAX 이하여야 한다
ACK_TIMEOUT = 10.0    # 섹터 지우기가 낀 청크는 느리다
RESEND_MAX = 5

TELNET_PORT = 23
DATA_PORT = 5000
DATA_CH = 5           # HW_UART_CH_NET_DATA (_DEF_UART5)


class Link:
    """CLI 와 데이터를 같은 얼굴로 다룬다. 통로가 하나든 둘이든 위쪽은 모른다."""

    def __init__(self, cli, dat):
        self.cli = cli
        self.dat = dat

    def write_cli(self, data):
        self.cli.write(data)

    def write_dat(self, data):
        self.dat.write(data)

    def read_dat(self, n=1):
        return self.dat.read(n)

    def flush_in(self):
        while self.dat.read(4096):
            pass


class SerialIO:
    def __init__(self, port, baud):
        self.s = serial.Serial(port, baud, timeout=0.2)
        self.s.reset_input_buffer()

    def write(self, data):
        self.s.write(data)

    def read(self, n=1):
        return self.s.read(n)

    def close(self):
        self.s.close()


class SockIO:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=10.0)
        self.s.settimeout(0.2)

    def write(self, data):
        self.s.sendall(data)

    def read(self, n=1):
        try:
            return self.s.recv(n)
        except socket.timeout:
            return b""

    def close(self):
        self.s.close()


class OtaError(Exception):
    pass


def read_line(io, timeout=ACK_TIMEOUT):
    """개행까지 한 줄. 보드가 보내는 말은 전부 한 줄짜리다."""
    end = time.time() + timeout
    line = b""

    while time.time() < end:
        data = io.read(1)
        if not data:
            continue
        if data == b"\n":
            text = line.strip().decode("utf-8", "replace")
            if text:
                return text
            line = b""
            continue
        line += data

    raise OtaError("보드가 응답하지 않는다")


def wait_for(io, expect, timeout=ACK_TIMEOUT):
    """기대한 말이 나올 때까지 읽는다. 에코와 로그가 섞여 들어온다."""
    end = time.time() + timeout

    while time.time() < end:
        line = read_line(io, timeout=max(0.1, end - time.time()))
        if line == expect:
            return line
        if line.startswith("e") and len(line) == 5:
            raise OtaError(f"보드 오류 0x{line[1:]}")
        if line == "r":
            return line

    raise OtaError(f"'{expect}' 를 받지 못했다")


def frame(data):
    """len(2 LE) | data | sum(1). 링크에서 한 바이트가 밀려도 이 청크만 다시 보낸다."""
    return len(data).to_bytes(2, "little") + data + bytes([sum(data) & 0xFF])


def update(link, image, show_info=None):
    payload = open(image, "rb").read()
    crc = zlib.crc32(payload) & 0xFFFFFFFF

    print(f"\n▶ 이미지  {image}")
    print(f"  크기    {len(payload)} B")
    print(f"  crc32   0x{crc:08X}")

    if show_info:
        for line in show_info:
            print("  " + line)

    link.flush_in()
    ch = f" {DATA_CH}" if link.cli is not link.dat else ""
    link.write_cli(f"ota write {len(payload)} {crc}{ch}\r\n".encode())
    wait_for(link.dat, "ready", timeout=20.0)

    print()
    sent = 0
    resend = 0
    started = time.time()

    while sent < len(payload):
        chunk = payload[sent:sent + CHUNK]
        try:
            link.write_dat(frame(chunk))
        except KeyboardInterrupt:
            link.write_dat(b"\x00\x00")     # 길이 0 = 중지
            raise

        if wait_for(link.dat, "a") == "r":
            resend += 1
            if resend > RESEND_MAX:
                raise OtaError("같은 청크가 계속 깨진다")
            continue

        resend = 0
        sent += len(chunk)

        pct = sent * 100 // len(payload)
        rate = sent / max(0.001, time.time() - started) / 1024
        print(f"\r  전송 {pct:3d}%  {sent}/{len(payload)} B  {rate:.1f} KB/s", end="")

    print()
    wait_for(link.dat, "ok", timeout=30.0)
    print("\n  전환 완료. 재부팅하면 새 펌웨어로 뜬다")


def open_serial(port, baud):
    io = SerialIO(port, baud)

    io.write(b"\r\nota info\r\n")
    time.sleep(0.5)
    lines = []
    while True:
        d = io.read(4096)
        if not d:
            break
        lines += [l.strip() for l in d.decode("utf-8", "replace").splitlines()]

    return Link(io, io), [l for l in lines if l.startswith(("실행", "대상", "ver"))]


def open_net(host):
    """CLI 는 텔넷, 데이터는 전용 포트. 텔넷은 붙자마자 IAC 협상을 보내는데
    우리는 줄 단위로만 읽으므로 그 바이트는 read_line 이 알아서 버린다."""
    cli = SockIO(host, TELNET_PORT)
    time.sleep(0.3)
    while cli.read(256):
        pass

    dat = SockIO(host, DATA_PORT)
    return Link(cli, dat), []


def main():
    parser = argparse.ArgumentParser(description="실행 중인 보드에 펌웨어 밀어넣기")
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="USB 시리얼 포트")
    src.add_argument("--host", help="WiFi. 이름이나 IP (예: nu87-tinydk.local)")
    parser.add_argument("--image", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--reset", action="store_true", help="끝나면 재부팅까지")
    args = parser.parse_args()

    link = None
    try:
        if args.port:
            link, info = open_serial(args.port, args.baud)
        else:
            link, info = open_net(args.host)

        update(link, args.image, info)

        if args.reset:
            link.write_cli(b"\r\nreset reset\r\n")
            time.sleep(0.5)
            print("  재부팅 요청")

    except KeyboardInterrupt:
        print("\n중지했다. 지금 펌웨어는 그대로다")
        return 1
    except (OtaError, OSError) as e:
        print(f"\n실패: {e}")
        return 1
    finally:
        if link:
            link.cli.close()
            if link.dat is not link.cli:
                link.dat.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
