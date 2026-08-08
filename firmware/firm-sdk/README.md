# firm-sdk — 부트로더 / 펌웨어 공유 SDK 영역

부트로더 프로젝트와 펌웨어 프로젝트가 **공유**하는 벤더 SDK 자산을 모아 둔 곳이다.

```
firmware/
├── docs/                       설계 문서
├── firm-sdk/                   ← 여기
│   ├── ameba-rtos-d/           git submodule (241MB) — 빌드에 불필요
│   ├── lib/Realtek/                벤더링 결과 (2.9MB, git 커밋) — 빌드가 참조하는 유일한 대상
│   ├── prebuilt/               KM0 부팅 블롭
│   └── tools/                  동기화·플래싱·디버깅 도구, manifest, 패치, 툴체인 파일
├── backup/                     보드 공장 플래시 백업
├── nu87-fw/                    펌웨어 프로젝트
└── (향후) nu87-boot/           부트로더 프로젝트
```

## 왜 공유하는가

1. **서브모듈 중복 방지.** SDK 는 241MB다. 두 프로젝트가 각자 가질 이유가 없다.
2. **SDK 버전 정합성이 자동 보장된다.** 이게 진짜 이유다.
   KM4 는 KM0 와 **IPC** 로 PMU/슬립/WiFi 핸드셰이크를 하는데,
   **IPC 테이블 레이아웃과 공유메모리 구조는 SDK 버전 간 호환이 보장되지 않는다.**
   `prebuilt/km0_image2_all.bin` 과 `Realtek/` 의 KM4 소스가 다른 커밋에서 오면
   조용히 오동작한다 (부팅은 되는데 WiFi 만 안 켜지는 식).
   서브모듈과 벤더링 결과가 하나면 이 문제가 구조적으로 사라진다.
3. **manifest 와 패치를 한 벌만 유지한다.**
   프로젝트별로 빌드에 넣을 파일이 다른 것은 각 프로젝트의 `CMakeLists.txt`
   `EXCLUDE_PATHS` 가 처리한다 (기존 프로젝트에 이미 있는 idiom).
   부트로더는 `bootloader/*.c` 를, 펌웨어는 `fwlib/ram_hp/*.c` 를 각각 고른다.

## 구성

| 경로 | 내용 |
|---|---|
| `ameba-rtos-d/` | `Ameba-AIoT/ameba-rtos-d` 서브모듈. 커밋 고정 (`7569200f`, 2026-06-18) |
| `lib/Realtek/` | manifest 기준으로 복사된 SDK 소스 **229 파일 / 2.9MB**. `.sdk_version` 에 출처 커밋 기록 |
| `prebuilt/` | `km0_boot_all.bin` (4500B) · `km4_boot_all.bin` (4456B) · `km0_image2_all.bin` (110592B) |
| `tools/sdk_manifest.txt` | 복사 대상의 단일 진실 원천 |
| `tools/patches/` | 벤더 헤더 로컬 수정. 동기화 후 자동 적용 |
| `tools/sync_sdk.py` | manifest 처리 + 패치 적용 + 버전 기록 |
| `tools/extract_blobs.py` | 공장 플래시 덤프에서 부팅 블롭 추출 |
| `tools/arm-none-eabi-gcc.cmake` | CMake 툴체인 파일 (아키텍처 플래그 격리) |
| `tools/openocd/nu87.cfg` | SWD 디버깅 설정 |

## 사용

```bash
# 일반 빌드 — 서브모듈이 필요 없다
cd firmware/nu87-fw && cmake -S . -B build && cmake --build build -j

# SDK 를 새 버전으로 올리거나 파일을 추가할 때만
python3 firmware/firm-sdk/tools/sync_sdk.py
git diff --stat firmware/firm-sdk/lib/Realtek/     # 반드시 검토

# 무엇이 복사될지 미리 보기
python3 firmware/firm-sdk/tools/sync_sdk.py --dry-run
```

## 주의

- **`lib/Realtek/` 를 직접 손으로 고치지 말 것.** `sync_sdk.py` 가 덮어쓴다.
  벤더 파일을 고쳐야 하면 `tools/patches/` 에 패치를 추가한다.
- **패치는 손으로 쓰지 말 것.** 벤더 코드에 후행 탭 같은 공백이 있어서
  손으로 쓴 컨텍스트는 `git apply` 가 거부한다. 실제 파일에서 `difflib.unified_diff` 로 생성한다.
- **`ram_lp/` (KM0 전용) 는 복사하지 않는다.** KM4 이미지에 들어가면 안 된다.
- `Realtek/inc_hp/` 의 `platform_autoconf.h` / `platform_opts.h` 는 **참조용**이다.
  include 경로에 넣지 않는다. 실제로 쓰는 것은 각 프로젝트의 `src/bsp/device/` 에 있는 우리 버전이다.

자세한 내용: [docs/04-sdk-vendoring.md](../docs/04-sdk-vendoring.md)
