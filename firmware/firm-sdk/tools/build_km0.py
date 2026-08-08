#!/usr/bin/env python3
"""
KM0 image2 를 SDK 서브모듈에서 빌드해 prebuilt 로 넣는다 (Windows/macOS/Linux 공통)

    python3 firm-sdk/tools/build_km0.py

평소 빌드에는 필요 없다. prebuilt/km0_image2_all.bin 이 커밋되어 있고 그것만 쓴다.
KM0 를 다시 만들어야 할 때만 실행한다. 서브모듈이 체크아웃되어 있어야 한다.

왜 다시 만드는가
  KM4 는 이 저장소가 SDK 소스에서 빌드하는데 KM0 가 공장 블롭이면 두 이미지의
  SDK 버전이 다르다. IPC 는 채널 테이블과 공유메모리 구조가 양쪽에서 일치해야
  하므로 무선 단계부터는 같은 버전으로 맞춰야 한다.

벤더 asdk 툴체인을 쓰지 않는다
  SDK 가 지정한 asdk-10.4.1 arm64 릴리스는 이미 내려갔고, 남아 있는 것들도
  버전이 다르다. 어차피 KM4 를 표준 arm-none-eabi-gcc 로 빌드하고 있으므로
  KM0 도 같은 컴파일러로 만든다.

  벤더 Makefile 은 tool_dir 아래의 arm-none-eabi-* 를 그대로 부르므로,
  완화 플래그를 붙여 실제 컴파일러로 넘기는 shim 을 만들어 그 경로를 넘긴다.
  GCC 14 는 벤더 코드(GCC 6.4.1 기준)의 여러 진단을 error 로 승격했다.
"""

import argparse
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
SDK_DIR = TOOLS_DIR.parent
SUBMODULE = SDK_DIR / "ameba-rtos-d"
PREBUILT = SDK_DIR / "prebuilt"

PROJECT = SUBMODULE / "project/realtek_amebaD_va0_example/GCC-RELEASE"
KM0_DIR = PROJECT / "project_lp"

# arm-none-eabi-gcc.cmake 의 NU87_VENDOR_RELAX_FLAGS 와 같은 이유의 플래그다.
RELAX_FLAGS = [
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=return-mismatch",
    "-Wno-int-conversion",
    "-Wno-implicit-function-declaration",
    "-Wno-incompatible-pointer-types",
    "-Wno-char-subscripts",
    "-Wno-maybe-uninitialized",
    "-Wno-builtin-declaration-mismatch",
]

SHIM_TOOLS = ["gcc", "as", "ar", "ld", "objcopy", "objdump", "size", "nm", "strip",
              "readelf", "gdb"]


def find_toolchain():
    gcc = shutil.which("arm-none-eabi-gcc")
    if gcc is None:
        sys.exit("arm-none-eabi-gcc 를 PATH 에서 찾을 수 없다")
    return Path(gcc).resolve().parent


def make_shim(shim_bin: Path, real_bin: Path):
    """벤더 Makefile 이 부르는 이름 그대로 만들고, gcc 에만 완화 플래그를 붙인다."""
    shim_bin.mkdir(parents=True, exist_ok=True)

    for tool in SHIM_TOOLS:
        name = f"arm-none-eabi-{tool}"
        real = real_bin / name
        if not real.exists():
            continue

        path = shim_bin / name
        if tool == "gcc":
            body = f'#!/bin/sh\nexec "{real}" {" ".join(RELAX_FLAGS)} "$@"\n'
        else:
            body = f'#!/bin/sh\nexec "{real}" "$@"\n'
        path.write_text(body)
        path.chmod(path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)


def skip_toolchain_download():
    """
    벤더 Makefile 은 asdk 압축파일이 없으면 받으려 한다. 그 URL 은 이미 죽었고
    우리는 쓰지도 않으므로, 빈 파일을 놓아 다운로드 단계를 건너뛴다.
    """
    gen = (KM0_DIR / "asdk/Makefile.include.gen").read_text(errors="replace")
    names = [l.split("=", 1)[1].strip()
             for l in gen.splitlines() if l.strip().startswith("TOOLCHAINNAME")]
    stamp_dir = PROJECT / "project_hp/toolchain/asdk"
    stamp_dir.mkdir(parents=True, exist_ok=True)
    for name in names:
        (stamp_dir / name).touch()


def submodule_is_dirty():
    r = subprocess.run(["git", "-C", str(SUBMODULE), "status", "--porcelain"],
                       capture_output=True, text=True)
    return r.returncode == 0 and r.stdout.strip() != ""


