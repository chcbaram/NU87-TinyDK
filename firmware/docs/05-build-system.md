# 05. 빌드 시스템 — CMake 독립 빌드

**결론: 벤더 asdk 툴체인 없이 표준 arm-none-eabi-gcc + CMake 로 빌드된다. 실증 완료.**

Realtek SDK 는 GNU make + 벤더 asdk GCC(6.4.1/10.4.1) + ROM 심볼 링커스크립트에 묶여 있다.
그런데 기존 STM32 프로젝트와 동일한 CMake 환경을 유지하는 것이 이 포팅의 목표이므로,
SDK 소스만 `firm-sdk/lib/Realtek/` 로 선별 복사해 독립 빌드한다. ([04](04-sdk-vendoring.md))

## 5.1 왜 가능한가 — 실증된 근거 3개

**① ROM 심볼 링커스크립트가 평문이다**

주변장치 드라이버 대부분이 칩 마스크 ROM 에 있고 `_LONG_CALL_` 로 선언된다.
이를 해석하는 `ld/rlx8721d_rom_symbol_acut.ld` 는 `symbol = 0xADDR;` 나열일 뿐인 **평문 텍스트**라
어떤 GCC 로도 그대로 쓸 수 있다. 결과적으로 **복사해야 할 소스가 매우 적다.**

증거: SDK `make/target/fwlib/Makefile` 의 KM4 소스 목록에 **`rtl8721d_gpio.c` 가 아예 없다.**
`GPIO_Init` / `GPIO_WriteBit` / `Pinmux_Config` / `DelayMs` 전부 ROM 이다.

**② ABI 가 일치한다**

Realtek 은 `-march=armv8-m.main+dsp -mthumb` + `arm-none-eabi/lib/v8-m.main/fpu/fpv5-sp-d16` 로 링크한다.
표준 툴체인에서 같은 멀티립이 나오는지 확인:

```
$ arm-none-eabi-gcc -print-multi-directory -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
thumb/v8-m.main+fp/hard          ← 일치
```

**③ 실제로 컴파일된다**

`ameba_soc.h`(SDK 최상위 umbrella 헤더) 를 포함한 TU 가 표준 GCC 14.2 로 컴파일된다.
fwlib 소스 44개 중 **30개 컴파일 성공, 실패 0** (나머지 14개는 아래 §5.4 참고).

## 5.2 검증된 빌드 설정

```cmake
# 아키텍처 — firm-sdk/tools/arm-none-eabi-gcc.cmake 에 격리한다 (§5.6)
-mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mcmse

# 벤더 소스 전용 완화 플래그 (§5.3)
-Wno-error=int-conversion
-Wno-error=implicit-function-declaration
-Wno-error=incompatible-pointer-types

# 링크
-T src/bsp/ldscript/nu87_km4_img2.ld
-T firm-sdk/lib/Realtek/ld/rlx8721d_rom_symbol_acut.ld
-Wl,--gc-sections   -ffunction-sections -fdata-sections
```

`-mcmse` 가 **필수**다. 없으면 `rtl8721d_trustzone.h` 가 깨진다:
```
rtl8721d_trustzone.h:88:39: error: 'struct cmse_address_info' has no member named 'secure'
```
TrustZone 을 쓰지 않아도 `arm_cmse.h` 의 구조체 정의가 `-mcmse` 에 의존한다.

### include 경로

```
src/bsp/device                      ← 우리 설정 헤더가 벤더보다 먼저 와야 한다
firm-sdk/lib/Realtek/fwlib/include
firm-sdk/lib/Realtek/cmsis
firm-sdk/lib/Realtek/swlib/include
firm-sdk/lib/Realtek/swlib/string
firm-sdk/lib/Realtek/app/monitor/include
firm-sdk/lib/Realtek/app/xmodem          ← rtl8721d.h 가 xmodem_update_rom.h 를 include
firm-sdk/lib/Realtek/os_dep/include
firm-sdk/lib/Realtek/misc
firm-sdk/lib/Realtek/imgtool_floader/include
```

> `firm-sdk/lib/Realtek/inc_hp/` 는 **include 경로에 넣지 않는다.** 그 안의
> `platform_autoconf.h` / `platform_opts.h` 는 SDK 예제 설정(WiFi·lwIP·mbedTLS 전부 on)이므로
> 참조용으로만 둔다. 우리 버전은 `src/bsp/device/` 에 있다. (§5.5)

