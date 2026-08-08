# 04. SDK 벤더링 — 서브모듈로 버전 고정 + 선별 복사

## 4.1 원칙

**서브모듈은 "출처와 버전의 증명"일 뿐, 빌드 의존성이 아니다.**

```
git clone <repo> && cd firmware/nu87-fw
cmake -S . -B build && cmake --build build      # ← 서브모듈 없이 그대로 빌드된다
```

SDK 전체는 **241MB**다. 이걸 저장소에 넣거나 매번 clone 하게 만들 이유가 없다.
필요한 것만(**229 파일, 2.9MB**) `firm-sdk/lib/Realtek/` 에 복사해 커밋하고, 빌드는 그것만 본다.

SDK 를 새 버전으로 올리거나 파일을 추가할 때만:
```bash
python3 firm-sdk/tools/sync_sdk.py         # 내부에서 submodule update --init --depth 1 수행
git diff --stat firm-sdk/lib/Realtek/  # 무엇이 바뀌었는지 반드시 검토
```

## 4.2 구성 요소

| 파일 | 역할 |
|---|---|
| `.gitmodules` → `sdk/ameba-rtos-d` | 커밋 고정. 현재 `7569200f` (2026-06-18) |
| `firm-sdk/tools/sdk_manifest.txt` | **복사 대상의 단일 진실 원천.** `<SDK 경로> -> <lib 경로>` |
| `firm-sdk/tools/sync_sdk.py` | manifest 처리 + 패치 적용 + 버전 기록 |
| `firm-sdk/tools/patches/*.patch` | 벤더 헤더에 대한 로컬 수정. 동기화 후 자동 적용 |
| `firm-sdk/lib/Realtek/.sdk_version` | 어느 SDK 커밋에서 왔는지 자동 기록 |

`sync_sdk.py` 는 **Python** 이다 (셸 스크립트 아님) — Windows/macOS/Linux 공통. ([05](05-build-system.md) §5.7)

## 4.3 manifest 방침

STM32 프로젝트가 `STM32H5xx_HAL_Driver/Src/*.c` 를 전부 넣고 `--gc-sections` 로 떨어내던 것과
**동일한 idiom** 으로 간다. `.c` 도 디렉토리 단위로 넉넉히 가져오고,
빌드에서 빼야 하는 파일은 `CMakeLists.txt` 의 `EXCLUDE_PATHS` 로 처리한다.
의존 관계를 손으로 추적하는 비용이 더 크다.

```
component/soc/realtek/amebad/fwlib/include/      -> fwlib/include/       (69)
component/soc/realtek/amebad/cmsis/              -> cmsis/               (21)
component/soc/realtek/amebad/swlib/              -> swlib/               (45)
component/soc/realtek/amebad/fwlib/ram_hp/       -> fwlib/ram_hp/        (14)  KM4 전용
component/soc/realtek/amebad/fwlib/ram_common/   -> fwlib/ram_common/    (20)  양 코어 공용
component/soc/realtek/amebad/fwlib/usrcfg/       -> fwlib/usrcfg/        (10)  보드 설정
...
```

**`fwlib/ram_lp/` (KM0 전용) 는 복사하지 않는다.** KM4 이미지에 들어가면 안 된다.

### 어떤 파일이 필요한지는 SDK Makefile 이 정답을 갖고 있다

추측하지 않고 `project_hp/asdk/make/target/fwlib/Makefile` 의 `CSRC` 목록을 근거로 삼았다.
거기서 알아낸 사실들:

- **`rtl8721d_gpio.c` 가 목록에 없다** → GPIO 는 전부 ROM 이다. 우리가 복사할 소스가 적어지는 이유.
- **`usrcfg/` 에서 KM4 가 쓰는 것은 4개뿐**이다:
  `rtl8721dlp_intfcfg.c`, `rtl8721dhp_intfcfg.c`, `rtl8721d_ipccfg.c`, `rtl8721d_wificfg.c`.
  나머지 6개는 KM0/부트로더 몫이다.
  → 처음에 `rtl8721d_bootcfg.c` 를 KM4 용으로 넣었던 것은 **틀렸다** (부트로더 몫).
- `ram_common/` 20개 중 KM4 는 18개만 쓴다 (`rtl8721d_efuse.c`, `rtl8721d_qdec.c` 제외).

## 4.4 로컬 패치

### 0001 — OTA 헤더의 전송 방식 선언을 opt-in 으로

