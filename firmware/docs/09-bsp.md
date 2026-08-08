# 09. BSP 계층 — 진입점 · 링커스크립트 · 시간 기반

KM4 이미지가 부팅해서 `main()` 까지 도달하고, `millis()` 로 LED 를 깜빡이기까지의 구현.

관련 파일:
```
src/bsp/ldscript/nu87_km4_img2.ld     이미지 배치
src/bsp/device/nu87_app_start.c       진입점 · fault 핸들러 · MPU
src/bsp/device/platform_autoconf.h    SDK 빌드 설정
src/bsp/device/platform_opts.h        SDK 기능 스위치
src/bsp/device/autoconf.h             WLAN 설정 stub
src/bsp/bsp.c / bsp.h                 클럭 · SysTick · delay/millis/micros
firm-sdk/tools/make_image.py          이미지 생성
```

## 9.1 이미지 배치 — 전량 SRAM

코드·상수·데이터를 전부 `BD_RAM_NS`(`0x10005000`, 476KB)에 배치한다.
SDK 원본은 `.text`/`.rodata` 를 `.xip_image2.text`(플래시 XIP 가상주소 `0x0E000000`)에 두지만
쓰지 않는다.

| 이점 | 내용 |
|---|---|
| 이미지가 1파트 | 32바이트 헤더 하나만 붙인다. 원본은 xip/ram/psram 3파트 |
| 플래시 쓰기 제약 없음 | XIP 면 쓰기 루틴과 그 경로의 인터럽트 핸들러를 SRAM 에 따로 둬야 한다 |
| 소프트웨어 브레이크포인트 | XIP 코드에는 걸 수 없고 하드웨어 BP 는 코어당 2개뿐이다 |
| GDB `load` 로 즉시 실행 | 플래시를 건드리지 않고 반복 개발이 가능하다 |

실제 사용량은 **38 KB / 476 KB (7.9%)** 이다.

> **SRAM 배치는 실행 위치를 말하는 것이지 저장이 휘발성이라는 뜻이 아니다.**
> 이미지는 플래시에 저장되고, 이미지 헤더의 로드 주소(`0x10005000`)를 보고
> 부트로더가 **매 부팅마다 SRAM 으로 복사**한다. 공장 이미지의 KM4 `ram` 파트도 같다.
> SRAM 배치의 대가는 지속성이 아니라 크기 제한(476KB)이다.

WiFi + lwIP + mbedTLS 가 들어오면 SRAM 에 담기지 않으므로 XIP 배치로 전환해야 한다.

### 이미지 앞부분 레이아웃

부트로더가 읽는 구조체와 펌웨어 정보를 고정 오프셋에 둔다.

| 오프셋 | 크기 | 내용 | 심볼 |
|---|---|---|---|
| +0 | 12 | `Img2EntryFun0` = `{ RamStartFun, RamWakeupFun, VectorNS }` | `__image2_entry_func__` |
| +12 | 20 | `RAM_IMG2_VALID_PATTEN` = `"RTKWin\0\xff…"` | `__image2_validate_code__` |
| **+32** | **72** | **`firm_ver_t`** (magic `"VER "` + version + name + addr) | `__image2_version__` |

`firm_ver_t` 를 +32 에 **고정**하는 이유는 부트로더나 호스트 툴이 **펌웨어를 실행하지 않고**
버전을 읽을 수 있어야 하기 때문이다. OTA 슬롯 검증, 다운그레이드 방지, 업데이트 전 비교에 쓴다.

주소 계산에 의존하지 않고 링커스크립트에서 명시적으로 맞춘다:
```ld
. = __ram_image2_text_start__ + 32;
__image2_version__ = .;
KEEP(*(.version))
```
앞쪽 구조체 크기가 바뀌어도 오프셋이 유지된다.

> OTA1/OTA2 는 Flash MMU 가 같은 가상주소로 매핑하므로 **절대번지가 아니라 이미지 상대
> 오프셋**이어야 한다. 플래시에서의 위치는
> `OTA 슬롯 시작 + km0_image2 크기 + 32(이미지 헤더) + 32` 다.

빌드 결과 확인:
```
$ arm-none-eabi-objdump -s -j .ram_image2.entry build/nu87-fw.elf
 10005000 2d590010 00000000 00100010 52544b57  -Y..........RTKW
 10005010 696e00ff 00010100 95810101 00000000  in..............
 10005020 20524556 56323630 38303852 31000000   REVV260808R1...
          └ "VER "  └ "V260808R1"
```

