#!/usr/bin/env python3
"""
KM4 이미지 생성 — 링크 결과를 부팅 가능한 이미지로 만든다 (Windows/macOS/Linux 공통)

  ELF --objcopy--> ram_2.bin --헤더부착--> km4_image2_all.bin
                                              + prebuilt/km0_image2_all.bin
                                              = km0_km4_image2.bin

벤더 도구를 쓰지 않는 이유:
  SDK 의 prepend_header.sh 는 bash 전용이고, 체크섬 계산기가
  checksum(Linux) / checksum_MacOS / checksum.exe 세 바이너리로 갈려 있어
  크로스플랫폼이 아니다. 32바이트 헤더는 struct.pack 몇 줄이면 된다.

이미지 헤더 (32바이트) — docs/06-boot-image.md 에서 실측 확인:
  0x00  8  시그니처.  IMG2(애플리케이션) = ASCII "81958711"
  0x08  4  이미지 길이 (LE, 헤더 제외)
  0x0C  4  로드 주소 (LE)
  0x10 16  예약 (0xFF)

KM4 이미지는 반드시 2 파트여야 한다.
KM4 부트로더(bootloader/boot_flash_hp.c)가 이렇게 파싱하기 때문이다:

    img2_hdr           = __flash_text_start__ - 32          ← part1(xip) 헤더
    FlashImage2DataHdr = __flash_text_start__ + img2_hdr->image_size
                                                             ← part2(ram) 헤더
    _memcpy(FlashImage2DataHdr->image_addr,
            FlashImage2DataHdr + 1,
            FlashImage2DataHdr->image_size);                 ← 이것만 RAM 으로 복사한다

즉 part1 은 플래시에 남아 XIP 로 실행되고, part2 만 RAM 으로 복사된다.
우리는 전량 SRAM 배치라 XIP 코드가 없지만, 파트를 하나만 넣으면 부트로더가
그것을 part1 로 보고 part2 를 엉뚱한 곳에서 찾아 IMG2 ADDR Invalid 로 실패한다.

그래서 길이 0 인 xip 파트를 앞에 두고 우리 페이로드를 ram 파트로 넣는다:

    [header: sig, size=0, load=0x0E000020]   ← part1 xip (빈 파트)
    [header: sig, size=N, load=0x10005000]   ← part2 ram
    [payload N bytes]

공장 이미지의 KM4 IMG1 도 같은 형태다 (xip_boot len=0 + ram 파트).

로드 주소는 .map 의 __ram_image2_text_start__ 에서 읽는다.
"""

import argparse
import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path

SIG_IMG2 = b"81958711"
HDR_LEN = 32

# km0_image2_all.bin 은 4KB 경계로 패딩된 상태여야 한다.
KM0_PAD_ALIGN = 0x1000

# KM4 애플리케이션 SRAM 시작. ram 파트가 여기로 복사된다.
BD_RAM_NS_BASE = 0x10005000

# KM4 XIP 가상주소. 32바이트 헤더 다음이 코드 시작이다.
# 리전은 0x0E000000 부터 SRAM 시작(0x10000000) 전까지다. SRAM 주소가 숫자상
# 더 크므로 하한 비교만으로는 배치를 구분할 수 없다.
KM4_XIP_BASE = 0x0E000020
KM4_XIP_REGION = range(0x0E000000, 0x10000000)

# 길이 0 인 psram 파트의 로드 주소. 공장 이미지가 쓰는 값 그대로다.
KM4_PSRAM_BASE = 0x02000020
BD_RAM_NS_SIZE = 0x1007C000 - 0x10005000        # 476K