`fwlib/include/rtl8721d_ota.h` 가 `HTTP_OTA_UPDATE` / `HTTPS_OTA_UPDATE` / `SDCARD_OTA_UPDATE` 를
**아무 조건 없이 `#define`** 한다. `ameba_soc.h` 가 이 헤더를 무조건 include 하므로,
`ameba_soc.h` 를 쓰는 순간 **mbedTLS 전체와 FatFs 가 강제 의존**이 된다.

```
firm-sdk/lib/Realtek/fwlib/include/rtl8721d_ota.h:173:10:
    fatal error: mbedtls/version.h: No such file or directory
```

→ 세 매크로를 `CONFIG_OTA_HTTP` / `CONFIG_OTA_HTTPS` / `CONFIG_OTA_SDCARD` 로 감싼다.
기본값 off, 필요할 때 `src/bsp/device/platform_opts.h` 에서 켠다.
OTA 자체(`ota_get_cur_index`, 슬롯 전환)는 영향받지 않고 **전송 방식 선언만** 조건부가 된다.

> 패치는 **손으로 쓰지 말고** 실제 파일에서 `difflib.unified_diff` 로 생성해야 한다.
> 벤더 코드에는 후행 탭 같은 공백이 있어서 손으로 쓴 컨텍스트는 `git apply` 가 거부한다.
> (`#define HTTP_OTA_UPDATE\t\n` — 실제로 여기서 한 번 막혔다)

## 4.5 KM0 블롭 — SDK 빌드 없이 확보

부팅에는 KM0 이미지와 부트로더가 필요하지만, 우리가 만드는 것이 아니다.
**공장 출하 플래시를 SWD 로 4MB 백업해 거기서 잘라냈다.** SDK 참조 빌드가 필요 없어졌다.

```bash
# 1) 전체 백업 (반드시 먼저. 4MB, SWD 로 약 2분)
openocd -f firm-sdk/tools/openocd/nu87.cfg -c "init" \
        -c "nu87_backup backup/nu87_factory_flash_4MB.bin" -c "shutdown"

# 2) 블롭 추출
python3 firm-sdk/tools/extract_blobs.py backup/nu87_factory_flash_4MB.bin -o firm-sdk/prebuilt/
```

```
=== 블롭 경계 ===
  km0_boot_all.bin   = flash[0x08000000 .. 0x08001194)  2 파트
  km4_boot_all.bin   = flash[0x08004000 .. 0x08005168)  2 파트
  km0_image2_all.bin = flash[0x08006000 .. 0x08021000)  = 0x1B000 (110592 B)
    4KB 정렬: OK (패딩 포함)

=== 출력 ===
  km0_boot_all.bin           4500 B  sha256:453c880307fc3890
  km4_boot_all.bin           4456 B  sha256:05fbf808d43113ea
  km0_image2_all.bin       110592 B  sha256:9595267fe00aa42d
```

**주의 — 처음에 틀렸던 것**: 부트로더도 **2파트 체인**이다.
첫 파트만 자르면(2064 B / 32 B) 부팅하지 않는다. 그리고 IMG2 체인에는 **4KB 패딩**이 끼어 있어
헤더 워크가 거기서 멈춘다. `extract_blobs.py` 는 두 가지를 모두 처리한다. ([06](06-boot-image.md))

### 한계 — 무선 단계에서 해결해야 함

이 KM0 이미지는 **공장 펌웨어의 SDK 버전**으로 빌드된 것이고, 우리 서브모듈 커밋과 다를 수 있다.

- **1단계(bare-metal LED/CLI)는 문제없다.** IPC 를 전혀 쓰지 않는다.
- **무선 단계에서는 문제가 된다.** KM4 코드와 KM0 이미지의 **IPC 테이블 레이아웃·공유메모리 구조가
  일치해야 한다.** 어긋나면 조용히 오동작한다 (부팅은 되는데 WiFi 만 안 켜지는 식).

→ 무선에 들어갈 때 SDK 를 실제로 빌드해 **버전이 맞는 KM0 이미지**로 교체한다.
그때를 위해 `firm-sdk/prebuilt/.sdk_version` 과 `firm-sdk/lib/Realtek/.sdk_version` 을 대조하는 검사를
CMake configure 단계에 넣는다. **서브모듈로 커밋을 고정해 둔 실질적 이유가 이것이다.**

SDK 참조 빌드가 필요해지면:
```bash
cd sdk/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp
make -C toolchain        # asdk 자동 다운로드 (macOS arm64 용 asdk-10.4.1 존재)
make all                 # → asdk/image/km0_image2_all.bin
```

