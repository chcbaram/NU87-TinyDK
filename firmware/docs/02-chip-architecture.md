# 02. 칩 아키텍처 — RTL8720DF (Ameba-D)

출처: `hardware/ameba_datasheet_rtl872xd_v4.8.pdf` (UM0401 Rev 4.8, 2025-09),
`hardware/ameba_pinmux_rtl872xd_v2.0.xlsx`, SDK `ameba-rtos-d` @ `7569200f`,
그리고 **라이브 칩 SWD 실측**.

## 2.1 "rtl8721d"가 왜 우리 칩 파일인가

SDK 파일명이 전부 `rtl8721d*`인데 우리 칩은 RTL8720DF다. 혼란스러울 수 있으니 먼저 정리한다.

**`rtl8721d`는 칩 품번이 아니라 Ameba-D 패밀리 전체의 접두사다.** 실측 근거:

```
$ ls sdk/ameba-rtos-d/component/soc/realtek/
amebad                              # soc가 하나뿐

$ ls sdk/ameba-rtos-d/project/
realtek_amebaD_va0_example          # 프로젝트도 하나뿐

$ find sdk/ameba-rtos-d -iname "*rtl8720*"
(결과 없음)                          # rtl8720 이름의 파일은 0개
```

- 데이터시트 파일명도 `ameba_datasheet_rtl872xd`, 핀먹스도 `ameba_pinmux_rtl872xd` — **패밀리 단위 문서**다.
- 패밀리 구성원: RTL8720DF(QFN48, 4MB Flash, PSRAM 없음), RTL8720DN,
  RTL8721DM(QFN68, 4MB PSRAM), RTL8722DM 등.
- **패키지 차이는 컴파일타임이 아니라 런타임에 처리된다.** `rtl8721d_syscfg.h`:
  ```c
  #define SYSCFG_BD_QFN32                 ((u32)0x000000000)
  #define SYSCFG_BD_QFN48_MCM_8MBFlash    ((u32)0x000000001)
  #define SYSCFG_BD_QFN48                 ((u32)0x000000002)
  #define SYSCFG_BD_QFN68                 ((u32)0x000000007)
  ```
  즉 같은 바이너리가 QFN48/68/88에서 동작한다.
- 보드/패키지별 차이가 실제로 들어가는 곳은 **`fwlib/usrcfg/`** 뿐이다
  (`rtl8721dlp_pinmapcfg.c` vs `rtl8721dlp_pinmapcfg_qfn88_evb_v1.c` 두 변종이 존재하는 것이 증거).
- 추가 실증: Particle Photon 2(**RTL8721DM**)용 OpenOCD AP 맵이 우리 **RTL8720DF**에서
  그대로 동작했다 → 동일 실리콘. [03](03-debug-swd.md)

**결론: `rtl8721dhp_*.c` / `rtl8721d_*.h`를 그대로 쓴다. 손볼 곳은 `usrcfg/`다.**

## 2.2 듀얼코어

| | **KM0** (`project_lp`, LP=low power) | **KM4** (`project_hp`, HP=high perf) |
|---|---|---|
| 코어 | "Real-M200", Armv8-M **Cortex-M23 호환** | "Real-M300", Armv8-M **Cortex-M33 호환** |
| 클럭 | ~20 MHz | **최대 200 MHz** |
| 캐시 | I 16KB + D 4KB | **I 32KB + D 4KB** |
| FPU/DSP | 없음 | **FPU(fpv5-sp-d16) + DSP + TrustZone-M** |
| SRAM | 64KB @ `0x00080000` + Retention 1KB @ `0x000C0000` | **512KB @ `0x10000000`** |
| 담당 | PMU/슬립/웨이크, RTC, keyscan, captouch, 플래시 클럭 설정, **WLAN MAC 펌웨어 로딩** | **WiFi 드라이버 + supplicant + lwIP**, **BLE 호스트 스택**, mbedTLS, 애플리케이션, shell |
| MPU | — | 8 리전 |
| SWD | 2 BP / 1 WP (AP1) | 2 BP / 1 WP (AP2) |