### `.module` 섹션 — 모듈 레지스트리

`ap` 계층의 `MODULE_DEF(x)` 가 `module_t` 를 `.module` 섹션에 넣고,
`moduleInit()` 이 링커 심볼로 개수를 계산해 순회한다:

```c
info.count    = ((int)&_emodule - (int)&_smodule) / sizeof(module_t);
info.p_module = (module_t *)&_smodule;
```

`.ram_image2.data` 안에 배치한다:
```ld
. = ALIGN(8);
_smodule = .;
KEEP(*(.module))
KEEP(*(.module*))
_emodule = .;
```

이 섹션이 없으면 모듈이 하나도 등록되지 않는다. 검증:
```
$ arm-none-eabi-nm build/nu87-fw.elf | grep -E "_smodule|_emodule|module_"
1000b090 D _emodule      # 104 B = module_t(52) × 2
1000b028 D _smodule
1000b028 d module_ap
1000b05c d module_cli
```
콘솔에도 `moduleInit() count : 2` 로 나온다.

## 9.2 진입점

KM4 부트로더가 `.image2.entry.data` 의 `Img2EntryFun0.RamStartFun` 으로 점프한다.
진입 시점의 상태:

- MSP 가 `MSP_RAM_NS`(`0x10004000`~`0x10005000`, 4K) 로 설정되어 있다
- `.ram_image2.{entry,text,data}` 가 SRAM 에 로드되어 있다
- **BSS 는 아직 0 이 아니다** — 전역 변수를 신뢰할 수 없다
- **`PRIMASK = 1`** — 모든 인터럽트가 마스킹되어 있다

`nu87AppStart()` 순서:

```c
irq_table_init(MSP_RAM_HP_NS);   /* 비보안 벡터테이블. 이후 InterruptRegister() 동작 */
nu87VectorTableOverride();       /* fault 핸들러 4개 설치 */
_memset(__bss_start__, 0, __bss_end__ - __bss_start__);
Cache_Enable(ENABLE);
SystemSetCpuClk(CPU_CLOCK_SEL_VALUE);   /* 0 = 200MHz */
__libc_init_array();             /* C++ 전역 생성자 / __init_array */
/* SP 를 8바이트 정렬 — AAPCS 요구사항, 가변인자 함수가 전제한다 */
mpu_init();
nu87MpuNoCacheInit();
main();
```

SDK 원본(`rtl8721dhp_app_start.c`)에서 뺀 것과 이유:

| 제거 | 이유 |
|---|---|
| PSRAM 초기화/로드 | RTL8720DF 에 PSRAM 이 없다. `psram_reserve.h` 가 `osdep_service.h` 를 끌어온다 |
| FreeRTOS 벡터 3개 | bare-metal 구성 |
| `os_heap_init()` | newlib `_sbrk` 를 쓴다 (링커스크립트의 `end` 심볼) |
| TrustZone `BOOT_IMG3()` | 미사용 |
| `cm_backtrace_init()` | 별도 라이브러리 의존 |
| `SOCPS_InitSYSIRQ_HP()` | 구현이 `lib_pmc_hp.a`(가져오지 않은 prebuilt)에만 있다. 전력관리용 |
| `app_vdd1833_detect()` | 구현이 prebuilt 에만 있다. 보드가 3.3V 고정 |
| `app_driver_call_os_func_init()`, `memcpy_gdma_init()` | WiFi 드라이버용 |

**`irq_table_init()` 은 유지한다.** 동적 IRQ 등록(`InterruptRegister`, `__NVIC_SetVector`)이
여기서 활성화되고, SysTick 벡터 설치와 UART RX 인터럽트에 바로 필요하다.

### Fault 핸들러

ROM 기본 핸들러는 정보 없이 멈춘다. 스택 프레임에서 레지스터를 꺼내 LOGUART 로 덤프한다.

```
[ HardFault ]
  R0  0x00000000   R1  0x00000000
  R2  0x101D0F10   R3  0x0BEBC200
  R12 0xF007E009   LR  0x100052A7
  PC  0x1000AEB4   PSR 0x41000000
  CFSR 0x00010000  HFSR 0x40000000
  MMFAR 0x00000000 BFAR 0x00000000
```

디버거가 붙어 있으면 `__BKPT(0)` 로 멈춘다. **이 핸들러가 아래 세 버그를 모두 잡아냈다.**

## 9.3 시간 기반

