#!/usr/bin/env python3
"""
NU87-TinyDK UART 플래셔 (Windows / macOS / Linux 공통)

  python3 flash.py --auto                          자동 포트 + 앱 이미지
  python3 flash.py --port /dev/cu.usbserial-0001
  python3 flash.py --auto --with-boot              부트로더까지 (복구용)
  python3 flash.py --auto --image some.bin --addr 0x08006000

보드의 CP2102N 자동 다운로드 회로(RTS=CHIP_EN, DTR=PA7 스트랩)를 써서
버튼을 누르지 않고 진입한다.

────────────────────────────────────────────────────────────────────────────
프로토콜

이름은 XMODEM 계열 문자를 쓰지만 프레임은 XMODEM 이 아니다.
1032 바이트 프레임의 offset 3 에 목적지 주소가 들어간다:

    0    1B   0x02
    1    1B   seq            1 부터, u8 wrap
    2    1B   ~seq
    3    4B   목적지 주소 (LE)      ← XMODEM 에는 없는 필드
    7  1024B  데이터 (부족분 0xFF)
  1031    1B  체크섬  = 0xFF 에서 시작해 0..1030 바이트를 더한 u8

목적지 주소가 프레임마다 실리므로 별도의 주소 설정 명령이 없다.
RAM(0x00082000)이든 플래시(0x08006000)든 같은 필드에 그대로 넣는다.

단계
  1) 115200 으로 열고 DTR/RTS 로 다운로드 모드 진입
  2) 배너를 흘려보내고 0x15 두 개를 확인
  3) 0x05 <서브커맨드> 로 보레이트 상향 → 호스트도 전환 → 0x07 로 확인
  4) imgtool_flashloader_amebad.bin 을 RAM 0x00082000 에 전송
  5) 0x04 로 로더 실행. ★ 로더는 115200 으로 올라오므로 다시 내려가서 재협상
  6) 0x26 01 01 00 (SPI 초기화) → 0x17 로 섹터 소거
  7) 이미지 전송 → 0x04 → 0x27 로 체크섬 검증
  8) RTS 로 리셋해 정상 부팅

확장 명령의 인자 길이는 imgtool_flashloader_amebad.bin 을 역어셈블해서 확정했다.
명령 디스패치는 0x82724 에 있고 각 핸들러가 정해진 바이트 수를 다 받을 때까지
블록된다. 모자라게 보내면 ACK 없이 타임아웃 후 NAK 루프로 돌아간다.

  0x17 XMERASE     페이로드 6B : 오프셋 4B LE + 섹터수 2B LE
  0x26 TXSTATUS    페이로드 2B + 길이만큼의 데이터
  0x27 XM_CHECKSUM 페이로드 8B : 오프셋 4B LE + 길이 4B LE

0x17 / 0x27 의 오프셋은 ROM FLASH_Erase() 규약대로 0 기준이고,
1KB 프레임의 목적지 주소는 0x08000000 을 포함한 전체 주소다.

프로토콜 참고 구현: Ameba-AIoT/ameba-arduino-d 의 Ameba_misc/Autoflash_patch/
              src/upload_image_tool.cpp (= jojoling/ameba_bw16_autoflash)
그 구현은 u32 두 개를 offset 1 과 4 에 겹쳐 packing 한 뒤 6 바이트(0x17) /
7 바이트(0x27)만 보내서 인자가 한두 바이트씩 모자란다. 따라 하면 안 된다.
"""

import argparse
import struct
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial 이 필요하다:  python3 -m pip install pyserial")


TOOLS_DIR = Path(__file__).resolve().parent
SDK_DIR = TOOLS_DIR.parent                      # firmware/firm-sdk
PREBUILT_DIR = SDK_DIR / "prebuilt"
FLASHLOADER = SDK_DIR / "lib/Realtek/imgtool/imgtool_flashloader_amebad.bin"

# 어느 디렉토리에서 실행하든 기본 이미지를 찾는다. 현재 위치의 build/ 를 먼저 보고,
# 없으면 저장소 표준 위치를 쓴다.
_LOCAL_IMAGE = Path("build/km0_km4_image2.bin")
DEFAULT_IMAGE = (_LOCAL_IMAGE if _LOCAL_IMAGE.exists()
                 else SDK_DIR.parent / "nu87-fw/build/km0_km4_image2.bin")

SYNC = 0x15
ACK = 0x06

FLASHLOADER_ADDR = 0x00082000       # KM0 SRAM
FLASH_BASE = 0x08000000
SECTOR = 4096
CHUNK = 1024

HANDSHAKE_BAUD = 115200