**SWD 실측 확인:**
```
Info : [rtl872xd.km0] Cortex-M23 r1p0 processor detected
Info : [rtl872xd.km0] target has 2 breakpoints, 1 watchpoints
Info : [rtl872xd.km4] Cortex-M55 r1p0 processor detected     ← OpenOCD 오식별, 실제는 KM4
Info : [rtl872xd.km4] target has 2 breakpoints, 1 watchpoints
```
> OpenOCD 0.12가 KM4를 "Cortex-M55"로 표시하지만 이는 CPUID 파트번호 테이블 문제일 뿐,
> 기능상 ARMv8-M Main 코어(FPB/DWT 정상)로 완전히 동작한다. 무해하다.

두 코어는 **IPC**로 통신한다 (`rtl8721d_ipc_api.c`, `usrcfg/rtl8721d_ipccfg.c`).
양쪽 `main()`이 가장 먼저 `IPC_INTHandler`를 등록한다.

### 무선 담당 코어 (중요)

- **WiFi 드라이버/스택은 KM4**에서 돈다 (`lib_wlan.a` 3.5MB, supplicant, lwIP).
- **WLAN MAC의 온칩 펌웨어 블롭은 KM0 이미지에 링크되어 KM0가 로드한다** (`lib_wifi_fw.a`).
  KM0의 `main()`에서 `wifi_FW_init_ram()` 호출. → **KM0를 스톡으로 유지해야 하는 결정적 이유.**
- **BLE 호스트 스택은 KM4** (`btgap.a` 629KB). BT 컨트롤러는 자체 ROM을 가진 별도 하드웨어 블록이고,
  펌웨어 패치는 **파일이 아니라 C 배열**(`bt_normal_patch.c`의 `rtlbt_fw[]`, ~9.7KB)로 링크된다.

## 2.3 메모리 맵

### RAM (`project_hp/asdk/rlx8721d_layout_is.ld` 실제 값)

| 리전 | 주소 | 크기 | 용도 |
|---|---|---|---|
| `ROMBSS_RAM_COM` | `0x10000000` | 4K | ROM BSS 공용 |
| `ROMBSS_RAM_NS` | `0x10001000` | 4K | ROM BSS 비보안 |
| `RSVD_RAM_NS` | `0x10002000` | 8K | 예약 |
| `MSP_RAM_NS` | `0x10004000` | 4K | 비보안 메인 스택 |
| **`BD_RAM_NS`** | **`0x10005000`** | **456K** | **KM4 애플리케이션 SRAM** |
| `ROMBSS_RAM_S` | `0x1007C000` | 4K | ROM BSS 보안 |
| `BOOTLOADER_RAM_S` | `0x1007D000` | 8K | 부트로더 |
| `MSP_RAM_S` | `0x1007F000` | 4K | 보안 메인 스택 |
| `EXTENTION_SRAM` | `0x100E0000` | 128K | **BT 공유 SRAM** (BLE 미사용 시 64K 회수 가능) |
| `PSRAM_NS` | `0x02000020` | 4M | **RTL8720DF에는 없음** |
| KM0 SRAM | `0x00080000` | 64K | |
| `RETENTION_RAM` | `0x000C0000` | 1K | 딥슬립 유지 |

**실측 확인**: KM4를 halt했을 때 `psp: 0x10011010` → `BD_RAM_NS` 범위 안 ✓

### ROM

| 리전 | 주소 | 크기 |
|---|---|---|
| KM4 ITCM ROM (`IROM`) | `0x10100000` | 40K + `IROM_NS` `0x1010A000` 216K |
| KM4 DTCM ROM | `0x101C0000` (`DROM_NS` 80K) / `0x101D4000` (`DROM` 16K) |
| KM0 ITCM ROM | `0x00000000` | 96K |
| KM0 DTCM ROM | `0x00020000` | 16K |

