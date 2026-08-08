# 10. LED · GPIO · RESET 드라이버

`src/hw/driver/` 의 세 드라이버. 셋 다 벤더 SDK 심볼을 이 파일들 안에만 두고,
`src/common/hw/include/` 의 포터블 API 시그니처는 그대로 유지한다.

## 10.1 공통 — ROM GPIO API

GPIO 조작 함수는 전부 칩 마스크 ROM 에 있다. `rlx8721d_rom_symbol_acut.ld` 가
주소를 준다.

```c
void GPIO_Init(GPIO_InitTypeDef *GPIO_InitStruct);
void GPIO_WriteBit(u32 GPIO_Pin, u32 BitVal);
u32  GPIO_ReadDataBit(u32 GPIO_Pin);
```

- `GPIO_Init()` 이 내부에서 `Pinmux_Config(PINMUX_FUNCTION_GPIO)` 와
  `PAD_PullCtrl()` 까지 수행한다. 따로 부를 필요가 없다.
- **토글 API 가 없다.** 읽어서 반전한다.
- 핀은 `_PA_13` 처럼 포트·번호가 한 바이트에 들어간 값이다 (`_PA_x` = `0x00+x`,
  `_PB_x` = `0x20+x`). STM32 처럼 포트 포인터와 핀 마스크로 나뉘지 않는다.

## 10.2 LED — `led.c`

```
PA13 --0R--> U4B --470R--> D8 pin2   RED     _DEF_LED1
PA12 --0R--> U4A --470R--> D8 pin1   GREEN   _DEF_LED2
PA14 --0R--> U4C --470R--> D8 pin3   BLUE    _DEF_LED3
```

U4 = SN74LVC3G17 비반전 슈미트 버퍼, D8 은 공통 캐소드다. 따라서 **Active-High**.

```c
static const led_tbl_t led_tbl[LED_MAX_CH] =
{
  {_PA_13, _DEF_HIGH},   // RED
  {_PA_12, _DEF_HIGH},   // GREEN
  {_PA_14, _DEF_HIGH},   // BLUE
};
```

`_DEF_LED1` 이 RED 인 것은 `ap.c` 의 `updateLED()` 가 1번 채널을 쓰기 때문이다.

## 10.3 GPIO — `gpio.c`

보드에 고정 기능이 배정된 GPIO 가 없어서 **P1/P2 확장 헤더로 나온 핀**을 채널로 연다.
채널 이름이 실크와 대응된다 (`P2_9_PB18` = P2 헤더 9번 = PB18).

| ch | 이름 | 비고 |
|---|---|---|
| 0 | `P1_5_PA15` | eFuse 로 기능이 정해지는 핀 |
| 1 | `P2_4_PB2` | ADC5 |
| 2 | `P2_5_PB1` | ADC4 |
| 3 | `P2_7_PB20` | HS_I2C SCL / HS_UART TX |
| 4 | `P2_8_PB21` | HS_I2C SDA / HS_UART RX |
| 5 | `P2_9_PB18` | HS_UART0 RX |
| 6 | `P2_10_PB19` | HS_UART0 TX |
| 7 | `P2_11_PB22` | |
| 8 | `P2_12_PB23` | 실크가 "PB23/PB24" 로 모호하다 |

### 채널에 넣지 않은 헤더 핀

| 핀 | 이유 |
|---|---|
| PA12 / PA13 / PA14 | RGB LED |
| PA27 / PB3 | SWDIO / SWCLK |
| PA25 / PA26 | 네이티브 USB D− / D+ |
| PA28 | RREF. 외부 12K 고정이라 구동하면 안 된다 |
| PA30 | 모듈 레귤레이터 모드 스트랩 (내부 10K 풀업) |
| PA7 / PA8 | LOGUART. PA7 은 `UART_DOWNLOAD` 스트랩이기도 하다 |

### 버튼을 채널로 열지 않은 이유

보드의 두 버튼은 **둘 다 다른 기능과 공유**한다.

- **SW1 = PA7** — LOGUART TX 이자 `UART_DOWNLOAD` 부팅 스트랩.
  콘솔을 쓰는 동안 입력으로 읽을 수 없다.
- **SW3 = PA27** — SWDIO 이자 `NORMAL_MODE_SEL` 스트랩.
  파워온 시 Low 면 부팅에 실패한다.

즉 이 보드에 **자유롭게 쓸 수 있는 버튼은 없다.** 버튼 입력이 필요하면 헤더 핀에
외부 스위치를 단다.

### 실측 — 내부 풀업이 듣지 않는 핀

아무것도 꽂지 않고 입력 + 내부 풀업으로 읽은 결과:

```
ch0 P1_5_PA15      0     ← 풀업이 듣지 않는다
ch2 P2_5_PB1       1
ch3 P2_7_PB20      1
ch4 P2_8_PB21      1
ch5 P2_9_PB18      1
ch6 P2_10_PB19     1
ch7 P2_11_PB22     1
ch8 P2_12_PB23     0     ← 풀업이 듣지 않는다
```

PA15 와 PB23 은 **출력으로 쓰면 1/0 이 정상적으로 나간다.** 핀 자체는 멀쩡하고
내부 풀업만 듣지 않는 것이다. 이 두 핀을 입력으로 쓸 때는 외부 풀업을 단다.

### CLI

```
gpio info                          현재 상태
gpio show                          갱신하며 표시
gpio mode ch[0~8] in:in_pu:in_pd:out
gpio read ch[0~8]
gpio write ch[0~8] 0:1
```

## 10.4 RESET — `reset.c`

### ★ `NVIC_SystemReset()` 을 쓰면 안 된다