# 추출할 섹션.
#
# RAM 파트 — __ram_image2_text_start__ ~ __ram_image2_text_end__ 사이.
#   부트로더가 이 범위만 SRAM 으로 복사한다.
#   .bss 는 로드하지 않는다 (app_start 가 0 으로 채운다).
#
# XIP 파트 — __flash_text_start__ 부터. 복사되지 않고 플래시에서 실행된다.
#
# 배치는 .map 의 __flash_text_start__ 값으로 판별한다. XIP 배치에서만 이 심볼이
# 0x0E000000 대에 있고, 전량 SRAM 배치에서는 SRAM 주소다.
#
# .ARM.extab / .ARM.exidx 는 배치에 따라 소속이 달라진다. 전량 SRAM 에서는
# RAM 범위 안에, XIP 에서는 플래시 쪽에 놓이므로 목록을 나눠 둔다.
RAM_SECTIONS_SRAM = [
    ".ram_image2.entry",
    ".ram_image2.text",
    ".ARM.extab",
    ".ARM.exidx",
    ".ram_image2.data",
]

RAM_SECTIONS_XIP = [
    ".ram_image2.entry",
    ".ram_image2.text",
    ".ram_image2.data",
]

XIP_SECTIONS = [
    ".xip_image2.text",
    ".ARM.extab",
    ".ARM.exidx",
]


def read_map_symbol(map_path: Path, name: str):
    """.map 에서 심볼 주소를 읽는다. '0x0000000010005000    name' 형태."""
    pat = re.compile(r"^\s*0x([0-9a-fA-F]+)\s+" + re.escape(name) + r"\s*$")
    for line in map_path.read_text(errors="replace").splitlines():
        m = pat.match(line)
        if m:
            return int(m.group(1), 16)
    # 같은 줄에 주소와 심볼이 붙어 나오는 경우도 처리
    pat2 = re.compile(r"0x([0-9a-fA-F]+)\s+" + re.escape(name) + r"\b")
    for line in map_path.read_text(errors="replace").splitlines():
        m = pat2.search(line)
        if m:
            return int(m.group(1), 16)
    return None


# firm_ver_t 는 이미지 페이로드 시작에서 고정 오프셋 +32 에 있다
# (링커스크립트 .ram_image2.entry 에서 '. = __ram_image2_text_start__ + 32').
#   uint32 magic  |  char version_str[32]  |  char name_str[32]  |  uint32 firm_addr
VERSION_OFFSET = 32
VERSION_MAGIC = 0x56455220          # "VER " (common/def.h VERSION_MAGIC_NUMBER)


def read_firm_ver(payload: bytes):
    """페이로드에서 firm_ver_t 를 읽는다. 없거나 매직이 틀리면 None."""
    if len(payload) < VERSION_OFFSET + 72:
        return None
    magic, = struct.unpack_from("<I", payload, VERSION_OFFSET)
    if magic != VERSION_MAGIC:
        return None
    ver = payload[VERSION_OFFSET + 4:VERSION_OFFSET + 36].split(b"\0")[0].decode(errors="replace")
    name = payload[VERSION_OFFSET + 36:VERSION_OFFSET + 68].split(b"\0")[0].decode(errors="replace")
    addr, = struct.unpack_from("<I", payload, VERSION_OFFSET + 68)
    return {"version": ver, "name": name, "addr": addr}


def km0_ls_size(km0: bytes):
    """
    KM0 부트로더가 계산하는 LsSize 를 그대로 재현한다 (boot_flash_lp.c):

        ImgSize = Img2Hdr->image_size + Img2DataHdr->image_size + IMAGE_HEADER_LEN * 2;
        ImgSize = (((ImgSize - 1) >> 12) + 1) << 12;      /* 4KB 정렬 */

    KM4 파트가 놓여야 하는 오프셋이기도 하다. 헤더가 이상하면 None.
    """
    if len(km0) < HDR_LEN * 2:
        return None
    if km0[0:8] != SIG_IMG2:
        return None
    size1, = struct.unpack_from("<I", km0, 8)
    off2 = HDR_LEN + size1
    if off2 + HDR_LEN > len(km0) or km0[off2:off2 + 8] != SIG_IMG2:
        return None
    size2, = struct.unpack_from("<I", km0, off2 + 8)
    raw = size1 + size2 + HDR_LEN * 2
    return (((raw - 1) >> 12) + 1) << 12


