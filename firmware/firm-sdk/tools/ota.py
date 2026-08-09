#!/usr/bin/env python3
"""실행 중인 보드에 펌웨어를 밀어 넣는다.

flash.py 와 다르다. flash.py 는 다운로드 모드로 진입시켜 플래시를 통째로
굽고, 이쪽은 돌고 있는 펌웨어의 CLI 로 반대편 OTA 슬롯에 쓴다. 실패해도
지금 슬롯이 그대로 남아 있어 보드가 죽지 않는다.

펌웨어 쪽은 hw/driver/ota.c 의 otaReceive() 다.

    python3 ota.py --port /dev/tty.usbserial-0001 --image km0_km4_image2.bin
"""

import argparse
import sys
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("pyserial 이 필요하다: pip3 install pyserial")


CHUNK = 1024          # 펌웨어의 OTA_CHUNK_SIZE 와 같아야 한다
ACK_TIMEOUT = 10.0    # 섹터 지우기가 낀 청크는 느리다


class OtaError(Exception):
    pass


def read_line(ser, timeout=ACK_TIMEOUT):
    """개행까지 한 줄. 보드가 보내는 말은 전부 한 줄짜리다."""
    end = time.time() + timeout
    line = b""

    while time.time() < end:
        data = ser.read(1)
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


def wait_for(ser, expect, timeout=ACK_TIMEOUT):
    """기대한 말이 나올 때까지 읽는다. 에코와 로그가 섞여 들어온다."""
    end = time.time() + timeout

    while time.time() < end:
        line = read_line(ser, timeout=max(0.1, end - time.time()))
        if line == expect:
            return
        if line.startswith("err"):
            raise OtaError("보드: " + line)

    raise OtaError(f"'{expect}' 를 받지 못했다")


def update(port, image, baud=115200):
    payload = open(image, "rb").read()
    crc = zlib.crc32(payload) & 0xFFFFFFFF

    print(f"\n▶ 이미지  {image}")
    print(f"  크기    {len(payload)} B")
    print(f"  crc32   0x{crc:08X}")

    ser = serial.Serial(port, baud, timeout=0.2)
    ser.reset_input_buffer()

    # 실행 중인 슬롯을 먼저 보여준다
    ser.write(b"\r\nota info\r\n")
    time.sleep(0.5)
    for line in ser.read(4096).decode("utf-8", "replace").splitlines():
        if "실행" in line or "대상" in line:
            print("  " + line.strip())

    ser.reset_input_buffer()
    ser.write(f"ota write {len(payload)} {crc}\r\n".encode())
    wait_for(ser, "ready", timeout=15.0)

    print()
    sent = 0
    started = time.time()

    while sent < len(payload):
        chunk = payload[sent:sent + CHUNK]
        ser.write(chunk)
        wait_for(ser, "a")
        sent += len(chunk)

        pct = sent * 100 // len(payload)
        rate = sent / max(0.001, time.time() - started) / 1024
        print(f"\r  전송 {pct:3d}%  {sent}/{len(payload)} B  {rate:.1f} KB/s", end="")

    print()
    wait_for(ser, "ok", timeout=20.0)
    print("\n  전환 완료. 재부팅하면 새 펌웨어로 뜬다")

    ser.close()


def main():
    parser = argparse.ArgumentParser(description="실행 중인 보드에 펌웨어 밀어넣기")
    parser.add_argument("--port", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--reset", action="store_true", help="끝나면 재부팅까지")
    args = parser.parse_args()

    try:
        update(args.port, args.image, args.baud)
    except (OtaError, OSError) as e:
        print(f"\n실패: {e}")
        return 1

    if args.reset:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
        ser.write(b"\r\nreset reset\r\n")
        time.sleep(0.5)
        ser.close()
        print("  재부팅 요청")

    return 0


if __name__ == "__main__":
    sys.exit(main())