SysTick 1kHz 하나로 만든다.

| API | 구현 |
|---|---|
| `millis()` | SysTick 인터럽트가 올리는 `bsp_tick_ms` |
| `micros()` | `ms × 1000 + (SysTick->LOAD - VAL) / (cpu_hz / 1MHz)`. ms 와 VAL 을 두 번 읽어 틱 경계 불일치를 막는다 |
| `delayUs()` | ROM `DelayUs()` (캘리브레이션된 busy-wait) |
| `delay()` | `millis()` 델타 루프. 대기 중 `cliLoopIdle()` 로 CLI 를 계속 돌린다 |

같은 SysTick 인터럽트로 `swtimerISR()`(1kHz)도 구동한다.

```c
__NVIC_SetVector(SysTick_IRQn, (uint32_t)bspSysTickHandler);
NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
SysTick->LOAD = (bsp_cpu_hz / 1000U) - 1U;
SysTick->VAL  = 0U;
SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
__enable_irq();
```

> CMSIS 의 `SysTick_Config()` 는 `__Vendor_SysTickConfig` 설정 때문에 제공되지 않는다.
> 레지스터를 직접 쓴다.

`delay()` 가 `cliLoopIdle()` → `moduleUpdate()` 를 부르므로 **모듈 그래프가 재진입한다.**
`delay()` 를 호출하는 모듈 update 는 자신이 다시 불릴 수 있음을 전제해야 한다.

## 9.4 첫 부팅까지 막혔던 세 가지

전부 fault 핸들러 덤프와 SWD 레지스터 판독으로 원인을 특정했다.
같은 계열 칩을 올릴 때 반복될 가능성이 높으므로 남긴다.

### ① `.init` / `.fini` 섹션 누락 → `__libc_init_array` 에서 UNDEFINSTR

**증상**
```
[ HardFault ]  PC 0x1000AEB4  LR 0x100052A7
CFSR 0x00010000   ← bit16 UNDEFINSTR
```
`0x1000AEB4` 는 `.ram_image2.data` 안이었다. **데이터를 실행**하려 한 것이다.

`LR` 을 역어셈블하니 호출 지점이 나왔다:
```
10005290 <__libc_init_array>:
100052a2:	f005 fdcd 	bl	1000ae40 <_init>
100052a6:	4b0b      	ldr	r3, [pc, #44]     ← LR = 0x100052A7
```

**원인** — `_init`/`_fini` 는 GCC 의 `crti.o`/`crtn.o` 가 `.init`/`.fini` 섹션으로 제공한다.
링커스크립트에 그 섹션 배치가 없어 orphan 으로 흩어졌고, 두 파일의 조각이 이어지지 않아
`_init` 이 깨진 코드가 되었다. (`_init` 이 4바이트뿐이었다)

**해결**
```ld
. = ALIGN(4);
KEEP(*(.init))
KEEP(*(.fini))
```
고친 뒤 `_init` 이 12바이트 `push`/`pop`/`bx lr` 로 온전해졌다.

> SDK 원본 `rlx8721d_img2_is.ld` 에도 이 섹션이 없다. 벤더 빌드에서는 orphan 배치가
> 우연히 맞아떨어졌던 것으로 보인다. 명시적으로 두는 것이 맞다.

### ② 진입 시 `PRIMASK = 1` → 인터럽트가 전혀 안 걸림

**증상** — 부팅 로그와 CLI 는 정상인데 `bsp_tick_ms` 가 계속 0. LED 도 안 깜빡인다.

SWD 로 확인:
```
SysTick CTRL 0x00010007   ← ENABLE·TICKINT·CLKSOURCE 다 켜져 있고 COUNTFLAG 도 섰다
SysTick LOAD 0x00030D3F   ← 199,999 (200MHz/1000-1) 정확
ICSR         0x0440F000   ← VECTPENDING = 15 (SysTick 대기 중)
PRIMASK      0x01         ← 원인
VTOR         0x10001000
VTOR[15]     0x10005755   ← bspSysTickHandler+1, 벡터 등록은 정상
```

**원인** — KM4 부트로더가 `PRIMASK=1` 상태로 이미지에 진입시킨다.
SDK 는 `vTaskStartScheduler()` 가 이를 풀어 주는데, bare-metal 에는 그 단계가 없다.

**해결** — `bspInit()` 의 SysTick 설정 뒤에 `__enable_irq()`.

