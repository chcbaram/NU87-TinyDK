#!/usr/bin/env python3
"""
Ameba-D SDK → firm-sdk/lib/Realtek/ 동기화 (Windows / macOS / Linux 공통)

서브모듈(firm-sdk/ameba-rtos-d)은 "출처와 버전의 증명"일 뿐 빌드 의존성이 아니다.
이 스크립트를 돌릴 때만 서브모듈이 필요하고, 평소 빌드에는 필요 없다.

firm-sdk/ 는 부트로더 프로젝트와 펌웨어 프로젝트가 공유하는 영역이다.
벤더링 결과(Realtek/)를 공유하므로 두 프로젝트의 SDK 버전이 자동으로 일치한다.

  python3 firm-sdk/tools/sync_sdk.py              # 동기화
  python3 firm-sdk/tools/sync_sdk.py --dry-run    # 무엇이 복사될지만 보기
  python3 firm-sdk/tools/sync_sdk.py --no-init    # 서브모듈 init 건너뛰기

동기화 후에는 반드시 diff 를 검토한다:
  git diff --stat firmware/firm-sdk/lib/Realtek/
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

SDK_ROOT = Path(__file__).resolve().parent.parent      # firmware/firm-sdk
SDK_DIR = SDK_ROOT / "ameba-rtos-d"                    # 서브모듈
DEST_DIR = SDK_ROOT / "lib" / "Realtek"                # 벤더링 결과 (git 커밋)
MANIFEST = SDK_ROOT / "tools" / "sdk_manifest.txt"
PATCH_DIR = SDK_ROOT / "tools" / "patches"

# git apply --directory= 는 저장소 루트 기준 경로를 요구한다
REPO_ROOT = Path(
    subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=str(SDK_ROOT),
                   capture_output=True, text=True, check=True).stdout.strip())
DEST_REL = DEST_DIR.relative_to(REPO_ROOT).as_posix()


def git(*args, cwd=REPO_ROOT, check=True):
    return subprocess.run(["git", *args], cwd=str(cwd), check=check,
                          capture_output=True, text=True)


def parse_manifest(path):
    """[(src, dst)] 를 돌려준다. '#' 주석과 빈 줄은 무시."""
    entries = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or "->" not in line:
            continue
        src, dst = (s.strip() for s in line.split("->", 1))
        entries.append((src, dst))
    return entries


def copy_tree(src: Path, dst: Path):
    """src 의 내용을 dst 로 복사한다. dst 에만 있는 파일은 지운다 (rsync --delete 상당)."""
    dst.mkdir(parents=True, exist_ok=True)

    src_rel = {p.relative_to(src) for p in src.rglob("*") if p.is_file()}
    dst_rel = {p.relative_to(dst) for p in dst.rglob("*") if p.is_file()}

    for rel in sorted(src_rel):
        target = dst / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src / rel, target)

    # SDK 에서 사라진 파일 제거
    for rel in sorted(dst_rel - src_rel):
        (dst / rel).unlink()

    # 빈 디렉토리 정리
    for d in sorted((p for p in dst.rglob("*") if p.is_dir()),
                    key=lambda p: len(p.parts), reverse=True):
        if not any(d.iterdir()):
            d.rmdir()

    return len(src_rel)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true", help="복사 목록만 출력")
    ap.add_argument("--no-init", action="store_true", help="서브모듈 init 건너뛰기")
    args = ap.parse_args()

    if not MANIFEST.exists():
        sys.exit(f"manifest 를 찾을 수 없다: {MANIFEST}")

    # ── 서브모듈 확보 ─────────────────────────────────────────────────
    if not args.no_init and not (SDK_DIR / "README.md").exists():
        print("▶ 서브모듈 체크아웃 (최초 1회, 약 240MB)")
        subprocess.run(["git", "submodule", "update", "--init", "--depth", "1",
                        SDK_DIR.relative_to(REPO_ROOT).as_posix()],
                       cwd=str(REPO_ROOT), check=True)

    if not SDK_DIR.is_dir():
        sys.exit(f"SDK 서브모듈이 없다: {SDK_DIR}\n"
                 f"  python3 firm-sdk/tools/sync_sdk.py  로 자동 체크아웃된다")

    commit = git("rev-parse", "HEAD", cwd=SDK_DIR).stdout.strip()
    date = git("log", "-1", "--format=%ad", "--date=short", cwd=SDK_DIR).stdout.strip()

    print(f"▶ SDK  : ameba-rtos-d @ {commit[:12]}  ({date})")
    print(f"▶ 대상 : {DEST_DIR}")
    if args.dry_run:
        print("▶ dry-run — 아무것도 쓰지 않는다")
    print()

    # ── 복사 ─────────────────────────────────────────────────────────
    n_items = n_files = 0
    missing = []
    for src, dst in parse_manifest(MANIFEST):
        abs_src = SDK_DIR / src
        abs_dst = DEST_DIR / dst

        if not abs_src.exists():
            missing.append(src)
            print(f"  [!] 원본 없음: {src}")
            continue

        if abs_src.is_dir():
            if args.dry_run:
                cnt = sum(1 for p in abs_src.rglob("*") if p.is_file())
            else:
                cnt = copy_tree(abs_src, abs_dst)
        else:
            cnt = 1
            if not args.dry_run:
                abs_dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(abs_src, abs_dst)

        n_items += 1
        n_files += cnt
        print(f"  {src:<62} -> {dst:<38} ({cnt})")

    print()
    print(f"▶ 항목 {n_items} 개 / 파일 {n_files} 개")
    if missing:
        print(f"▶ 경고: 원본 없음 {len(missing)} 건 — manifest 를 SDK 버전에 맞게 갱신할 것")

    if args.dry_run:
        return 0

    # ── 로컬 패치 적용 ────────────────────────────────────────────────
    patches = sorted(PATCH_DIR.glob("*.patch")) if PATCH_DIR.is_dir() else []
    if patches:
        print()
        print("▶ 로컬 패치 적용")
        for p in patches:
            rel = p.relative_to(REPO_ROOT).as_posix()
            r = git("apply", f"--directory={DEST_REL}", rel, check=False)
            if r.returncode != 0:
                print(f"  [X] {p.name}")
                print(r.stderr.strip())
                sys.exit("패치 적용 실패 — SDK 버전이 올라가면 패치를 갱신해야 한다")
            print(f"  {p.name}")

    # ── 버전 기록 ────────────────────────────────────────────────────
    (DEST_DIR / ".sdk_version").write_text(
        "# 자동 생성 — firm-sdk/tools/sync_sdk.py\n"
        "# 이 디렉토리의 내용이 어느 SDK 커밋에서 왔는지 기록한다.\n"
        f"repo=https://github.com/Ameba-AIoT/ameba-rtos-d\n"
        f"commit={commit}\n"
        f"date={date}\n"
        f"patches={len(patches)}\n",
        encoding="utf-8")

    total = sum(p.stat().st_size for p in DEST_DIR.rglob("*") if p.is_file())
    print()
    print(f"▶ 버전 기록 -> firm-sdk/lib/Realtek/.sdk_version")
    print(f"▶ 합계 {total/1024/1024:.1f} MB")
    print()
    print(f"다음: git diff --stat {DEST_REL}/ 로 변경 내용을 검토할 것")
    return 0


if __name__ == "__main__":
    sys.exit(main())
