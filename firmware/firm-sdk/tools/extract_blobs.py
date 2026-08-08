#!/usr/bin/env python3
"""
공장 출하 플래시 덤프에서 부팅에 필요한 바이너리 블롭을 추출한다.

RTL8720DF 이미지 체인은 32바이트 헤더(시그니처 8 + 길이 4 + 로드주소 4 + 예약 16)로
연결되어 있다. 우리는 KM4 애플리케이션만 새로 빌드하고, KM0 쪽과 부트로더는
공장 이미지를 그대로 재사용한다.

  km0_boot_all.bin    -> flash 0x08000000   KM0 부트로더
  km4_boot_all.bin    -> flash 0x08004000   KM4 부트로더 (헤더만, 본체는 ROM)
  km0_image2_all.bin  -> km0_km4_image2.bin 의 앞부분. 우리 KM4 이미지와 concat 한다.

사용:
  python3 tools/extract_blobs.py backup/nu87_factory_flash_4MB.bin -o prebuilt/
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

FLASH_BASE = 0x08000000

# 부트로더 시그니처: u32 쌍 0x96969999 / 0x3FCC66FC → LE 바이트열 99 99 96 96 3F CC 66 FC
SIG_IMG1 = bytes.fromhex("999996963fcc66fc")
SIG_IMG2 = b"81958711"                          # 애플리케이션

KM0_BOOT_OFF = 0x00000
KM4_BOOT_OFF = 0x04000
IMG2_OFF     = 0x06000

HDR_LEN = 32


def read_header(buf, off):
    """(sig, length, load_addr) 를 돌려준다. 알 수 없는 시그니처면 sig=None."""
    if off + HDR_LEN > len(buf):
        return None, 0, 0
    sig = buf[off:off + 8]
    length, load = struct.unpack_from("<II", buf, off + 8)
    if sig not in (SIG_IMG1, SIG_IMG2):
        return None, length, load
    return sig, length, load


def walk_chain(buf, off, limit=12, pad_align=0, want_sig=None, end=None):
    """
    헤더 체인을 걸어가며 [(off, sig, len, load)] 를 돌려준다.

    pad_align  다음 헤더가 바로 나오지 않을 때 건너뛸 정렬 경계. 0 이면 건너뛰지 않는다.
               km0_image2_all.bin 과 km4_image2_all.bin 은 각각 만들어진 뒤 concat 되고
               앞쪽 파일이 4KB 경계로 패딩되므로(pad.sh) IMG2 체인에는 0x1000 을 준다.
               부트로더 체인은 리전(8KB) 안에서 연속이므로 0 을 준다.
    want_sig   이 시그니처가 아니면 멈춘다. 부트로더 리전이 IMG2 체인으로 새는 것을 막는다.
    end        이 오프셋 이상으로는 진행하지 않는다 (리전 경계).
    """
    parts = []
    for _ in range(limit):
        if end is not None and off >= end:
            break
        sig, length, load = read_header(buf, off)
        if sig is None and pad_align:
            # 패딩을 건너뛰고 다음 정렬 경계에서 재시도
            nxt = (off + pad_align - 1) & ~(pad_align - 1)
            if nxt == off:
                nxt = off + pad_align
            if end is None or nxt < end:
                sig, length, load = read_header(buf, nxt)
                if sig is not None:
                    off = nxt
        if sig is None:
            break
        if want_sig is not None and sig != want_sig:
            break
        parts.append((off, sig, length, load))
        off = (off + HDR_LEN + length + 3) & ~3
    return parts


def chain_extent(buf, parts):
    """체인의 마지막 파트 끝 오프셋 (헤더 포함)."""
    if not parts:
        return None
    p_off, _sig, length, _load = parts[-1]
    return p_off + HDR_LEN + length


def describe(load):
    """로드 주소로 파트의 정체를 판별한다. 리전 이름은 rlx8721d_layout_is.ld 기준."""
    if load == 0x08000020:
        return "KM0 · xip_boot  (제자리 XIP)"
    if load == 0x08004020:
        return "KM4 · xip_boot  (제자리 XIP)"
    if load == 0x0C000020:
        return "KM0 · xip       (KM0 XIP 가상)"
    if load == 0x0E000020:
        return "KM4 · xip       (KM4 XIP 가상)"
    if 0x00080000 <= load < 0x00090000:
        return "KM0 · ram       (KM0 SRAM)"
    if 0x000C0000 <= load < 0x000C0400:
        return "KM0 · retention (Retention SRAM)"
    if 0x1007D000 <= load < 0x1007F000:
        return "KM4 · ram       (BOOTLOADER_RAM_S)"
    if 0x10005000 <= load < 0x1007C000:
        return "KM4 · ram       (BD_RAM_NS)"
    if 0x10000000 <= load < 0x10080000:
        return "KM4 · ram       (KM4 SRAM 기타)"
    if 0x02000000 <= load < 0x06000000:
        return "KM4 · psram"
    return "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", help="플래시 덤프 파일 (0x08000000 부터)")
    ap.add_argument("-o", "--outdir", default="prebuilt", help="출력 디렉토리")
    ap.add_argument("-n", "--dry-run", action="store_true", help="분석만 하고 쓰지 않음")
    args = ap.parse_args()

    buf = Path(args.dump).read_bytes()
    print(f"덤프: {args.dump}  ({len(buf)} B = {len(buf)/1024/1024:.2f} MB)")
    print()

    # ── 이미지 체인 분석 ────────────────────────────────────────────────
    # 부트로더는 8KB 리전 안에서 연속이고 IMG1 시그니처만 나온다 (패딩 스킵 금지).
    # IMG2 체인은 km0/km4 파일이 concat 되며 4KB 패딩이 끼어든다.
    print("=== 이미지 체인 ===")
    chains = {
        "KM0 IMG1": walk_chain(buf, KM0_BOOT_OFF, want_sig=SIG_IMG1, end=0x02000),
        "KM4 IMG1": walk_chain(buf, KM4_BOOT_OFF, want_sig=SIG_IMG1, end=0x06000),
        "IMG2":     walk_chain(buf, IMG2_OFF, pad_align=0x1000, want_sig=SIG_IMG2),
    }
    for name, parts in chains.items():
        if not parts:
            print(f"  {name:9s} → 유효한 헤더 없음")
            continue
        for p_off, sig, length, load in parts:
            tag = "IMG1" if sig == SIG_IMG1 else "IMG2"
            print(f"  {name:9s} @ 0x{FLASH_BASE+p_off:08X}  {tag}  "
                  f"len=0x{length:06X} ({length:7d} B)  load=0x{load:08X}  {describe(load)}")
    print()

    # ── 블롭 경계 계산 ──────────────────────────────────────────────────
    for key in ("KM0 IMG1", "KM4 IMG1", "IMG2"):
        if not chains[key]:
            sys.exit(f"{key} 체인을 찾지 못했다. 덤프가 올바른가?")

    km0_boot_end = chain_extent(buf, chains["KM0 IMG1"])
    km4_boot_end = chain_extent(buf, chains["KM4 IMG1"])
    km0_boot = buf[KM0_BOOT_OFF:km0_boot_end]
    km4_boot = buf[KM4_BOOT_OFF:km4_boot_end]

    # IMG2 체인에서 KM0 파트와 KM4 파트의 경계를 찾는다.
    # KM0 파트 = load 주소가 0x0C000020(KM0 XIP) 또는 0x0008xxxx(KM0 SRAM) 인 것
    km4_start = None
    for p_off, _sig, _length, load in chains["IMG2"]:
        is_km0 = (load == 0x0C000020) or (0x00080000 <= load < 0x00090000)
        if not is_km0:
            km4_start = p_off
            break
    if km4_start is None:
        sys.exit("IMG2 체인에서 KM4 파트를 찾지 못했다.")

    km0_image2 = buf[IMG2_OFF:km4_start]

    print("=== 블롭 경계 ===")
    print(f"  km0_boot_all.bin   = flash[0x{FLASH_BASE+KM0_BOOT_OFF:08X} .. 0x{FLASH_BASE+km0_boot_end:08X})"
          f"  {len(chains['KM0 IMG1'])} 파트")
    print(f"  km4_boot_all.bin   = flash[0x{FLASH_BASE+KM4_BOOT_OFF:08X} .. 0x{FLASH_BASE+km4_boot_end:08X})"
          f"  {len(chains['KM4 IMG1'])} 파트")
    print(f"  km0_image2_all.bin = flash[0x{FLASH_BASE+IMG2_OFF:08X} .. 0x{FLASH_BASE+km4_start:08X})"
          f"  = 0x{len(km0_image2):X} ({len(km0_image2)} B)")
    pad = len(km0_image2) % 0x1000
    print(f"    4KB 정렬: {'OK (패딩 포함)' if pad == 0 else f'경계 어긋남 {pad} B (확인 필요)'}")
    print()

    outs = {
        "km0_boot_all.bin": km0_boot,
        "km4_boot_all.bin": km4_boot,
        "km0_image2_all.bin": km0_image2,
    }

    print("=== 출력 ===")
    outdir = Path(args.outdir)
    if not args.dry_run:
        outdir.mkdir(parents=True, exist_ok=True)
    for name, data in outs.items():
        sha = hashlib.sha256(data).hexdigest()[:16]
        print(f"  {name:22s} {len(data):8d} B  sha256:{sha}")
        if not args.dry_run:
            (outdir / name).write_bytes(data)

    if args.dry_run:
        print("\n(dry-run — 파일을 쓰지 않았다)")
    else:
        print(f"\n→ {outdir}/ 에 기록했다")


if __name__ == "__main__":
    main()