### ③ `SYSTIMER_Init()` → IMPRECISERR (클럭 없는 주변장치 접근)

**증상** — `__enable_irq()` 를 넣자 곧바로 fault.
```
CFSR 0x00000400   ← bit10 IMPRECISERR (비동기 버스 폴트)
```
레지스터가 전부 `0xADBEEFDE`(스택 채움 패턴) 였다.

**원인** — `SYSTIMER_Init()` 이 TIMM05 타이머 주변장치를 건드리는데 그 클럭이 켜져 있지 않다.
비동기 버스 폴트는 나중에 표면화되므로 `cpsie i` 시점에 터졌다.

**해결** — 제거했다. `millis()`/`micros()` 를 SysTick 으로 만들므로 애초에 필요하지 않다.
`SYSTIMER_TickGet()` 을 쓰려면 먼저 해당 클럭을 켜야 한다.

## 9.5 이미지 생성

`firm-sdk/tools/make_image.py` (Python — 벤더 `prepend_header.sh` + 플랫폼별 `checksum`
바이너리를 쓰지 않는다).

```
objcopy -j .ram_image2.entry -j .ram_image2.text -j .ARM.extab -j .ARM.exidx
        -j .ram_image2.data -O binary  →  ram_2.bin
32바이트 헤더 부착 (sig "81958711" + len + load addr)
cat prebuilt/km0_image2_all.bin + km4_image2_all.bin  →  km0_km4_image2.bin
```

로드 주소는 `.map` 의 `__ram_image2_text_start__` 에서 읽는다.
**32바이트 헤더는 플래시에만 있고 로드 주소에는 더하지 않는다** — SRAM 목적지가
그대로 `0x10005000` 이어야 한다. (공장 이미지 실측값도 동일)

빌드 출력:
```
BD_RAM_NS:       38336 B       476 KB      7.87%
펌웨어      NU87-TINYDK  V260808R1   (페이로드 +32, addr 0x10005000)
로드 주소   0x10005000  (__ram_image2_text_start__)
ram_2.bin     24736 B
km4_image2    24768 B
km0(스톡)    110592 B
최종 이미지  135360 B  -> km0_km4_image2.bin  (flash 0x08006000)
```

## 9.6 SRAM 로드로 반복 개발

플래시를 건드리지 않고 SWD 로 SRAM 에 올려 바로 실행한다. 가장 빠른 반복 경로다.

```bash
openocd -f ../firm-sdk/tools/openocd/nu87.cfg -c init \
        -c "targets rtl872xd.km4" -c halt \
        -c "load_image build/nu87-fw.elf" \
        -c "reg msp 0x10005000" \
        -c "nu87_run_image" -c resume -c shutdown
```
VS Code 에서는 `load-sram` 태스크 또는 `Load to SRAM & Run KM4` 디버그 구성.

`nu87_run_image` 는 `0x10005000`(= `Img2EntryFun0.RamStartFun`)을 읽어 PC 에 넣는다.
MSP 는 `MSP_RAM_NS` 상단인 `0x10005000` 으로 맞춘다.

> **주의: 시리얼 포트를 먼저 열어야 한다.**
> CP2102N 의 DTR/RTS 자동 다운로드 회로가 포트를 열 때 보드를 리셋시켜
> SRAM 에 올린 코드를 날린다. 콘솔을 먼저 띄우고 그 뒤에 로드할 것.

## 9.7 검증

```
[ Firmware Begin... ]
Booting..Name 		: NU87-TINYDK
Booting..Ver  		: V260808R1
Booting..Clock		: 200 Mhz
Booting..Reason		: 0x0

[  ] moduleInit()
       count : 2
[  ] moduleBegin()
cli#        cli OK
```

| 항목 | 방법 | 결과 |
|---|---|---|
| SysTick 1kHz | `bsp_tick_ms` 를 2.5s / 5.5s 시점에 읽는다 | `2508` → `5514` (3006ms 증가) |
| 인터럽트 | `reg primask` | `0x00` |
| LED 토글 | `mdw 0x48014000` 를 500ms 간격으로 | `0x2000` → `0x0` → `0x2000` → `0x0` (bit13 = PA13 = RED) |
| 모듈 등록 | 콘솔 + `nm` | `count : 2`, `_smodule`~`_emodule` 104 B |
| 펌웨어 정보 | 이미지 페이로드 +32 | `"VER "` + `NU87-TINYDK` + `V260808R1` |
| CLI | `help` | `HELP MD LOG MODULE` |