# 보레이트 → 서브커맨드 (0x05 의 인자)
BAUD_TABLE = {
    1500000: 0x18,
    1444400: 0x17,
    1382400: 0x16,
    1000000: 0x15,
    921600:  0x14,
    500000:  0x13,
    460800:  0x12,
    380400:  0x11,
    230400:  0x10,
    153600:  0x0F,
    128000:  0x0D,
    115200:  0x0C,
}

# 기본 플래시 배치
ADDR_KM0_BOOT = 0x08000000
ADDR_KM4_BOOT = 0x08004000
ADDR_APP      = 0x08006000


class FlashError(Exception):
    pass


# ── 포트 ────────────────────────────────────────────────────────────────
def autodetect_port():
    def score(p):
        if p.vid == 0x10C4 and p.pid == 0xEA60:
            return 0                       # 보드의 CP2102N
        if p.vid == 0x10C4:
            return 1
        n = (p.device or "").lower()
        if "bluetooth" in n:
            return 90
        if "usbserial" in n or "ttyusb" in n:
            return 10
        if n.startswith("com"):
            return 15
        return 50
    ports = sorted(list_ports.comports(), key=lambda p: (score(p), p.device))
    if not ports:
        raise FlashError("시리얼 포트를 찾을 수 없다.\n"
                         "  USB 케이블이 충전 전용은 아닌지 확인할 것")
    return ports[0].device


# ── 리셋 / 진입 ─────────────────────────────────────────────────────────
def set_lines(ser, level):
    """원본의 set_DTR_RTS 규약. bit0=1 이면 RTS 해제, bit1=1 이면 DTR 해제."""
    ser.rts = not bool(level & 0x1)
    ser.dtr = not bool(level & 0x2)


def enter_download_mode(ser):
    set_lines(ser, 0x2); time.sleep(0.5)    # EN low  → 리셋
    set_lines(ser, 0x1); time.sleep(0.2)    # 리셋 해제, 스트랩 low
    set_lines(ser, 0x3); time.sleep(0.5)    # 둘 다 해제


def reset_to_boot(ser):
    set_lines(ser, 0x2); time.sleep(0.5)
    set_lines(ser, 0x3)