**ROM에 뭐가 있나가 중요하다.** GPIO/UART/Delay/Pinmux 등 주변장치 드라이버 대부분이
마스크 ROM에 있고 `_LONG_CALL_`로 선언되어 `ld/rlx8721d_rom_symbol_acut.ld`로 해석된다.
mbedTLS 2.4.0도 ROM에 있다. → **우리가 복사할 소스가 매우 적어지는 이유.**

### 주변장치

| | 주소 |
|---|---|
| KM4 peripheral | `0x40000000` (보안 alias `0x50000000`) |
| KM0 peripheral | `0x48000000` |
| **LOGUART (UART2)** | **`0x48012000`** (KM0 도메인, KM4에서도 접근 가능) |
| GPIOA / GPIOB | `0x48014000` |
| `LP_KM4_CTRL` | `0x4800021C` — 실측값 `0x0101001F` |
| PERI_ON KM4 boot cfg | `0x480003F8` — 실측값 `0x00000201` |

## 2.4 플래시 — 시리얼 연결 + XIP (실측 증명)

**질문: 이 칩의 플래시는 시리얼로 연결되고 XIP로 구동되는가? → 예, 둘 다 그렇다.**

### 물리 구성
- RTL8720DF는 **4MB SPI NOR 플래시를 패키지 내에 내장**(die-stack)하고 있고,
  **SPI 플래시 컨트롤러**로 연결된다.
- **QFN48(8720DF)과 QFN68은 SPI/DSPI만 지원한다. QSPI는 QFN88 전용.**
  → 플래시 대역폭이 QSPI 파트보다 낮다. XIP 성능에 영향.
- 플래시 컨트롤러에 **캐시 + RSIP/Flash-MMU**가 붙어 있어 코드가 XIP로 실행된다.
  KM4는 32KB I-cache가 이를 받쳐준다.

### 주소 창

| 창 | 범위 | 의미 |
|---|---|---|
| 물리 | `0x08000000`~`0x0FFFFFFF` (128MB 창) | 플래시 물리 주소. 부트로더/이미지 툴이 쓰는 주소 |
| **KM0 XIP 가상** | **`0x0C000000`** | Flash MMU가 리맵 |
| **KM4 XIP 가상** | **`0x0E000000`** | Flash MMU가 리맵 |
| PSRAM 창 | `0x02000000`~`0x07FFFFFF` | 8720DF에는 없음 |

### 실측 증명

KM4를 halt하니 **PC가 XIP 가상주소 영역에 있었다**:
```
[rtl872xd.km4] halted due to debug-request, current mode: Thread
xPSR: 0x21000000 pc: 0x0e002ca6 psp: 0x10011010
                    ^^^^^^^^^^ KM4 XIP 가상주소에서 실행 중
```

그리고 가상주소와 물리주소를 각각 읽어 비교했다:
```
SWD로 가상 0x0E000020 읽기 : 20032102 be1ef000 2101b508 f0002003 f44ffef9 ...
플래시 덤프 물리 0x08021020: 20032102 be1ef000 2101b508 f0002003 f44ffef9 ...   ← 동일

SWD로 가상 0x0C000020 읽기 : f004b510 6b00ff7d bf00bd10 f004b510 6c03ff77 ...
플래시 덤프 물리 0x08006020: f004b510 6b00ff7d bf00bd10 f004b510 6c03ff77 ...   ← 동일
```

**→ 확정: Flash MMU가 `0x0E000000 → 물리 0x08021000` (KM4),
`0x0C000000 → 물리 0x08006000` (KM0)으로 매핑 중이다.**

### 이 구조가 만드는 실질적 제약

1. **물리주소를 코드에 하드코딩하면 안 된다.** Flash MMU는 OTA1/OTA2 두 물리 슬롯을
   **같은 가상주소**로 매핑한다. 그래서 동일 바이너리가 어느 슬롯에서든 부팅된다.
   물리주소를 박으면 OTA2 부팅 시 깨진다.