def restore_submodule():
    """
    벤더 빌드가 gnu_utility 스크립트의 실행 권한을 바꾸고 산출물을 잔뜩 남긴다.
    서브모듈은 고정 체크아웃이므로 원래대로 되돌린다.
    """
    subprocess.run(["git", "-C", str(SUBMODULE), "checkout", "--", "."],
                   capture_output=True, text=True)
    subprocess.run(["git", "-C", str(SUBMODULE), "clean", "-fdq"],
                   capture_output=True, text=True)


def check_parts(path: Path):
    """
    KM4 파트가 놓일 자리를 KM0 부트로더가 이 이미지의 헤더에서 계산한다.
    파일 길이가 그 계산값과 다르면 KM4 를 찾지 못하고 무한 대기한다.
    make_image.py 가 같은 검사를 하지만 여기서 먼저 걸러 준다.
    """
    import struct

    d = path.read_bytes()
    off = 0
    raw = 0
    while off + 32 <= len(d) and d[off:off + 8] == b"81958711":
        size, addr = struct.unpack_from("<II", d, off + 8)
        print(f"    +0x{off:06X}  size={size:7d}  load=0x{addr:08X}")
        raw += 32 + size
        off += 32 + size

    aligned = (((raw - 1) >> 12) + 1) << 12
    if aligned != len(d):
        sys.exit(f"파트 크기 합({raw} -> 4KB 올림 {aligned})과 파일 길이({len(d)})가 다르다")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep-build", action="store_true", help="빌드 산출물을 지우지 않는다")
    ap.add_argument("--no-clean", action="store_true",
                    help="서브모듈 워킹트리를 되돌리지 않는다 (디버깅용)")
    args = ap.parse_args()

    if not args.no_clean and submodule_is_dirty():
        sys.exit("SDK 서브모듈에 수정된 내용이 있다. 빌드가 끝나면 되돌려지므로 먼저 정리할 것.\n"
                 "  되돌리지 않으려면 --no-clean")

    if not (SUBMODULE / ".git").exists() and not (SUBMODULE / "component").is_dir():
        sys.exit(f"SDK 서브모듈이 없다: {SUBMODULE}\n"
                 f"  git submodule update --init firmware/firm-sdk/ameba-rtos-d")

    real_bin = find_toolchain()
    ver = subprocess.run([str(real_bin / "arm-none-eabi-gcc"), "-dumpversion"],
                         capture_output=True, text=True).stdout.strip()
    print(f"▶ 컴파일러  arm-none-eabi-gcc {ver}  ({real_bin})")

    shim_bin = SDK_DIR / "build-km0/shim/bin"
    make_shim(shim_bin, real_bin)
    print(f"▶ shim      {shim_bin}")

    skip_toolchain_download()

    print("▶ make -C asdk image2")
    r = subprocess.run(
        ["make", "-C", "asdk", "image2", f"ASDK_TOOLCHAIN={shim_bin.parent}"],
        cwd=KM0_DIR, capture_output=True, text=True)

    log = SDK_DIR / "build-km0/build.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(r.stdout + r.stderr)

    # 마지막에 size 리포트와 python 스크립트가 실패하는데 이미지 생성 뒤라 무해하다.
    # 성공 판정은 결과 파일로 한다.
    out = KM0_DIR / "asdk/image/km0_image2_all.bin"

    if r.returncode != 0 and not out.exists():
        tail = [l for l in (r.stdout + r.stderr).splitlines() if "rror" in l][-15:]
        print("\n".join(tail))
        sys.exit(f"\nKM0 빌드 실패. 전체 로그: {log}")

    if not out.exists():
        sys.exit(f"빌드는 끝났는데 결과가 없다: {out}\n  로그: {log}")

    check_parts(out)

    PREBUILT.mkdir(parents=True, exist_ok=True)
    dst = PREBUILT / "km0_image2_all.bin"
    shutil.copy2(out, dst)

    sha = subprocess.run(["shasum", "-a", "256", str(dst)],
                         capture_output=True, text=True).stdout.split()[0]
    print(f"\n▶ {dst.name}  {dst.stat().st_size} B  sha256:{sha[:16]}")
    print(f"  로그 {log}")
    print("\n  make_image.py 가 이 블롭의 헤더로 KM4 파트 위치를 계산하므로")
    print("  크기가 바뀌어도 별도 조정은 필요 없다.")

    if not args.keep_build:
        shutil.rmtree(SDK_DIR / "build-km0", ignore_errors=True)

    if not args.no_clean:
        restore_submodule()
        print("  서브모듈 워킹트리를 되돌렸다")

    return 0


if __name__ == "__main__":
    sys.exit(main())