**KM4 코어만 리셋되고 KM0 는 계속 돈다.** KM4 이미지는 KM0 부트로더가 SRAM 으로
복사해 넘겨주는 구조라, 코어만 리셋하면 다시 올라오지 못하고 멈춘다.
실제로 그렇게 만들어 보드가 무응답이 되는 것을 확인했다 (CHIP_EN 리셋으로 복구된다).

SDK 의 `ota_platform_reset()` 도 같은 이유로 `NVIC_SystemReset()` 을 **주석 처리**하고
워치독으로 SoC 전체를 리셋한다. 우리도 그 방식을 따른다:

```c
BKUP_Set(BKUP_REG0, BIT_KM4SYS_RESET_HAPPEN);   // 사유를 직접 표시

WDG_Scalar(50, &count_process, &div_fac_process);
wdg_init.CountProcess  = count_process;
wdg_init.DivFacProcess = div_fac_process;
WDG_Init(&wdg_init);
WDG_Cmd(ENABLE);

while (1) { }                                    // 50ms 뒤 물린다
```

워치독 리셋은 사유 비트를 세워 주지 않으므로 소프트웨어가 직접 표시한다.
부트로더는 SYS 와 WDG 가 함께 서 있으면 WDG 를 지운다 — 그래서 소프트 리셋으로 읽힌다.

### 리셋 사유 — `BOOT_Reason()` 은 시프트된 값을 준다

부트로더 `boot_flash_hp.c`:

```c
tmp_reason = BACKUP_REG->DWORD[0] & BIT_MASK_BOOT_REASON;   // 하위 5비트
tmp_reason = tmp_reason << BIT_BOOT_REASON_SHIFT;
```

위쪽에 딥슬립/BOD 비트를 따로 OR 해서 넘긴다.

> **이 SDK 스냅샷에는 `BIT_BOOT_*` 와 `BIT_BOOT_REASON_SHIFT` 정의가 빠져 있다.**
> 부트로더 소스는 쓰는데 헤더에 없다. 실측으로 확정했다:
> `BKUP_Set(BKUP_REG0, BIT_KM4SYS_RESET_HAPPEN)` = `BIT(3)` 후 리셋하면
> `BOOT_Reason()` 이 `0x00080000` = `BIT(19)` = `BIT(3) << 16` 을 준다.
> → **시프트는 16.**

되돌린 뒤의 비트는 백업 레지스터 정의와 같다:

| 비트 | 의미 | 매핑 |
|---|---|---|
| `BIT_SYS_RESET_HAPPEN`(0) | KM0 시스템 리셋 | `RESET_BIT_SOFT` |
| `BIT_WDG_RESET_HAPPEN`(1) | KM0 워치독 | `RESET_BIT_WDG` |
| `BIT_KM4SYS_RESET_HAPPEN`(3) | KM4 시스템 리셋 | `RESET_BIT_SOFT` |
| `BIT_KM4WDG_RESET_HAPPEN`(4) | KM4 워치독 | `RESET_BIT_WDG` |
| 전부 0 | 파워온 | `RESET_BIT_POWER` |
| 시프트 영역 밖 | 딥슬립 복귀 / BOD | `RESET_BIT_ETC` |

이 칩에는 **핀 리셋을 알리는 플래그가 없다.** CHIP_EN 을 당기면 파워온과 같은 경로를
타므로 `RESET_BIT_PIN` 은 세우지 않는다.

### 부팅 모드 — 백업 레지스터 1

```c
#define BKUP_IDX_BOOT_MODE    BKUP_REG1
```

`BKUP_REG1`~`REG5` 가 사용자 몫이다 (`REG0`/`REG6`/`REG7` 은 시스템이 쓴다).
이 레지스터는 **CPU/시스템 리셋으로는 지워지지 않고 파워오프와 딥슬립에서만 지워진다.**
"리셋해서 업데이트 모드로 올라오기" 에 필요한 성질이다.

`resetInit()` 이 읽은 직후 0 으로 지운다. 모드는 한 번만 소비되어야 한다.

> ROM 부트로더도 같은 방식으로 백업 레지스터 **0** 의 `BIT_UARTBURN_BOOT`(BIT9)를 보고
> UART 다운로드 모드로 들어간다. 우리 부팅 모드와는 별개의 레지스터다.
> 향후 "CLI 에서 명령 한 줄로 다운로드 모드 진입" 을 만들 때 이 비트를 쓴다.

### 실측 확인

```
1) 하드웨어 리셋 (CHIP_EN)
   Booting..Reason : 0x0
   [OK] resetInit()
        RESET_BIT_POWER

2) 소프트 리셋 (reset reset)
   Booting..Reason : 0x80000
   [OK] resetInit()
        RESET_BIT_SOFT

3) reset update -> 재부팅
   Booting..Reason : 0x80000
   [OK] resetInit()
        RESET_BIT_SOFT
        MODE_BIT_UPDATE      ← 리셋을 넘어 유지된다
```

### CLI

```
reset info      사유 · 부팅 모드
reset boot      MODE_BIT_BOOT 로 재부팅
reset update    MODE_BIT_UPDATE 로 재부팅
reset reset     그냥 재부팅
```

## 10.5 아직 안 한 것

`rtc.c` 는 빌드에서 제외되어 있다 (`CMakeLists.txt` 의 `EXCLUDE_PATHS`).
원본 STM32 판은 RTC 백업 레지스터를 `reset.c` 의 저장소로 썼는데, 이 칩에서는
백업 레지스터가 RTC 와 무관한 별도 블록이라 `reset.c` 가 RTC 없이 성립한다.
시각이 필요해질 때 작성한다.