def prepend_header(data: bytes, load_addr: int) -> bytes:
    hdr = SIG_IMG2 + struct.pack("<II", len(data), load_addr) + b"\xff" * 16
    assert len(hdr) == HDR_LEN
    return hdr + data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True, help="링크 결과 ELF")
    ap.add_argument("--map", required=True, help="링커 맵 파일")
    ap.add_argument("--objcopy", default="arm-none-eabi-objcopy")
    ap.add_argument("--prebuilt", required=True, help="KM0 블롭이 있는 디렉토리")
    ap.add_argument("--outdir", default=".")
    args = ap.parse_args()

    elf = Path(args.elf)
    mapf = Path(args.map)
    outdir = Path(args.outdir)
    prebuilt = Path(args.prebuilt)

    for p in (elf, mapf):
        if not p.exists():
            sys.exit(f"파일이 없다: {p}")

    # ── 로드 주소 ─────────────────────────────────────────────────────
    load_addr = read_map_symbol(mapf, "__ram_image2_text_start__")
    if load_addr is None:
        sys.exit("__ram_image2_text_start__ 를 .map 에서 찾을 수 없다.\n"
                 "  링커스크립트가 이 심볼을 정의하는지 확인할 것")

    # ── 배치 판별 ─────────────────────────────────────────────────────
    flash_addr = read_map_symbol(mapf, "__flash_text_start__")
    is_xip = flash_addr is not None and flash_addr in KM4_XIP_REGION

    def extract(sections, name):
        out = outdir / name
        cmd = [args.objcopy]
        for s in sections:
            cmd += ["-j", s]
        cmd += ["-O", "binary", str(elf), str(out)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"objcopy 실패:\n{r.stderr}")
        return out.read_bytes()

    # ── 섹션 추출 ─────────────────────────────────────────────────────
    if is_xip:
        payload = extract(RAM_SECTIONS_XIP, "ram_2.bin")
        xip_payload = extract(XIP_SECTIONS, "xip_2.bin")
        xip_addr = flash_addr
        if len(xip_payload) == 0:
            sys.exit("XIP 배치인데 .xip_image2.text 가 비어 있다. 링커스크립트를 확인할 것")
    else:
        payload = extract(RAM_SECTIONS_SRAM, "ram_2.bin")
        xip_payload = b""
        xip_addr = KM4_XIP_BASE

    if len(payload) == 0:
        sys.exit("추출된 이미지가 비어 있다. 링커스크립트의 섹션 이름을 확인할 것")

    # ── 헤더 부착 ─────────────────────────────────────────────────────
    # part1: xip 파트. 플래시에 남아 XIP 로 실행된다.
    #        전량 SRAM 배치에서는 길이 0 이다. 그래도 있어야 부트로더가 part2
    #        헤더 위치를 제대로 계산한다.
    # part2: ram 파트. 부트로더가 이것만 image_addr 로 복사한다.
    # part3: 길이 0 인 psram 파트.
    #        KM0 부트로더(boot_flash_lp.c BOOT_FLASH_OTA_MMU)가 KM4 이미지에 한해
    #        part2 다음에서 psram 헤더를 읽어 MMU 매핑 크기에 더한다:
    #            PsramHdr = (IMAGE_HEADER *)(vAddr + ImgSize);
    #            ImgSize += (IMAGE_HEADER_LEN + PsramHdr->image_size);
    #        이 파트가 없으면 빈 플래시(0xFF)를 헤더로 읽어 image_size 가
    #        0xFFFFFFFF 가 된다. u32 오버플로 덕에 결과적으로 한 페이지 더 크게
    #        잡혀 동작은 하지만 의도한 동작이 아니다. 공장 이미지도 이 파트를 갖는다.
    km4 = (prepend_header(xip_payload, xip_addr)
           + prepend_header(payload, load_addr)
           + prepend_header(b"", KM4_PSRAM_BASE))
    km4_path = outdir / "km4_image2_all.bin"
    km4_path.write_bytes(km4)

    # ── KM0 스톡 블롭과 결합 ──────────────────────────────────────────
    km0_path = prebuilt / "km0_image2_all.bin"
    if not km0_path.exists():
        sys.exit(f"KM0 블롭이 없다: {km0_path}\n"
                 f"  extract_blobs.py 로 공장 플래시 덤프에서 추출할 것")

    km0 = km0_path.read_bytes()

    # KM0 블롭 길이는 부트로더가 계산하는 값과 정확히 같아야 한다.
    #
    # KM0 부트로더는 KM0 이미지의 두 파트 헤더에서 크기를 더해 4KB 로 올린 값
    # (LsSize)을 구하고, KM4 파트가 OTA_Region + LsSize 에 있다고 가정해
    # MMU 를 건다. 파일 길이가 이 값과 다르면 KM4 파트가 엉뚱한 자리에 놓여
    # "AmebaD Flash Memory Layout is modified!" 를 출력하며 무한 대기한다.
    ls_size = km0_ls_size(km0)
    if ls_size is None:
        print("  [!] km0_image2_all.bin 의 파트 헤더를 읽을 수 없다. 시그니처를 확인할 것")
    elif len(km0) != ls_size:
        sys.exit(f"km0_image2_all.bin 길이가 맞지 않다.\n"
                 f"  파일        {len(km0)} B\n"
                 f"  부트로더 계산 {ls_size} B  (파트 크기 합을 4KB 로 올림)\n"
                 f"  이대로 구우면 KM4 파트를 찾지 못해 부팅하지 않는다.\n"
                 f"  extract_blobs.py 의 pad_align 을 확인할 것")

    combined = km0 + km4
    comb_path = outdir / "km0_km4_image2.bin"
    comb_path.write_bytes(combined)

    # ── 보고 ──────────────────────────────────────────────────────────
    used = len(payload)

    fv = read_firm_ver(payload)
    if fv is None:
        print("  [!] 페이로드 +32 에서 firm_ver_t 를 찾지 못했다.\n"
              "      링커스크립트의 .version 슬롯과 hw.c 의 section(\".version\") 을 확인할 것.\n"
              "      부트로더가 실행 없이 버전을 읽지 못한다")
    else:
        print(f"  펌웨어      {fv['name']}  {fv['version']}   "
              f"(페이로드 +{VERSION_OFFSET}, addr 0x{fv['addr']:08X})")

    print(f"  배치        {'XIP (코드는 플래시에서 실행)' if is_xip else 'SRAM (전량 SRAM)'}")
    print(f"  part1 xip   {len(xip_payload):7d} B  load 0x{xip_addr:08X}"
          f"{'  (__flash_text_start__)' if is_xip else '  (빈 파트)'}")
    print(f"  part2 ram   {used:7d} B  load 0x{load_addr:08X}  (__ram_image2_text_start__)")
    print(f"  km4_image2  {len(km4):7d} B  sha256:{hashlib.sha256(km4).hexdigest()[:16]}")
    print(f"  km0(스톡)   {len(km0):7d} B")
    print(f"  최종 이미지 {len(combined):7d} B  -> {comb_path.name}  (flash 0x08006000)")
    print(f"  SRAM 로드분 {used} / {BD_RAM_NS_SIZE} B  ({used * 100.0 / BD_RAM_NS_SIZE:.1f}%)")

    # 로드 주소는 SRAM 의 목적지다. 32바이트 헤더는 플래시에만 있으므로
    # 로드 주소에는 더하지 않는다. BD_RAM_NS 시작이 그대로 나와야 한다.
    if load_addr != BD_RAM_NS_BASE:
        print(f"  [!] 로드 주소가 0x{BD_RAM_NS_BASE:08X}(BD_RAM_NS 시작)이 아니다. "
              f"링커스크립트를 확인할 것")

    return 0


if __name__ == "__main__":
    sys.exit(main())