## 5.3 GCC 14 vs GCC 6 — 완화 플래그가 필요한 이유

벤더 코드는 GCC 6.4.1 기준으로 작성됐는데, **GCC 14 는 다음을 기본 error 로 승격**했다:

| 진단 | 예시 |
|---|---|
| `int-conversion` | `rtl8721d_gdma_memcpy.c:43` — `GDMA_ChnlAlloc` 인자 3에 포인터를 정수로 전달 |
| `implicit-function-declaration` | FreeRTOS 함수들 (bare-metal 에서는 선언 없음) |
| `incompatible-pointer-types` | 벤더 콜백 캐스팅 |

`-Wno-error=` 로 **경고로 되돌린다.** 벤더 소스에만 적용하고 우리 코드(`src/ap`, `src/hw`, `src/bsp`)에는
기존처럼 `-Wall` 을 엄격하게 유지한다 — CMake `set_source_files_properties()` 로 분리한다.

## 5.4 1단계 빌드 제외 파일 (14개)

`CMakeLists.txt` 의 `EXCLUDE_PATHS` 로 제외한다. 기존 프로젝트에 이미 있는 idiom 이다.

| 파일 | 이유 |
|---|---|
| `fwlib/ram_hp/rtl8721dhp_app_start.c` | **우리 파생본**(`src/bsp/device/nu87_app_start.c`)이 대체. 원본은 대조용 |
| `fwlib/ram_hp/rtl8721dhp_sd.c` | FreeRTOS 필요 (`pvPortMalloc` / `vPortFree`) |
| `fwlib/ram_hp/rtl8721dhp_simulation.c` | FreeRTOS 필요 (`xTaskCreate` / `vTaskDelay` / `pdTRUE`) |
| `fwlib/ram_common/rtl8721d_flash_ram.c` | FreeRTOS 필요 (`xTaskGetSchedulerState`) — **OTA 단계에서 복원** |
| `fwlib/ram_common/rtl8721d_ipc_api.c` | FreeRTOS 필요 (`vTaskStackAddr`) — **무선 단계에서 복원 필수** |
| `fwlib/ram_common/rtl8721d_gdma_ram.c` | `osdep_service.h` (OS 추상화) 필요 |
| `fwlib/ram_common/rtl8721d_qdec.c` | `APBPeriph_QDEC0` 미정의 — 이 칩에 없음. SDK KM4 목록에도 없음 |
| `fwlib/ram_common/rtl8721d_efuse.c` | SDK KM4 목록에 없음 (ROM 에 있음) |
| `fwlib/usrcfg/rtl8721d_bootcfg.c` | 부트로더 몫 (`make/bootloader`) — **부트로더 단계에서 사용** |
| `fwlib/usrcfg/rtl8721dhp_boot_trustzonecfg.c` | TrustZone 미사용 |
| `fwlib/usrcfg/rtl8721dlp_flashcfg.c` | KM0 몫 |
| `fwlib/usrcfg/rtl8721dlp_sleepcfg.c` | KM0 몫 |
| `fwlib/usrcfg/rtl8721dlp_pinmapcfg.c` | KM0 몫 |
| `fwlib/usrcfg/rtl8721dlp_pinmapcfg_qfn88_evb_v1.c` | QFN88 EVB 변종 — 위 파일과 **같은 심볼을 정의**하므로 함께 빌드하면 중복 정의 |

**반드시 빌드에 포함해야 하는 것** (`app_start()` 가 참조하는 전역):
- `fwlib/usrcfg/rtl8721dhp_intfcfg.c` → `psram_dev_config`
- `fwlib/usrcfg/rtl8721d_wificfg.c` → `rtk_wifi_config.km4_cache_enable`
  (**WiFi 를 안 써도 필요하다**)
- `fwlib/usrcfg/rtl8721dlp_intfcfg.c`, `fwlib/usrcfg/rtl8721d_ipccfg.c`

## 5.5 벤더 설정 헤더는 우리 것으로 대체한다

SDK 헤더가 요구하는 설정 헤더 3개를 `src/bsp/device/` 에 **우리 버전**으로 둔다.
STM32 프로젝트가 `stm32h5xx_hal_conf.h` 를 같은 자리에 두었던 것과 동일한 패턴이다.