# ── 저수준 ──────────────────────────────────────────────────────────────
def read_byte(ser, timeout=5.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        b = ser.read(1)
        if b:
            return b[0]
    return None


def flush_banner(ser):
    """부팅 배너를 흘려보낸다. 첫 0x15 를 만나면 멈춘다 (최대 300 바이트)."""
    for _ in range(300):
        b = ser.read(1)
        if not b:
            break
        if b[0] == SYNC:
            break
    ser.reset_input_buffer()


def wait_sync(ser, count, timeout=5.0):
    """0x15 를 count 개 기다린다. 첫 0x15 전의 잡음은 흘려보낸다."""
    seen = 0
    t0 = time.time()
    while time.time() - t0 < timeout:
        b = ser.read(1)
        if not b:
            continue
        if b[0] == SYNC:
            seen += 1
            if seen >= count:
                return True
        elif seen == 0:
            continue          # 아직 첫 sync 전이면 잡음 허용
        else:
            raise FlashError(f"sync 대기 중 예기치 않은 0x{b[0]:02X}")
    raise FlashError(f"0x15 를 {count} 개 기다리다 시간 초과")


def wait_ack(ser, timeout=5.0):
    """0x06 을 기다린다. 0x15 는 흘려보낸다."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        b = ser.read(1)
        if not b:
            continue
        if b[0] == ACK:
            return True
        if b[0] == SYNC:
            continue
        raise FlashError(f"ACK 대기 중 예기치 않은 0x{b[0]:02X}")
    raise FlashError("ACK(0x06) 시간 초과")


def send_cmd(ser, payload, timeout=5.0):
    """
    명령 전송. 0x07 은 예외적으로 sync 를 기다리지 않는다 —
    보레이트를 막 바꾼 직후라 장치가 이미 새 속도로 말하고 있기 때문이다.

    timeout 은 응답 대기용이다. 소거처럼 오래 걸리는 명령은 늘려서 부른다.
    """
    if payload[0] != 0x07:
        wait_sync(ser, 1)
    ser.write(bytes(payload))
    ser.flush()
    wait_ack(ser, timeout)


# ── 보레이트 ────────────────────────────────────────────────────────────
def set_max_speed(ser, baud):
    if baud not in BAUD_TABLE:
        raise FlashError(f"지원하지 않는 보레이트: {baud}\n"
                         f"  가능: {sorted(BAUD_TABLE, reverse=True)}")
    if baud != HANDSHAKE_BAUD:
        wait_sync(ser, 1)
        send_cmd(ser, [0x05, BAUD_TABLE[baud]])
        ser.baudrate = baud                 # ACK 를 받은 뒤에 전환한다
    send_cmd(ser, [0x07])                   # 새 속도로 확인
    wait_sync(ser, 1)


# ── 데이터 전송 ─────────────────────────────────────────────────────────
def frame_checksum(buf):
    """0xFF 에서 시작해 헤더까지 포함한 1031 바이트를 더한 u8."""
    return (0xFF + sum(buf)) & 0xFF


def write_block(ser, seq, addr, data, label=""):
    """1KB 프레임으로 전송한다. 다음 seq 를 돌려준다."""
    n = (len(data) + CHUNK - 1) // CHUNK
    padded = data + b"\xFF" * (n * CHUNK - len(data))

    wait_sync(ser, 1)                       # 블록의 첫 프레임 전에만

    for i in range(n):
        body = padded[i * CHUNK:(i + 1) * CHUNK]
        hdr = bytes([0x02, seq & 0xFF, (~seq) & 0xFF]) + struct.pack("<I", addr + i * CHUNK)
        frame = hdr + body
        frame += bytes([frame_checksum(frame)])
        assert len(frame) == 1032

        ser.write(frame)
        ser.flush()
        try:
            wait_ack(ser)
        except FlashError as e:
            raise FlashError(f"{label} 프레임 {i + 1}/{n} (seq {seq & 0xFF}) 실패: {e}")

        seq = (seq + 1) & 0xFF
        if label and (i + 1) % 16 == 0 or i + 1 == n:
            pct = (i + 1) * 100 // n
            print(f"\r  {label:<22} {pct:3d}%  ({i + 1}/{n})", end="", flush=True)

    if label:
        print()
    return seq


def erase_block(ser, addr, size):
    """
    0x17 소거 — 명령 뒤에 페이로드 6 바이트가 온다.

      17 | 오프셋 4B LE | 섹터수 2B LE            (총 7 바이트)

    로더는 오프셋을 ROM FLASH_Erase() 에 그대로 넘긴다. 이 함수는 0 기준
    오프셋을 받으므로 0x08000000 을 떼고 보낸다.
    오프셋 0 과 섹터수 0xFFFF 조합은 전체 칩 소거이므로 만들지 않는다.

    섹터수가 16 이상이고 오프셋이 64KB 정렬이면 로더가 64KB 블록 소거로
    처리하고, 그렇지 않으면 4KB 섹터 단위로 돈다.
    """
    n = (size + SECTOR - 1) // SECTOR
    off = addr & ~FLASH_BASE & 0x00FFFFFF
    if off == 0 and n == 0xFFFF:
        raise FlashError("전체 칩 소거 조합이다. 크기를 조정할 것")
    cmd = bytes([0x17]) + struct.pack("<IH", off, n)
    # 섹터 소거는 섹터당 수십~수백 ms 걸린다. 넉넉히 기다린다.
    send_cmd(ser, cmd, timeout=max(120.0, n * 2.0))


def file_checksum(data):
    """u32 LE 워드를 wrapping 합산. 끝의 부분 워드는 마스킹한다."""
    total = 0
    n = len(data) // 4
    rem = len(data) % 4
    if rem:
        tail = data[n * 4:] + b"\x00" * (4 - rem)
        mask = {1: 0xFF, 2: 0xFFFF, 3: 0xFFFFFF}[rem]
        total = struct.unpack("<I", tail)[0] & mask
    for i in range(n):
        total = (total + struct.unpack_from("<I", data, i * 4)[0]) & 0xFFFFFFFF
    return total


def verify_block(ser, addr, size):
    """
    0x27 검증 — 명령 뒤에 페이로드 8 바이트가 온다.

      27 | 오프셋 4B LE | 길이 4B LE               (총 9 바이트)

    응답은 0x27 에 이어 u32 LE 체크섬이다.
    """
    off = addr & ~FLASH_BASE & 0x00FFFFFF
    cmd = bytes([0x27]) + struct.pack("<II", off, size)
    wait_sync(ser, 1)
    ser.write(cmd)
    ser.flush()

    while True:
        b = read_byte(ser)
        if b is None:
            raise FlashError("검증 응답 시간 초과")
        if b == SYNC:
            continue
        if b != 0x27:
            raise FlashError(f"검증 응답이 0x27 이 아니다: 0x{b:02X}")
        break

    raw = b""
    while len(raw) < 4:
        b = read_byte(ser)
        if b is None:
            raise FlashError("체크섬 수신 시간 초과")
        raw += bytes([b])
    return struct.unpack("<I", raw)[0]


# ── 본체 ────────────────────────────────────────────────────────────────
def program(port, images, baud, verify, verbose):
    """images: [(addr, path, data)]"""
    if not FLASHLOADER.exists():
        raise FlashError(f"플래시로더가 없다: {FLASHLOADER}")
    loader = FLASHLOADER.read_bytes()

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = HANDSHAKE_BAUD
    ser.timeout = 0.05
    ser.dtr = False
    ser.rts = False
    ser.open()

    try:
        print(f"▶ 포트   {port}")
        print(f"▶ 보레이트 {baud}")
        print()

        print("  다운로드 모드 진입")
        enter_download_mode(ser)
        flush_banner(ser)
        wait_sync(ser, 2)
        print("  진입 확인 (0x15)")

        # 0x05/0x07 핸드셰이크. 0x07 은 보레이트를 바꾸지 않아도 반드시 보낸다.
        # 이것 없이 데이터 프레임을 보내면 0x06 대신 잡값이 온다.
        print(f"  보레이트 {baud} 로 전환")
        set_max_speed(ser, baud)

        write_block(ser, 1, FLASHLOADER_ADDR, loader, "플래시로더 -> RAM")
        send_cmd(ser, [0x04])                    # 로더 실행

        # 로더도 115200 으로 올라온다.
        print("  로더 실행 · 재동기화")
        ser.baudrate = HANDSHAKE_BAUD
        wait_sync(ser, 2)

        send_cmd(ser, [0x26, 0x01, 0x01, 0x00])  # SPI 플래시 초기화

        for addr, path, data in images:
            print(f"  소거 0x{addr:08X}  {(len(data) + SECTOR - 1) // SECTOR} 섹터")
            erase_block(ser, addr, len(data))

        wait_sync(ser, 1)
        print(f"  보레이트 {baud} 재협상")
        set_max_speed(ser, baud)

        seq = 1                                   # 플래시 단계에서 다시 1 부터
        for addr, path, data in images:
            seq = write_block(ser, seq, addr, data, f"{path.name} -> 0x{addr:08X}")

        send_cmd(ser, [0x04])

        if verify:
            print("  115200 재동기화 후 검증")
            ser.baudrate = HANDSHAKE_BAUD
            flush_banner(ser)
            wait_sync(ser, 1)
            ok = True
            for addr, path, data in images:
                want = file_checksum(data)
                got = verify_block(ser, addr, len(data))
                mark = "OK" if got == want else "불일치"
                if got != want:
                    ok = False
                print(f"  검증 0x{addr:08X}  기대 0x{want:08X}  실제 0x{got:08X}  {mark}")
            if not ok:
                raise FlashError("체크섬이 맞지 않는다")

        print()
        print("  리셋 → 정상 부팅")
        reset_to_boot(ser)
        print("완료")
        return 0

    finally:
        ser.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="시리얼 포트")
    g.add_argument("--auto", action="store_true", help="포트 자동 선택")
    ap.add_argument("--image", default=str(DEFAULT_IMAGE), help="구울 이미지")
    ap.add_argument("--addr", default=hex(ADDR_APP), help="이미지 플래시 주소")
    ap.add_argument("--with-boot", action="store_true",
                    help="부트로더(km0_boot / km4_boot)까지 함께 굽는다 (복구용)")
    ap.add_argument("--baud", type=int, default=1500000, help="전송 보레이트")
    ap.add_argument("--no-verify", action="store_true", help="체크섬 검증 생략")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    try:
        port = autodetect_port() if args.auto else args.port

        images = []
        if args.with_boot:
            for name, addr in (("km0_boot_all.bin", ADDR_KM0_BOOT),
                               ("km4_boot_all.bin", ADDR_KM4_BOOT)):
                p = PREBUILT_DIR / name
                if not p.exists():
                    raise FlashError(f"부트로더 블롭이 없다: {p}")
                images.append((addr, p, p.read_bytes()))

        img = Path(args.image)
        if not img.exists():
            raise FlashError(f"이미지가 없다: {img}\n  먼저 빌드할 것")
        images.append((int(args.addr, 0), img, img.read_bytes()))

        for addr, p, d in images:
            print(f"  {p.name:<24} {len(d):8d} B  -> 0x{addr:08X}")
        print()

        return program(port, images, args.baud, not args.no_verify, args.verbose)

    except FlashError as e:
        print(f"\n실패: {e}", file=sys.stderr)
        return 1
    except serial.SerialException as e:
        print(f"\n시리얼 오류: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n중단됨", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
