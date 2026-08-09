# NU87-TinyDK 펌웨어 개발 문서

STM32H5 기반 `nu87-fw` 프로젝트를 **NU87 모듈(Realtek RTL8720DF / Ameba-D)** 펌웨어로
포팅하는 과정을 기능 단위로 나누어 단계별로 기록한다.

기존 프로젝트의 4계층 구조(`ap` / `hw` / `common` / `bsp` + `.module` 링커 섹션 레지스트리 +
`hw_def.h` 단일 설정 seam)를 **그대로 유지**하는 것이 핵심 제약이다.

## 문서 목록

| # | 문서 | 내용 | 상태 |
|---|---|---|---|
| 00 | [개요와 목표](00-overview.md) | 포팅 목표, 계층 구조 보존 원칙, 단계 계획 | ✅ |
| 01 | [하드웨어 분석](01-hardware.md) | NU87-TinyDK 회로도 실측 — 핀맵, LED, 버튼, USB, SWD | ✅ |
| 02 | [칩 아키텍처](02-chip-architecture.md) | RTL8720DF 듀얼코어(KM0/KM4), 메모리 맵, XIP 구조 | ✅ |
| 03 | [SWD 디버깅 환경](03-debug-swd.md) | OpenOCD + ST-LINK 실동작 검증, pyOCD 검토 결과 | ✅ |
| 04 | [SDK 벤더링](04-sdk-vendoring.md) | 서브모듈 버전 고정 + manifest 기반 선별 복사, 블롭 추출 | ✅ |
| 05 | [빌드 시스템](05-build-system.md) | CMake 독립 빌드, 툴체인 분리, **OS별 개발환경 구축** | ✅ |
| 06 | [부팅 흐름과 이미지 포맷](06-boot-image.md) | 부트 체인, 32바이트 헤더, 링커스크립트 | ✅ |
| 07 | [개발환경 구축](07-dev-environment.md) | **Windows / macOS / Linux 단계별 설치**, 툴체인 버전 정책 | ✅ |
| 08 | [UART 다운로드](08-flash-download.md) | CP2102N 자동 다운로드 회로, 확장 명령 인자 길이 실측, 빈 칩 복구 | ✅ |
| 09 | [BSP 계층](09-bsp.md) | 진입점, 링커스크립트, 시간 기반, **첫 부팅까지 막혔던 3가지** | ✅ |
| 10 | [LED · GPIO · RESET](10-driver-led-gpio-reset.md) | RGB LED, 헤더 GPIO, **NVIC_SystemReset 을 쓰면 안 되는 이유** | ✅ |
| 11 | [UART / LOG / CLI](11-driver-uart-log-cli.md) | LOGUART 는 범용 UART 가 아니다, 폴링 방식과 그 한계 | ✅ |
| 12 | [무선 — WiFi · BLE](12-wireless.md) | 라디오 공존, net 모듈, mDNS, **BLE 는 16비트 UUID 여야 하는 이유** | ✅ |
| 13 | [펌웨어 업데이트 (OTA)](13-ota.md) | A/B 슬롯 전환, 청크 프로토콜, USB / BLE / 웹페이지 | ✅ |
| 14 | [설정 페이지](14-web-page.md) | Web Bluetooth · Web Serial 로 붙는 GitHub Pages 도구 | ✅ |

✅ 완료 · 🚧 진행 중/예정

## 코딩 규약 (기존 프로젝트에서 유지)

- **계층 경계를 지킨다.** `src/common/hw/include/` 의 포터블 API 시그니처는 MCU가 바뀌어도 변하지 않는다.
  벤더 SDK 심볼(`GPIO_InitTypeDef`, `_PA_13`, `DelayMs`)은 `src/hw/driver/` 와 `src/bsp/` 안에서만 보인다. ([00](00-overview.md))
- **`extern` 을 쓰지 않는다.** 파일 간 참조는 헤더를 통한다. 예외는 **링커스크립트가 만든 심볼**뿐이다
  (`_smodule` / `_emodule` / `_fw_flash_begin`) — 헤더로 표현할 수 없기 때문이다.
- **드라이버는 채널 인덱스 패턴**을 따른다: `xxxInit()` + `xxxOn(uint8_t ch)` +
  `if (ch >= XXX_MAX_CH) return;` 가드 + 정적 테이블.
- **설정은 한 곳에.** 기능 스위치는 `src/hw/hw_def.h`, 벤더 SDK 가 요구하는 매크로는 `src/bsp/device/`.
- **크로스플랫폼.** Windows / macOS / Linux 모두에서 빌드된다. 빌드 도구는 셸 스크립트가 아니라 Python 3. ([05](05-build-system.md) §5.7)

## 빠른 참조

```bash
# 빌드 (무선을 쓰려면 옵션을 준다. BLE 는 WiFi 를 요구한다)
cd firmware/nu87-fw && cmake -S . -B build -DNU87_WIFI=ON -DNU87_BLE=ON
cmake --build build -j

# UART 플래싱 (CP2102N 자동 다운로드)
python3 firm-sdk/tools/flash.py --port /dev/cu.usbserial-0001

# 콘솔 — 같은 CLI 가 세 통로로 나온다
python3 -m serial.tools.miniterm --rts 0 --dtr 0 /dev/cu.usbserial-0001 115200
telnet nu87-tinydk.local                      # WiFi 접속 후
#   BLE 는 설정 페이지에서 (docs/index.html)

# 실행 중인 펌웨어에 밀어넣기 (반대편 슬롯. 실패해도 지금 것이 남는다)
python3 firm-sdk/tools/ota.py --port /dev/cu.usbserial-0001 --image build/km0_km4_image2.bin

# 같은 네트워크의 보드 찾기
python3 firm-sdk/tools/discover.py

# SWD 디버깅
openocd -f firm-sdk/tools/openocd/nu87.cfg
arm-none-eabi-gdb build/nu87-fw.elf -ex "target extended-remote :3334"
```

## 검증된 환경

| 항목 | 값 |
|---|---|
| 호스트 | macOS 12.7.6 (arm64) |
| 툴체인 | Arm GNU Toolchain 14.2.Rel1 — multilib `thumb/v8-m.main+fp/hard` |
| CMake | 4.4.2 |
| OpenOCD | 0.12.0 (Homebrew) |
| 디버그 프로브 | ST-LINK/V2-1 (PID `0x3752`), 펌웨어 **V2J46M33** |
| USB-UART | CP2102N → `/dev/cu.usbserial-0001` (macOS 내장 드라이버) |
