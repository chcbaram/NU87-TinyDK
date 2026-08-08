# 03. SWD 디버깅 환경 — OpenOCD + ST-LINK

**결론부터: ST-LINK/V2-1 + OpenOCD 0.12로 RTL8720DF의 KM0/KM4 양쪽 디버깅이 동작한다.
CMSIS-DAP를 따로 살 필요가 없다.** 실측 검증 완료.

> 사전 조사에서는 "RTL872xD를 ST-LINK로 디버깅한 사례를 찾을 수 없음"이 결론이었고
> Particle 문서는 명시적으로 ST-LINK를 배제한다. 그런데 **실제로 해보니 됐다.**
> 조건이 까다로워서 그렇지 불가능한 게 아니었다 — 아래 3가지가 모두 맞아야 한다.

## 3.1 왜 까다로운가 — 3가지 조건

### 조건 1. AP 번호 — KM0/KM4는 AP0에 없다

RTL8720DF는 **단일 SW-DP + APSEL 기반 AP 선택** 구조다 (멀티드롭 아님).

| AP | 타입 | 용도 |
|---|---|---|
| AP0 | APB-AP | CoreSight APB — **코어 없음** |
| **AP1** | AHB-AP | **KM0 (Cortex-M23)** |
| **AP2** | AHB-AP | **KM4 (Cortex-M33급)** |
| AP3 | APB-AP | CTRL_AP — SWD 패스워드 / 디버그 락 |

근거: SDK `project_hp/jlink_script/AP1_KM0.JLinkScript`, `AP2_KM4.JLinkScript`, `AP3_SWDPWD.JLinkScript`.

### 조건 2. OpenOCD 인터페이스 파일 — `stlink.cfg`가 아니라 `stlink-dap.cfg`

이게 첫 번째 함정이었다. Homebrew OpenOCD 0.12의 두 파일은 **완전히 다른 드라이버**다:

```
interface/stlink.cfg      →  adapter driver hla      ← AP0 고정. 절대 안 됨
interface/stlink-dap.cfg  →  adapter driver st-link  ← dapdirect_swd 지원. 이걸 써야 한다
```

`stlink.cfg`로 시도하면:
```
Error: Debug adapter doesn't support 'dapdirect_swd' transport
```
HLA 경로는 소스에 `#define STLINK_HLA_AP_NUM 0`으로 **AP 0이 하드코딩**되어 있고
`hla_target`은 `dap create` / `-ap-num`과 조합할 수 없다. 따라서 HLA로는 KM0/KM4에 영원히 도달 못 한다.

### 조건 3. ST-LINK 하드웨어/펌웨어 버전

OpenOCD `stlink_usb.c`의 게이팅:

| 기능 플래그 | 요구 조건 | 왜 필요한가 |
|---|---|---|
| `STLINK_F_HAS_AP_INIT` | V2 ≥ **J28**, 또는 모든 V3 | AP 0 이외의 AP를 열기 위해 |
| `STLINK_F_HAS_CSW` | V2 ≥ **J32**, 또는 V3 ≥ J2 | `ap_num != 0`일 때 필수. 가드가 `if ((ap_num != 0 \|\| csw != 0) && !(flags & STLINK_F_HAS_CSW)) return ERROR_COMMAND_NOTFOUND;` |

