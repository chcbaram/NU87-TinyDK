# 11. UART · LOG · CLI

콘솔 경로. `log.c` / `cli.c` / `cli_gui.c` 는 MCU 에 의존하지 않으므로 손대지 않았고,
**바뀐 것은 `hw/driver/uart.c` 하나**다.

```
cliMain()  ──  cli.c        포터블
logPrintf() ──  log.c        포터블 (부팅 버퍼 · 링버퍼)
                  ↓ uartWrite / uartRead / uartAvailable
                uart.c       ← RTL8720DF 전용
                  ↓ ROM
                LOGUART      PA7 TX / PA8 RX -> CP2102N -> USB-C
```

## 11.1 LOGUART 는 범용 UART 가 아니다

RTL8720DF 에는 범용 UART 4개(`UART_DEV_TABLE[4]`)와 **별개로 LOGUART 라는 전용 IP** 가
있다. API 도 IRQ 도 다르다.

```c
_LONG_CALL_ u8   LOGUART_GetChar(BOOL PullMode);
_LONG_CALL_ u8   LOGUART_Readable(void);
_LONG_CALL_ void LOGUART_PutChar(u8 c);
_LONG_CALL_ void LOGUART_WaitBusy(void);
```

전부 마스크 ROM 에 있다. IRQ 는 `UART_LOG_IRQ = 3`.

**PA7/PA8 은 핀맵에 `UART_LOG_TXD` / `UART_LOG_RXD` 로 고정**되어 있어 범용 UART 로
뮤싱할 수 없다.

### 콘솔을 범용 UART 로 옮길 수 없는 이유

| UART | 핀 | 상태 |
|---|---|---|
| LP_UART0 | PA12/13/14/15 | ❌ RGB LED 3개와 충돌 |
| LP_UART1 | PA25/PA26 또는 PB1/PB2 | ❌ PA25/26 은 USB D± |
| HS_UART0 | PB18/PB19 (6Mbps) | ⭕ P2 헤더에 인출 — 유일하게 현실적 |

HS_UART0 로 옮겨도 **CP2102N 에는 연결되지 않는다.** 보드의 USB-시리얼은 PA7/PA8 에
0R(R2/R3)로 묶여 있다. 게다가 PA7 은 다운로드 스트랩이라 플래싱은 어차피 PA7/PA8 을
계속 쓴다 → 케이블이 두 개가 된다.

→ 콘솔은 LOGUART 에 두는 것이 맞다. 범용 UART 가 필요해지면 HS_UART0 를
`_DEF_UART2` 로 **추가**한다 (교체가 아니라 채널 추가).

## 11.2 보레이트를 설정하지 않는다

```c
bool uartOpen(uint8_t ch, uint32_t baud)
{
  ...
  /* KM0 부트로더가 LOGUART 를 115200 8N1 로 이미 설정해 둔다.
   * 플래싱 툴과 콘솔이 같은 포트를 공유하므로 보레이트를 바꾸지 않는다. */
  uart_tbl[ch].is_open = true;
  return true;
}
```

`baud` 인자는 기록만 하고 하드웨어에 반영하지 않는다. 포터블 API 시그니처를
유지하면서도 플래싱 경로와 충돌하지 않게 하기 위해서다.

## 11.3 폴링 방식

송수신 모두 ROM 함수를 직접 부르는 폴링이다. 인터럽트도 링버퍼도 쓰지 않는다.

LOGUART 에 하드웨어 FIFO 가 있고 `cliMain()` 이 `moduleUpdate()` 마다 호출되므로
115200 타이핑 속도에는 충분하다.

**한계**: 긴 텍스트를 붙여넣으면 FIFO 를 넘겨 문자가 떨어질 수 있다.
그때는 `InterruptRegister(UART_LOG_IRQ)` + `qbuffer` 로 바꾼다.
`irq_table_init()` 을 `nu87_app_start.c` 에 남겨 뒀으므로 동적 IRQ 등록이 이미 가능하다.

바꿀 때 주의할 점:

- **TX 는 폴링을 유지한다.** fault handler 가 `DiagPrintf` 로 레지스터를 덤프하는데
  그 시점에는 인터럽트를 쓸 수 없다.
- KM0 도 LOGUART 를 쓴다 (`#calibration_ok` 가 KM0 부트로더 출력). RX 인터럽트
  소유권이 겹치지 않는지 확인이 필요하다.

`uart_driver_t` vtable 을 유지했으므로 **교체 범위가 `uart.c` 안에 머문다.**

## 11.4 `uartFlush()` 의 타임아웃

수신 버퍼를 비우는 루프에 시간 제한이 있다.

```c
uint32_t pre_time = millis();

while (LOGUART_Readable())
{
  LOGUART_GetChar(_FALSE);

  if (millis() - pre_time >= UART_FLUSH_TIMEOUT_MS)
  {
    return false;
  }
}
```

상대가 끊임없이 보내면 `LOGUART_Readable()` 이 내려가지 않아 빠져나오지 못한다.
`UART_FLUSH_TIMEOUT_MS` 기본값은 100ms 이고 `common/hw/include/uart.h` 에서 재정의할 수 있다.

## 11.5 로그 · CLI

`log.c` 와 `cli.c` 는 그대로다. 백엔드가 `uartWrite()` 뿐이라 MCU 가 바뀌어도
영향이 없다.

STM32 판에서 뺀 것은 **텔넷 `cli_net`** 하나다. 무선이 올라오면 되살린다.

부팅 로그는 `logBoot(false)` 로 버퍼에 모았다가 한 번에 내보낸다:

```
[ Firmware Begin... ]
Booting..Name 		: NU87-TINYDK
Booting..Ver  		: V260808R1
Booting..Clock		: 200 Mhz
Booting..Date 		: Aug  8 2026
Booting..Time 		: 23:42:28
Booting..Reason		: 0x0

[OK] resetInit()
     RESET_BIT_POWER
[  ] moduleInit()
       count : 2
[  ] moduleBegin()

cli#        cli OK
```

`hw_def.h` 의 CLI 스위치:

```c
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_ASSERT          1
#define _USE_CLI_HW_UART            1
```

## 11.6 콘솔 열기

포트를 열 때 **DTR/RTS 를 반드시 꺼야 한다.** 자동 다운로드 회로 때문에 보드가
리셋된다 ([08-flash-download.md](08-flash-download.md) §8.1).

```bash
pyserial-miniterm --rts 0 --dtr 0 /dev/cu.usbserial-0001 115200
```

VS Code 에서는 `console - monitor` / `console - monitor · 자동` 태스크를 쓴다.