| 파일 | 원본 | 우리 버전이 하는 일 |
|---|---|---|
| `platform_autoconf.h` | `inc_hp/` (211줄, menuconfig 산출물) | 칩·클럭만 남기고 RTOS/WiFi/BT/mbedTLS off. `CPU_CLOCK_SEL_VALUE (0)` = 200MHz |
| `platform_opts.h` | `inc_hp/` (778줄) | WLAN/lwIP/SSL/클라우드 전부 off. OTA 전송도 off |
| `autoconf.h` | WLAN 드라이버 설정 헤더 | 빈 stub. `CONFIG_REPEATER` 미정의 → `psram_dev_enable = FALSE` (8720DF 에 맞음) |

이것이 곧 **설정이 우리 계층에 있다**는 원칙의 실행이다. 기능 스위치는 `src/hw/hw_def.h`,
벤더 SDK 가 기대하는 매크로는 `src/bsp/device/` 에 있고, 둘이 섞이지 않는다.

### 벤더 헤더 로컬 패치

`fwlib/include/rtl8721d_ota.h` 는 `HTTP_OTA_UPDATE` / `HTTPS_OTA_UPDATE` / `SDCARD_OTA_UPDATE` 를
**아무 조건 없이 `#define`** 한다. `ameba_soc.h` 가 이 헤더를 무조건 include 하므로
**mbedTLS 전체가 강제 의존**이 된다.

→ `firm-sdk/tools/patches/0001-ota-header-make-transport-opt-in.patch` 로 `CONFIG_OTA_*` 가드를 씌운다.
`sync_sdk.py` 가 동기화 후 자동 적용한다.

## 5.6 툴체인 파일 격리 — 무선 단계 대비 보험

**아키텍처 플래그와 컴파일러 지정을 `firm-sdk/tools/arm-none-eabi-gcc.cmake` 안에만 둔다.**

이유: 무선 단계에서 `lib_wlan.a`(3.5MB), `btgap.a`(629KB) 같은 **prebuilt 아카이브를 링크**해야 하는데,
이들은 asdk GCC 6.4.1/newlib 로 빌드된 오브젝트다. newlib 심볼 불일치나 ABI attribute 충돌이
날 수 있다. 그때 **툴체인 파일만 `firm-sdk/tools/asdk-gcc.cmake` 로 교체**하면 되고
**소스와 프로젝트 구조는 전혀 손대지 않는다.**

이것이 CMake 독립 빌드를 택한 실질적 보험이다. ([11](11-wireless-plan.md) ① 항목)

## 5.7 크로스플랫폼 — Windows / macOS / Linux

**벤더 셸 스크립트와 플랫폼별 바이너리에 의존하지 않는다.**

SDK 의 이미지 생성 경로는 크로스플랫폼이 아니다:
- `prepend_header.sh`, `pad.sh`, `imagetool.sh` — bash 전용
- `checksum` (Linux) / `checksum_MacOS` / `checksum.exe` — **플랫폼별 바이너리 3종**
- `ImageTool.exe` — Windows/.NET 전용

→ 우리 도구는 **전부 Python 3** 으로 만든다. 32바이트 헤더 부착은 `struct.pack` 몇 줄이면 된다.

| 도구 | 역할 |
|---|---|
| `firm-sdk/tools/sync_sdk.py` | SDK 서브모듈 → `firm-sdk/lib/Realtek/` 동기화 + 패치 적용 |
| `firm-sdk/tools/extract_blobs.py` | 공장 플래시 덤프에서 부팅 블롭 추출 |
| `firm-sdk/tools/make_image.py` | objcopy 결과에 헤더 부착 → `km0_km4_image2.bin` 생성 |
| `firm-sdk/tools/flash.py` | CP2102N UART 다운로드 (pyserial) |

벤더 스크립트/바이너리는 `firm-sdk/lib/Realtek/imgtool/` 에 **대조용으로만** 둔다.

### 개발환경 구축 — 공통

| 항목 | 필요 버전 | 비고 |
|---|---|---|
| Arm GNU Toolchain | **14.2.Rel1** 이상 권장 | 멀티립에 `thumb/v8-m.main+fp/hard` 가 있어야 한다 |
| CMake | 3.13 이상 | 4.x 검증됨 |
| Python | 3.8 이상 + `pyserial` | 플래싱/이미지 도구 |
| OpenOCD | **0.12.0** 이상 | SWD 디버깅. 0.12 는 `gdb_breakpoint_override`(밑줄) 표기 |
| Git | — | 서브모듈 (SDK 갱신 시에만) |