2. **플래시 쓰기 중에 XIP 코드를 실행할 수 없다.** 플래시가 쓰기 모드일 때 그 플래시에서
   명령어를 페치할 수 없으므로, **플래시 쓰기 루틴과 그 경로의 인터럽트 핸들러는 반드시
   SRAM에 있어야 한다** (`SRAM_ONLY_TEXT_SECTION`). 이걸 놓치면 OTA/NVS 구현 시
   재현이 어려운 하드폴트가 난다.
3. **XIP 코드에는 소프트웨어 브레이크포인트를 걸 수 없다.** 하드웨어 BP 2개가 전부다.
   OpenOCD 설정에 `gdb_breakpoint_override hard`가 필수인 이유.
4. 1단계는 **전량 SRAM 배치**로 이 문제들을 전부 회피한다. 456KB면 LED+CLI에 충분하다.

## 2.5 부팅 스트랩 핀

데이터시트 p.25 Table 3-2 "Power on trap pins" — QFN48 열이 RTL8720DF다.

| 심볼 | 공유 핀 | QFN48 핀# | 의미 |
|---|---|---|---|
| **NORMAL_MODE_SEL** | **PA[27]** | 26 | 1 = 정상, 0 = Realtek test/debug 모드. **파워온 시 Low면 부팅 실패** |
| **UART_DOWNLOAD** | **PA[7]** | 3 | 1 = 플래시 부팅, **0 = UART로 이미지 다운로드** |
| **SPS_SEL** | **PA[30]** | 27 | 1 = 내부 1.1V 레귤레이터 SPS(스위칭) 모드, 0 = LDO 모드 |
| **CHIP_EN** | — | 1 | 1 = enable, 0 = shutdown |

TinyDK에서: PA27 = SW3 + P3 헤더 2번, PA7 = SW1 + CP2102N DTR 경유, PA30 = 모듈 내부 10K 풀업(SPS 모드).

## 2.6 SWD 핀 배정

**JTAG 없음 — SWD 전용, SWO/트레이스 없음.** 두 코어가 각자 debug port를 갖지만
**물리 핀은 2개를 공유**하고, 코어 선택은 **AP 인덱스**로 한다 (AP1=KM0, AP2=KM4).

| eFuse 0x0E bit[0] | SWD_DATA | SWD_CLK |
|---|---|---|
| **0 (기본)** | **PA[27]** (내부 풀업) | **PB[3]** (풀 없음) |
| 1 (프로그램됨) | PB[19] (내부 풀업) | PB[18] |

핀 공유 충돌:
- **PA[27]** = SWDIO + `NORMAL_MODE_SEL` 스트랩 + GPIO/LP_UART_RTS/WLAN_ACT (3중 용도)
- **PB[3]** = SWCLK + ADC_CH6 + PCM_SYNC
- PB[18]/PB[19] (대체 SWD) = HS_UART0 RX/TX, SPI0 MOSI/MISO, SD_D2/D3

전기적 규격 (p.50–51): SWCLK 주기 ≥ 50ns (≤ 20MHz), 듀티 30~70%.

**펌웨어가 SWD를 죽일 수 있다**: `Pinmux_Swdoff()`가 `REG_SWD_PMUX_EN`의
`BIT_LSYS_SWD_PMUX_EN`을 클리어한다. 우리 코드에서 절대 호출하지 않도록 주의.

**디버그 포트 락**: AP3가 CTRL_AP이고 128비트 SWD 패스워드 레지스터가 있다
(`AP3_SWDPWD.JLinkScript`). 우리 모듈은 잠겨 있지 않음 — 실측으로 확인됨.

## 2.7 무선 스펙

| | |
|---|---|
| WiFi | 802.11 a/b/g/n, **듀얼밴드 2.4/5GHz**, 40MHz BW, 150Mbps PHY |
| BLE | **BLE 5.0 only (BR/EDR 없음)**. `bt_flags.h`: `F_BT_BREDR_SUPPORT 0`, `F_BT_LE_5_0_SUPPORT 1` |
| BLE 미지원 | Extended Advertising 0, LE Privacy 0 |
| 안테나 | NU87 = NCWB87R01VC (칩 안테나) |
| 공존 | `rtk_coex.c` — 2.4GHz 프론트엔드 공유 |