그리고 **ST-LINK V3는 비-ST 실리콘을 펌웨어 레벨에서 거부한다** (OpenOCD ticket #275).
→ **V2 / V2-1만 가능.**

우리 프로브 실측:
```
USB: STM32 STLink, VID 0x0483, PID 0x3752   ← V2-1 (no-MSD). V3가 아니다 ✓
OpenOCD: Info : STLINK V2J46M33 (API v2)    ← J46 ≥ J32 ✓
```

> `st-info --probe`는 macOS에서 `Found 0 stlink programmers`가 나온다
> (libusb가 커널 드라이버 detach 권한을 못 얻음). **OpenOCD는 정상 동작하므로 무시**하면 된다.
> 펌웨어 버전은 OpenOCD 출력에서 확인하는 게 확실하다.

## 3.2 결선

P3 헤더("MP Header") 사용:

| P3 핀 | 신호 | ST-LINK 쪽 |
|---|---|---|
| 1 | VMCU | VTREF (전압 감지용. 보드 전원 공급도 가능) |
| **2** | **PA27 = SWDIO** | **SWDIO** |
| 3 | GND | **GND (필수)** |
| **4** | **PB3 = SWCLK** | **SWCLK** |
| 5 | CHIP_EN | (선택) nRST |

PA27/PB3는 P1 6번 / P2 6번에도 나와 있다.

**주의사항**
- **SW3을 누르지 말 것.** PA27을 GND로 당기고, PA27은 `NORMAL_MODE_SEL` 부팅 스트랩이다.
  파워온 시점에 Low면 Realtek test 모드로 들어가 부팅에 실패한다.
- 보드는 USB-C로 별도 급전하는 것이 안전하다 (SWD 커넥터 전원만으로는 부족할 수 있음).
- Realtek 1V0 EVB에는 CLK/DATA 실크가 뒤바뀐 오류가 있다(AN0400 §7.6). TinyDK는 해당 없음.

## 3.3 OpenOCD 설정

`firmware/firm-sdk/tools/openocd/nu87.cfg`:

```tcl
source [find interface/stlink-dap.cfg]      ;# ← stlink.cfg 아님!
transport select dapdirect_swd

adapter speed 480

source [find target/swj-dp.tcl]

set _CHIPNAME rtl872xd
swj_newdap $_CHIPNAME cpu -irlen 4 -expected-id 0x6ba02477
dap create $_CHIPNAME.dap -chain-position $_CHIPNAME.cpu

target create $_CHIPNAME.km0 cortex_m -dap $_CHIPNAME.dap -ap-num 1
target create $_CHIPNAME.km4 cortex_m -dap $_CHIPNAME.dap -ap-num 2

gdb_breakpoint_override hard                 ;# OpenOCD 0.12는 밑줄 표기
```

> `gdb breakpoint_override`(공백)는 0.13+ 문법이다. 0.12에서는
> `Error: invalid command name "gdb"`가 나므로 **`gdb_breakpoint_override`**를 써야 한다.

### ★ `-gdb-port` 를 고정하면 VS Code 디버깅이 안 된다

지정하지 않은 타겟은 `gdb_port` 설정값부터 **생성 순서대로** 포트를 받는다.

```
생성 0번 KM0 -> gdb_port + 0
생성 1번 KM4 -> gdb_port + 1
```

cortex-debug 는 설정 파일보다 **앞에** `-c "gdb_port N"` 을 붙이고 그 대역으로 접속한다.
여기서 `-gdb-port` 로 고정하면 접속할 곳이 없어 이렇게 실패한다:

```
Info : starting gdb server for rtl872xd.km4 on 3334     ← OpenOCD 가 연 포트
Failed to launch GDB: could not connect: Operation timed out.
                     (from target-select extended-remote localhost:50000)
```

그래서 `-gdb-port` 를 쓰지 않는다. 결과:

| 실행 방식 | KM0 | KM4 |
|---|---|---|
| 단독 (`gdb_port` 기본 3333) | 3333 | 3334 |
| cortex-debug (`gdb_port 50000`) | 50000 | 50001 |

CMSIS-DAP나 J-Link로 바꾸려면 첫 두 줄만 교체한다:
```tcl
source [find interface/cmsis-dap.cfg]   /   [find interface/jlink.cfg]
transport select swd
```

## 3.4 실측 결과

```
$ openocd -f firm-sdk/tools/openocd/nu87.cfg
Open On-Chip Debugger 0.12.0
force hard breakpoints
Info : STLINK V2J46M33 (API v2) VID:PID 0483:3752
Info : Target voltage: 3.229552
Info : clock speed 480 kHz
Info : stlink_dap_op_connect(connect)
Info : SWD DPIDR 0x6ba02477
Info : [rtl872xd.km0] Cortex-M23 r1p0 processor detected
Info : [rtl872xd.km0] target has 2 breakpoints, 1 watchpoints
Info : [rtl872xd.km4] Cortex-M55 r1p0 processor detected
Info : [rtl872xd.km4] target has 2 breakpoints, 1 watchpoints
Info : starting gdb server for rtl872xd.km0 on 3333
Info : starting gdb server for rtl872xd.km4 on 3334
```

`dap info`로 두 AP 모두 정상 열거:
```
AP # 0x1                         AP # 0x2
  AP ID register 0x84770001        AP ID register 0x84770001
  Type is MEM-AP AHB3              Type is MEM-AP AHB3
  MEM-AP BASE 0xe00ff003           MEM-AP BASE 0xe00ff003
  Designer is 0x479, Realtek       Designer is 0x479, Realtek
  Dev Arch ... "Processor debug architecture (ARMv8-M)"
  Dev Arch ... "DWT architecture"
  Dev Arch ... "Flash Patch and Breakpoint unit (FPB) architecture"
```

halt / 레지스터 / 메모리 읽기 전부 동작:
```
> halt
[rtl872xd.km4] halted due to debug-request, current mode: Thread
xPSR: 0x21000000 pc: 0x0e002ca6 psp: 0x10011010

> mdw 0x10005000 4                  ;# KM4 SRAM (BD_RAM_NS)
0x10005000: 0e001dfd 10005805 10001000 574b5452

> mdw 0x0E000000 4                  ;# KM4 XIP 가상
0x0e000000: 35393138 31313738 00010b70 0e000020

> dump_image flash.bin 0x08000000 0x40000
dumped 262144 bytes in 7.433669s (34.438 KiB/s)
```

**플래시 256KB 덤프까지 34 KiB/s로 성공** — 이것으로 이미지 레이아웃을 실측 확정했다.
([06-boot-image.md](06-boot-image.md))

> KM4가 "Cortex-M55"로 나오는 것은 OpenOCD 0.12의 CPUID 파트번호 테이블 문제다.
> FPB/DWT가 정상 검출되고 halt/step/메모리 접근이 모두 되므로 **기능상 무해**하다.

## 3.5 사용법

```bash
# 터미널 1
cd firmware/nu87-fw
openocd -f firm-sdk/tools/openocd/nu87.cfg

# 터미널 2 — KM4 디버깅
arm-none-eabi-gdb build/nu87-fw.elf
(gdb) target extended-remote :3334
(gdb) monitor halt
(gdb) load                    # SRAM 배치 이미지라면 직접 로드 가능
(gdb) break main
(gdb) continue
```

일회성 명령은 `-c`로:
```bash
openocd -f firm-sdk/tools/openocd/nu87.cfg -c "init" -c "targets rtl872xd.km4" \
        -c "halt" -c "reg pc" -c "resume" -c "shutdown"
```

### VS Code — `numberOfProcessors` / `targetProcessor`

cortex-debug 는 `servertype: "openocd"` 일 때 포트를 **스스로 할당**한다.
`gdbTarget` 은 `servertype: "external"` 전용이고, `targetId` 는 BMP/PyOCD 전용이라
OpenOCD 에서는 무시된다. 멀티코어용 옵션은 이 둘이다:

```jsonc
"numberOfProcessors": 2,     // 포트를 코어 수만큼 확보한다
"targetProcessor": 1,        // 몇 번째 코어에 붙을지. KM0=0, KM4=1
```

동작 방식은 이렇다. cortex-debug 는 `gdbPort`, `gdbPort1`, `tclPort`, `tclPort1`, …
순으로 빈 포트를 잡고, OpenOCD 에 `gdb_port <gdbPort>` 를 넘긴 뒤
`gdbPort<targetProcessor>` 로 접속한다. OpenOCD 가 타겟 생성 순서대로 포트를 배정하므로
**cfg 의 `target create` 순서와 `targetProcessor` 번호가 그대로 대응**한다.

`.vscode/launch.json` 의 세 가지:

| 구성 | targetProcessor | 용도 |
|---|---|---|
| Attach KM4 (OpenOCD) | 1 | 실행 중인 펌웨어에 붙는다 |
| Load to SRAM & Run KM4 | 1 | 빌드 → SRAM 로드 → `main` 까지 실행. 플래시를 건드리지 않는다 |
| Attach KM0 (OpenOCD) | 0 | KM0 는 스톡 블롭이라 심볼이 없다. 멈춰 놓고 보는 용도 |

## 3.6 반드시 지킬 순서 — KM0 먼저

**KM0가 KM4에 전원을 넣는다.** KM0의 `main()`이 `km4_boot_on()`을 호출해 KM4를 리셋에서 푼다.

- **KM0가 부팅하지 않았으면 AP2가 아예 응답하지 않는다.** J-Link/OpenOCD가 "cannot find KM4"를 낸다.
- 플래시가 비어 있으면: KM0 이미지를 먼저 굽고 → 리셋 → 그 다음 KM4에 attach.
- AN0400 §1.4: *"KM4는 KM0에 의해 전원이 켜지므로, J-Link나 Probe로 KM4에 접근하기 전에
  KM0가 이미 부팅했는지 확인해야 한다. 데모 보드의 Reset 버튼을 누르는 것이 권장 방법이다."*

우리 보드는 현재 순정 펌웨어가 들어 있어 KM0가 정상 부팅한 상태였고, 그래서 바로 붙었다
(콘솔에 `#calibration_ok:[2:19:11]` 출력 확인).

Particle은 reset 이벤트에서 `0x480003F8 = 0x02000201`을 써서 KM4를 부트 스핀에서 빼낸다.
우리 보드 실측값은 `0x00000201`이었다 — attach만 할 때는 불필요했지만,
`reset halt` 후 제어가 필요하면 이 훅을 추가한다.

### ★ halt 중에는 타이머가 멈춘다

STM32 와 다른 점이다. STM32 는 `DBGMCU_APB1_FZ` 프리즈 비트가 기본 0 이라 코어를
halt 해도 타이머가 계속 돈다. RTL8720DF 는 **KM4 를 halt 하면 타이머도 같이 선다.**

실측 — halt 상태에서 TIM4 CNT 를 1초 간격으로 3회:

```
0x40002214: 0000003d
0x40002214: 0000003d
0x40002214: 0000003d      ← 전혀 움직이지 않는다
```

EN 레지스터는 `0x00010100` (`TIM_CR_CNT_RUN | TIM_CR_CNT_STS`) 로 "동작 중" 을
가리키는데도 카운터가 서 있다. SDK 헤더에 프리즈를 제어하는 비트가 없다.

그래서 **시간 관련 측정에 디버거를 쓰면 안 된다.** `millis()` 를 GDB 로 두 번 읽어
비교하면 halt 구간만큼 빠진 값이 나온다. 대신 CLI 의 `md` 로 메모리를 직접 읽으면
코어를 멈추지 않고 측정할 수 있다:

```
md 0x1000541c 4        # bsp_tick_ms
```

이 방법으로 잰 TIM4 시간 기반 오차는 60초에 +0.001% 였다.

## 3.7 제약

| 제약 | 내용 |
|---|---|
| **하드웨어 브레이크포인트 2개** | 코어당 2개. **단일 스텝이 두 레지스터를 모두 요구**하는 경우가 있어 실질적으로 더 빡빡하다 |
| **소프트웨어 BP 불가** | 코드가 XIP로 외부/내장 플래시에서 실행되므로 SW BP를 못 쓴다 → `gdb_breakpoint_override hard` 필수 |
| 워치포인트 1개 | 감시 범위는 20바이트 미만 권장 (AN0400 §1.8) |
| SWO/트레이스 없음 | 데이터시트 확인 |
| **halt 하면 타이머가 멈춘다** | KM4 를 halt 하면 TIM0~5 카운터가 정지한다. STM32 의 DBGMCU 프리즈 비트처럼 끄고 켤 수단이 없다 → **디버거로 시간을 측정하면 안 된다** |
| 저전력이 SWD를 끊음 | KM0 슬립/클럭게이팅 시 디버그 포트가 죽는다. 리셋 직후 붙고 슬립을 막아야 한다 |
| 펌웨어가 SWD를 죽일 수 있음 | `Pinmux_Swdoff()`가 `BIT_LSYS_SWD_PMUX_EN`을 클리어. **우리 코드에서 호출 금지** |
| 디버그 포트 락 | AP3 CTRL_AP에 128비트 패스워드. 우리 모듈은 잠겨 있지 않음(실측 확인) |

→ **이 제약 때문에 1단계에서 CLI/log 인프라를 제대로 만들어 두는 것이 무선 단계의 보험이 된다.**

## 3.8 pyOCD — 사용 불가 (실측 확인)

```
$ pyocd list --targets | grep -i realtek
  rtl8195am    Realtek Semiconductor    RTL8195AM    builtin
  rtl8762c     Realtek Semiconductor    RTL8762C     builtin
                                        ← RTL872x 없음

$ pyocd commander -t cortex_m -f 480k -c "show cores"
Setting SWD clock to 480 kHz
0002894 W Invalid coresight component, cidr=0x0 [rom_table]
0002894 E Error while initing target: No cores were discovered! [commander]
```

**원인**: generic `cortex_m` 타겟이 AP0(ROM table이 없는 APB-AP)만 검사하고
KM0/KM4가 있는 AP1/AP2로 넘어가지 못한다. RTL872x용 CMSIS-Pack/DFP도 존재하지 않아
플래시 알고리즘도 없다.

**결론: pyOCD는 버린다. OpenOCD를 쓴다.**

## 3.9 다른 프로브 (참고)

| 프로브 | 상태 |
|---|---|
| **ST-LINK/V2-1 (V2J46M33)** | ✅ **실측 동작** — 우리 환경 |
| ST-LINK V3 | ❌ 펌웨어가 비-ST 타겟 거부 |
| CMSIS-DAP / DAPLink | ✅ Particle이 상용 제품(Photon 2)에서 사용. `interface/cmsis-dap.cfg` + `transport select swd` |
| J-Link (v9+) | ✅ Realtek 공식. SDK `jlink_script/AP2_KM4.JLinkScript` + `JLinkGDBServer -device Cortex-M33 -port 2335` |
| Realtek RLX Probe | 독자 프로브. SDK `jlink_script/rlx_probe0.cfg` |

이식 가능한 참조 설정:
- Particle `device-os/scripts/{rtl872x.tcl, rtl872x_km4_debug.tcl, init_km4.gdb}` — AmebaD와 동일 AP 맵
- Realtek `ameba-rtos/tools/scripts/jlink_script/openocd/amebadplus_openocd.cfg`

## 3.10 SWD 플래싱

OpenOCD에 RTL872x용 flash bank 드라이버가 **없으므로** `flash write_image`는 안 된다.
다만 **RAM 플래시로더 방식으로는 가능**하고, 양 벤더가 실제로 그렇게 한다:

- Realtek: `rtl_gdb_flash_write.txt`가 `flash_loader_ram_1.bin`을 **KM0 SRAM `0x00082000`**에
  올린 뒤 GDB로 구동 (`make flash`)
- Particle `rtl872x.tcl`: 같은 로더를 Tcl 바이트 배열로 내장, `LP_KM4_CTRL 0x4800021C`로 KM4 정지 후
  `rtl872x_flash_read_id / erase / write_bin / verify / wdg_reset` 제공

1단계에서는 **UART 다운로드를 주 경로로 둔다** ([07](08-flash-download.md)).
SRAM 배치 이미지는 GDB `load`로 직접 올릴 수 있어 반복 개발에는 그게 더 빠르다.

> ⚠️ **절대 chip-erase 하지 말 것.** 그리고 플래시 오프셋 0을 함부로 덮지 말 것.
> 벤더 프리부트로더/보안부팅 키가 들어 있을 수 있다. **작업 전 전체 플래시 덤프를 떠 둘 것.**