확인:
```bash
arm-none-eabi-gcc -print-multi-directory -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
# → thumb/v8-m.main+fp/hard  가 나와야 한다
python3 -c "import serial; print(serial.__version__)"
openocd --version
```

### macOS

```bash
brew install --cask gcc-arm-embedded     # 또는 brew install arm-none-eabi-gcc
brew install cmake open-ocd
pip3 install pyserial
```

- **CP210x 드라이버는 설치 불필요.** macOS 11+ 에 `AppleUSBSLCOM.dext` 로 내장되어 있다.
  포트는 `/dev/cu.usbserial-XXXX` 로 나타난다. (`/dev/tty.*` 는 DCD 를 기다려 블록되므로 쓰지 않는다)
- `st-info --probe` 는 `Found 0 stlink programmers` 를 낸다 (libusb 가 커널 드라이버 detach 권한 없음).
  **OpenOCD 는 정상 동작하므로 무시**한다.
- SVG 다이어그램을 직접 렌더링해 확인하려면 `brew install librsvg` (선택).

### Linux (Ubuntu / Debian)

```bash
sudo apt install cmake git python3-pip openocd
pip3 install pyserial
# 툴체인은 배포판 패키지(gcc-arm-none-eabi)가 구버전일 수 있으므로
# developer.arm.com 의 공식 tarball 권장
wget https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
sudo tar xf arm-gnu-toolchain-*.tar.xz -C /opt
export PATH=/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:$PATH
```

- **시리얼 포트 권한**: `sudo usermod -aG dialout $USER` 후 재로그인. 포트는 `/dev/ttyUSB0`.
- **OpenOCD udev 규칙**: ST-LINK 를 sudo 없이 쓰려면
  `sudo cp /usr/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/ && sudo udevadm control --reload`
- CP210x 드라이버는 커널에 내장(`cp210x` 모듈)되어 있다.

### Windows

```powershell
winget install Kitware.CMake
winget install Git.Git
winget install Python.Python.3.12
pip install pyserial
# 툴체인: developer.arm.com 의 arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-arm-none-eabi.exe
# OpenOCD: xpack-openocd 또는 OpenOCD-xPack 릴리스
```

- **CP210x 드라이버는 설치가 필요하다** — Silicon Labs 의 CP210x VCP Driver.
  Windows Update 로 자동 설치되기도 한다. 포트는 `COM3` 같은 형태.
- **ST-LINK 드라이버**: ST 의 ST-LINK USB driver, 또는 OpenOCD 사용 시 **Zadig 로 WinUSB(libusb) 설치**.
- 빌드 생성기: `cmake -S . -B build -G "MinGW Makefiles"` 또는 Ninja.
  기존 `firm-sdk/tools/arm-none-eabi-gcc.cmake` 가 `WIN32` 에서 `.exe` 접미사와 MinGW `make` 를 찾도록 되어 있다.
- 경로 구분자: 우리 Python 도구는 `pathlib` 을 쓰므로 문제없다.
  다만 **벤더 `*.sh` 는 Windows 에서 동작하지 않는다** — 그래서 Python 으로 대체했다.

## 5.8 빌드 / 플래싱 / 디버깅

```bash
# 빌드
cmake -S . -B build
cmake --build build -j

# 산출물
build/nu87-fw.elf           # GDB 용
build/nu87-fw.map           # 메모리 사용량 확인
build/km4_image2_all.bin    # KM4 이미지 (헤더 부착됨)
build/km0_km4_image2.bin    # 최종 — flash 0x08006000 에 쓴다

# UART 플래싱 (CP2102N 자동 다운로드)
python3 firm-sdk/tools/flash.py --port /dev/cu.usbserial-0001

# 콘솔
python3 -m serial.tools.miniterm --rts 0 --dtr 0 /dev/cu.usbserial-0001 115200

# SWD 디버깅
openocd -f firm-sdk/tools/openocd/nu87.cfg
arm-none-eabi-gdb build/nu87-fw.elf -ex "target extended-remote :3334"
```

`.vscode/tasks.json` 에 `build-configure` / `build-build` / `build-clean` / **`flash`** 를 둔다.
`compile_commands.json` 은 `CMAKE_EXPORT_COMPILE_COMMANDS ON` 으로 생성되고
`.vscode/c_cpp_properties.json` 이 이를 참조한다 (기존과 동일).
