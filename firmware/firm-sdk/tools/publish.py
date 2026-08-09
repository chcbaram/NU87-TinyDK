#!/usr/bin/env python3
"""빌드한 이미지를 설정 페이지의 배포본 목록에 올린다.

저장소의 docs/fw/ 에 넣고 manifest.json 을 갱신한다. GitHub Pages 가 docs/ 를
서빙하므로 페이지와 같은 출처가 되어 CORS 제약이 없다.

릴리스 자산은 쓸 수 없다. release-assets.githubusercontent.com 이 CORS 헤더를
주지 않아 브라우저가 fetch 를 막는다 (api.github.com 과 raw.githubusercontent.com
은 열려 있지만 자산 본문은 그쪽으로 오지 않는다).

    python3 publish.py --image build/km0_km4_image2.bin
    python3 publish.py --list
    python3 publish.py --remove nu87-fw-V260809R1-wifi.bin

배포본은 WiFi+BLE 구성만 올린다. 설정 페이지가 BLE 로 붙어서 쓰는 것이라,
BLE 가 없는 이미지를 올리면 그것을 받은 보드와는 페이지로 다시 붙을 수 없다.
다른 구성이 필요하면 --any 로 푼다.

오래된 것은 자동으로 정리한다. 구성별로 최근 것만 남긴다 — 나중에 --any 로
다른 구성을 섞더라도 한쪽이 다른 쪽 이력을 밀어내지 않게 하기 위해서다.
"""

import argparse
import json
import os
import shutil
import struct
import sys
from datetime import date, datetime

HERE = os.path.dirname(os.path.abspath(__file__))
FW_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "..", "docs", "fw"))
MANIFEST = os.path.join(FW_DIR, "manifest.json")

IMG_SIG = b"81958711"
RAM_ADDR = 0x10005000

KEEP_DEFAULT = 3      # 구성별로 남길 개수


def read_image(data):
    """이미지에서 이름 · 버전 · 무선 지원을 읽는다. 페이지의 readImage() 와 같다."""
    parts = []
    off = 0

    while off + 32 <= len(data):
        if data[off:off + 8] != IMG_SIG:
            nxt = ((off >> 12) + 1) << 12       # KM0 이미지는 4KB 로 패딩된다
            if nxt <= off or nxt + 32 > len(data):
                break
            off = nxt
            continue
        length, addr = struct.unpack("<II", data[off + 8:off + 16])
        parts.append((off + 32, length, addr))
        off += 32 + length

    ram = [p for p in parts if p[2] == RAM_ADDR]
    if not ram:
        return None

    v = data[ram[0][0] + 32:ram[0][0] + 32 + 68]
    txt = lambda a, b: v[a:b].split(b"\0")[0].decode("ascii", "replace")

    feat = []
    if b"wifiInit()" in data:
        feat.append("WiFi")
    if b"bleInit()" in data:
        feat.append("BLE")

    return {
        "ver": txt(4, 36),
        "name": txt(36, 68),
        "feat": " + ".join(feat) or "무선 없음",
    }


def load():
    if not os.path.exists(MANIFEST):
        return []
    return json.load(open(MANIFEST))


def prune(items, keep):
    """구성별로 최근 keep 개만 남기고 파일도 지운다."""
    kept = []
    dropped = []
    seen = {}

    for it in items:                      # 이미 최신순으로 정렬되어 들어온다
        cnt = seen.get(it["feat"], 0)
        if cnt < keep:
            seen[it["feat"]] = cnt + 1
            kept.append(it)
        else:
            dropped.append(it)

    for it in dropped:
        path = os.path.join(FW_DIR, it["file"])
        if os.path.exists(path):
            os.remove(path)

    return kept, dropped


def save(items):
    os.makedirs(FW_DIR, exist_ok=True)
    with open(MANIFEST, "w") as f:
        json.dump(items, f, indent=2, ensure_ascii=False)
        f.write("\n")


def show(items):
    if not items:
        print("  (없음)")
        return
    for it in items:
        print(f"  {it['ver']:<12} {it['feat']:<12} {it['size']:>8} B  {it['date']}  {it['file']}")


def main():
    parser = argparse.ArgumentParser(description="설정 페이지의 배포본 목록 관리")
    parser.add_argument("--image", help="올릴 이미지 (km0_km4_image2.bin)")
    parser.add_argument("--list", action="store_true", help="목록만 본다")
    parser.add_argument("--remove", metavar="FILE", help="목록과 파일에서 뺀다")
    parser.add_argument("--keep", type=int, default=KEEP_DEFAULT,
                        help=f"구성별로 남길 개수 (기본 {KEEP_DEFAULT})")
    parser.add_argument("--any", action="store_true",
                        help="WiFi+BLE 가 아닌 구성도 올린다")
    args = parser.parse_args()

    items = load()

    if args.list:
        print(f"\n▶ 배포본  {FW_DIR}")
        show(items)
        print()
        return 0

    if args.remove:
        items = [i for i in items if i["file"] != args.remove]
        path = os.path.join(FW_DIR, args.remove)
        if os.path.exists(path):
            os.remove(path)
        save(items)
        print(f"뺐다: {args.remove}")
        show(items)
        return 0

    if not args.image:
        parser.error("--image 나 --list / --remove 중 하나가 필요하다")

    data = open(args.image, "rb").read()
    info = read_image(data)
    if info is None:
        print("이미지 형식이 아니다. km0_km4_image2.bin 인지 확인할 것")
        return 1

    if info["feat"] != "WiFi + BLE" and not args.any:
        print(f"\n배포본은 WiFi+BLE 구성만 올린다. 이 이미지는 '{info['feat']}' 다.")
        print("설정 페이지가 BLE 로 붙어서 쓰므로, BLE 가 없는 이미지를 받은 보드와는")
        print("페이지로 다시 붙을 수 없다. 그래도 올리려면 --any 를 준다.\n")
        return 1

    tag = info["feat"].replace(" + ", "-").replace(" ", "-").lower()
    name = f"nu87-fw-{info['ver']}-{tag}.bin"

    os.makedirs(FW_DIR, exist_ok=True)
    shutil.copy2(args.image, os.path.join(FW_DIR, name))

    items = [i for i in items if i["file"] != name]
    items.append({
        "ver": info["ver"],
        "feat": info["feat"],
        "file": name,
        "size": len(data),
        "date": date.today().isoformat(),
    })
    # 새 것이 위로
    items.sort(key=lambda i: (i["date"], i["ver"]), reverse=True)
    items, dropped = prune(items, args.keep)
    save(items)

    print(f"\n▶ 올렸다  {name}")
    print(f"  {info['name']} · {info['ver']} · {info['feat']} · {len(data)} B")

    if dropped:
        print(f"\n▶ 정리  구성별 {args.keep} 개를 넘겨 지웠다")
        for it in dropped:
            print(f"  {it['file']}")

    print(f"\n▶ 배포본  {FW_DIR}")
    show(items)
    print("\n  커밋하고 푸시하면 설정 페이지에 나타난다\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