## 4.6 향후 부트로더 / 펌웨어 구조 대비

manifest 에 지금은 빌드하지 않지만 앞으로 필요한 것들을 **미리 복사해 둔다**
(`CMakeLists.txt` `EXCLUDE_PATHS` 로 제외). 나중에 SDK 를 다시 뒤질 필요가 없다.

| 그룹 | 파일 | 대응하는 기존 구조 |
|---|---|---|
| **부트로더** | `bootloader/boot_{ram,flash}_hp.c`, `boot_trustzone_hp.c`, `boot_flash_hp_sboot.c`, KM0 쪽 `boot_{ram,flash}_lp.c` | `apm32e103-kit-boot` 프로젝트 |
| **2nd stage 부트로더** | `bootloader_2nd/` (`readme.txt` 에 절차, 자체 `.ld` 포함) | 커스텀 부트로더의 공식 참조 구현 |
| **OTA** | `misc/rtl8721d_ota.c` (66KB), `fwlib/ram_hp/rtl8721dhp_ota_ram.c` | `loader.h` / `loader.c` |
| **파라미터 저장** | `file_system/ftl/`, `file_system/kv/` | `common/hw/include/nvs.h` |
| **UART 업데이트** | `app/xmodem/` (구현체는 ROM, 헤더만) | `common/hw/include/ymodem.h` |
| **RAM 플래시로더** | `imgtool/imgtool_flashloader_amebad.bin` + `imgtool_floader/src/` | `firm-sdk/tools/flash.py` 가 사용. 프로토콜 참조 |
| **부트로더 링커스크립트** | `ld/rlx8721d_img1_s.ld`, `ld/rlx8721d_rom_symbol_acut_boot.ld` | `APM32E103RE_BOOT.ld` |
| **TrustZone/보안부팅** | `img3/` | (참조용) |

### 설계 방향 — 커스텀 부트로더보다 OTA1/OTA2 를 쓴다

`apm32e103-kit` 은 `-boot` 와 `-fw` 두 개의 독립 CMake 프로젝트로 나뉘고,
`.version` 섹션(`0x08000400`) + `firm_ver_t` / `firm_tag_t` 로 핸드셰이크한다.

**AmebaD 에서는 그 구조를 그대로 옮기지 않는 것이 낫다.** 이유:
- 칩이 **이미 부트로더(ROM + IMG1)와 듀얼 슬롯 OTA를 갖고 있다.**
  Flash MMU 가 OTA1(`0x08006000`) / OTA2(`0x08106000`) 를 **같은 가상주소**로 매핑하므로
  동일 바이너리가 어느 슬롯에서든 부팅한다 — 슬롯별 재링크가 필요 없다.
- 커스텀 정책은 `usrcfg/rtl8721d_bootcfg.c` 의 훅으로 주입할 수 있다:
  `OTA1_START` / `OTA2_START` / `Force_OTA1_GPIO` / **`FwCheckCallback`** / **`OTASelectHook`**.
- `.version` 을 고정 번지에 두는 방식은 Ameba 이미지 포맷과 맞지 않는다 → 평범한 `const` rodata 로.

**그리고 업데이트 전송 계층은 기존 추상화를 그대로 쓴다.**
`common/hw/include/cmd.h` 의 `cmd_driver_t` 가 이미 전송 방식에 무관한 vtable 이다
(`uart_driver_t` 와 동일한 idiom):

```c
typedef struct cmd_driver_t_ {
  uint32_t  args[32];
  bool     (*open)(void *args);
  bool     (*close)(void *args);
  uint32_t (*available)(void *args);
  bool     (*flush)(void *args);
  uint8_t  (*read)(void *args);
  uint32_t (*write)(void *args, uint8_t *p_data, uint32_t length);
} cmd_driver_t;
```

`apm32e103-kit-boot` 에 이미 `cmd_uart.c` 와 **`cmd_udp.c`** 두 드라이버가 있다.
→ NU87 에서는 여기에 **`cmd_wifi.c`** (lwIP 소켓) 와 **`cmd_ble.c`** (GATT) 를 추가하면
**프로토콜(`cmd.h`)과 쓰기 로직(`loader.c`)은 한 줄도 고치지 않고** WiFi/BLE 업데이트가 된다.
이것이 계층 분리의 배당금이다.

자세한 내용은 [12-wireless-plan.md](12-wireless-plan.md).
