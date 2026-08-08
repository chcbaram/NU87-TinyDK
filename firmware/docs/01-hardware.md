# 01. 하드웨어 분석 — NU87-TinyDK

`hardware/NU87-TinyDK.pdf`(Altium 벡터 PDF의 네트 연결 레이어에서 추출한 실제 netlist),
`hardware/NU87_DATASHEET-V1.0.pdf`, `hardware/NU-87TinyDK_StartGuide_EN.pdf` 기준.

> 회로도는 4시트 구성 — 1: 최상위/블록도, 2: `Modules.SchDoc`, 3: `USBtoUART.SchDoc`,
> 4: `Periph_Port.SchDoc`. Variant `NU87-TinyDK-C`.

## 1.1 최초 가정과 다른 점 (중요)

| 최초 가정 | 실제 |
|---|---|
| "USB와 ST-LINK로 SWD가 연결되어 있다" | **보드에 ST-LINK 없음.** 온보드 디버거가 아예 없다 |
| USB = 디버그/CDC | USB = **CP2102N USB↔UART 브리지** (LOGUART에 연결) |
| — | SWD는 **P3 "MP Header"**에 핀으로만 나와 있어 **외부 프로브 필요** |

## 1.2 LED — RGB 3색, Active-High

RGB LED **D8 = TC5050RGBF07-3CJH-AF70** (공통 캐소드, 4/5/6번 핀 → GND).
**U4 = SN74LVC3G17DCUR** (3채널 **비반전** 슈미트 버퍼, VCC=3V3_SYS)를 경유한다.

| 색 | **MCU 핀** | 직렬 0R | 버퍼 | 밸러스트 | D8 핀 |
|---|---|---|---|---|---|
| **Green** | **PA12** | R15 → `LED_G_LV` | U4A (in1 → out7) | R16 470R/1% | 1 |
| **Red** | **PA13** | R18 → `LED_R_LV` | U4B (in3 → out5) | R19 470R/1% | 2 |
| **Blue** | **PA14** | R21 → `LED_B_LV` | U4C (in6 → out2) | R22 470R/1% | 3 |

- **Active-High**: 버퍼가 비반전이고 LED 캐소드가 GND이므로 `1` = 점등.
- 버퍼 전원이 **3V3_SYS**(전류 측정 션트 R13의 *앞단*)라서 LED 전류는 모듈 소비전류 측정에서 제외된다.
- 별도 전원 LED는 없다. UART 활동 LED 2개가 CP2102N에 붙어 있다 — D1(황, `TXT`), D2(주, `RXT`).

`hw_def.h` / `led.c` 매핑 결정:
```
_DEF_LED1 = RED   = PA13
_DEF_LED2 = GREEN = PA12
_DEF_LED3 = BLUE  = PA14
HW_LED_MAX_CH = 3
```

## 1.3 버튼

| SW | 네트 | 풀업 | 직렬 | 기능 |
|---|---|---|---|---|
| **SW1** | **PA7** | R14 10K → VMCU (+ 칩 내부 풀업) | R17 1K → GND | **DOWNLOAD / BOOT**. PA7이 `UART_DOWNLOAD` 스트랩. 파워온 시 Low = UART 다운로드 모드. 1K 직렬이라 LOGUART TX 드라이버와 싸우지 않는다 |
| **SW2** | `NU87_RESET` (= 모듈 `CHIP_EN`) | R20 10K → VMCU, C2 1uF | — | **RESET** (엄밀히는 리셋 핀이 아니라 칩 인에이블) |
| **SW3** | **PA27** | 내부 풀업만 (회로도 주석: *"Use internal pullup to attatch PA27"*) | — | PA27을 Low로 당김 → Realtek **test/debug 모드** |

> ⚠️ **SW3은 디버깅 중 누르지 말 것.** PA27은 SWDIO이면서 동시에 `NORMAL_MODE_SEL`
> 부팅 스트랩이다. 파워온 시점에 Low면 정상 부팅에 실패한다.

## 1.4 USB / 시리얼 — CP2102N

시트 3은 순수한 USB-UART 브리지다.

- **J1 = USB Type-C 리셉터클** `TYPE-C-31-M-12` (12핀, device 전용 — CC1/CC2에 R7/R8 5.1K/1%). USB2.0 D+/D− 만.
- **U2 = USBLC6-2P6** — 커넥터측 ESD 보호.
- **U1 = CP2102N-A02-GQFN24**
  - TXD(21) → `NU87_LOG_RXD` → R3(0R) → 모듈 26번 = **PA8 / LOGUART_RX**
  - RXD(20) ← `NU87_LOG_TXD` ← R2(0R) ← 모듈 27번 = **PA7 / LOGUART_TX**
  - `VIO`(5) = **VMCU** (모듈 레일에 레벨 정합), `REGIN`(7) = VBUS, `RST`(9) R5 1K 풀업
  - VBUS 감지(8): R9 22.1K / R11 47.5K 분압 → `VBUS_SIG`
  - `GPIO.0`(14) = `TXT` → R4 1K → D1(황), `GPIO.1`(13) = `RXT` → R6 1K → D2(주)
  - **R2/R3(0R)을 떼면 PA7/PA8을 범용으로 해방**할 수 있다.

### 자동 다운로드 회로 (ESP32 방식)

**Q1 = MMDT3904-7-F** (듀얼 NPN, SOT-363) 크로스커플:

```
CP2102_DTR (U1 pin 23) ─┬─→ Q1 pin 1
                        └─ R10 18K ─→ Q1 pin 5
CP2102_RTS (U1 pin 19) ─┬─→ Q1 pin 4
                        └─ R12 18K ─→ Q1 pin 2
Q1 pin 3 (collector A) ─→ NU87_RESET (= CHIP_EN), C2 1uF to GND
Q1 pin 6 (collector B) ─→ NU87_LOG_TXD = PA7 = UART_DOWNLOAD 스트랩
```

→ **RTS = CHIP_EN(리셋), DTR = PA7(부트 스트랩)**. esptool과 동일 규약이므로
공개된 AmebaD 플래셔의 DTR/RTS 시퀀스가 그대로 맞는다. 자세한 내용은 [07-flash-download.md](07-flash-download.md).

## 1.5 전원

- 두 소스를 다이오드 OR: **USB VBUS 5V** (FB1 `BLM18KG102SH1D` → D3 `SBR2U60S1F-7`) /
  **EXT VIN 4~14V** (P1 15번, TVS D6 `CDSOD323-T15LC` 15V → D5 `SBR2U60S1F-7`)
- **U3 = AZ1117CR-3.3TRG1** LDO → **3V3_SYS**. D4 `CUS10S30` 역전압 보호.
- **R13 = 0R (0805)** 이 `3V3_SYS → VMCU` 사이의 **전류 측정 션트**.
  시트 1 주석: *"To measure current consumption: 1. Detatch Resistor 2. Attatch current probe"*
- **VMCU**가 모듈 VDD(X1 12번), CP2102N VIO, R14/R20 풀업, P1 12번, P3 1번에 공급. TVS D7 `PTVS5V5D1BLYL`.

## 1.6 커넥터

**P1 — 16핀 2.54mm**

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|GND|PA12|PA13|PA14|PA15|PA27|PA30|PA28|USB D+ (PA26)|USB D− (PA25)|GND|VMCU|3V3_SYS|GND|VIN|NC|

**P2 — 16핀 2.54mm**

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|GND|GND|GND|PB2|PB1|PB3|PB20|PB21|PB18|PB19|PB22|PB23/PB24|RESET (CHIP_EN)|PA8|PA7|GND|

**P3 — 5핀, 실크 "MP Header" = SWD 헤더**

| 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|
|**VMCU**|**PA27 = SWDIO**|**GND**|**PB3 = SWCLK**|**NU87_RESET (CHIP_EN)**|

보드에는 센서/크리스탈/외부 플래시/SD 슬롯이 없다. 능동소자 전체:
X1(모듈), U1 CP2102N, U2 USBLC6, U3 AZ1117, U4 SN74LVC3G17, Q1 MMDT3904,
D1/D2/D8(LED), D3~D7(다이오드/TVS).

## 1.7 모듈측 스트랩 (시트 2)

모듈 = **X1, `NCWB87R01VC`**, 31핀.

- **PA28 / RREF**: **R1 = 12K/1%** → GND, 솔더브리지 **SB1** 경유.
  주석: *"When USB Mode Activated, PA28 must be fixed in external 12K"*,
  *"USB Mode enabled: PA26 = USBD+, PA25 = USBD−, PA28 = 12K pulldown"*
- **PA30**: *"PA30 Has internal 10K Pullup on module"* → 기본 SPS 레귤레이터 모드. P1 7번에만 인출.
- **네이티브 USB는 Type-C에 연결되지 않는다** — P1 9/10번 헤더로만 나온다.

## 1.8 NU87 모듈 (NCWB87R01VC)

NUCODE "NU-87 BLE MCU Module" v1.0, 2026-04. (문서 본문이 "NU-76"이라고 적힌 곳이 여러 군데 있는데 복붙 오류.)

**변종 (p.23 Ordering Information)**
- **NCWB87R01VC** — **칩 안테나**, RTL8720DF ← **TinyDK에 실장된 것**
- NCWB87R01VI — IPEX(u.FL) 커넥터, RTL8720DF

**메모리**: RTL8720DF + **내장 Flash 4MB**, KM4 SRAM 512KB, KM0 SRAM 64KB, Retention SRAM 1KB. **PSRAM 없음**.

**치수**: 10.50 × 16.50 mm. MSL3, HBM Class 2, CDM Class C2.
**전원**: VD1833 = 1.76~3.63V (1.8V 또는 3.3V), 최대 450mA@3.3V / 800mA@1.8V, TA −20~+85℃.
소비전류: Active(KM4 200MHz, RF off) 20mA@3.3V, sleep 75µA, deep-sleep 10µA.

### 31핀 핀아웃 (Table 2-1) — 회로도 X1과 정확히 일치

| # | 이름 | 주요 대체 기능 |
|---|---|---|
|1–5|GND| |
|6|**PA[12]**|LP_UART_TXD, SPI1_MOSI, HS_PWM0, LP_PWM0, I2S_MCLK, ANT_SEL_N, KEY_ROW0|
|7|**PA[13]**|LP_UART_RXD, SPI1_MISO, HS_PWM1, LP_PWM1, I2S_SD_TX1, KEY_ROW1|
|8|**PA[14]**|LP_UART_RTS, SPI1_CLK, I2S_SD_TX2, RTC_OUT, KEY_ROW2|
|9|**PA[15]**|LP_UART_CTS, SPI1_CS, RTC_EXT_32K, KEY_ROW3/COL6|
|10|**PA[27]**|**SWD_DATA**, LP_UART_RTS, WLAN_ACT, **스트랩 NORMAL_MODE_SEL**|
|11|**PA[30]**|HS_SPI_CLK, HS_PWM7, LP_PWM1, **스트랩 SPS_SEL**|
|12|**VD1833**|1.8V / 3.3V 공급|
|13|**PA[28]**|**RREF**, LP_UART_CTS, HS_SPI_CS, BT_CK — USB 사용 시 12kΩ/1% → GND 필수|
|14|**PA[26]**|**USB D+ (HSDP)**, LP_I2C_SDA, HS_SPI_MISO, IR_RX, HS_PWM5|
|15|**PA[25]**|**USB D− (HSDM)**, LP_I2C_SCL, HS_SPI_MOSI, IR_TX, HS_PWM4|
|16|**PB[2]**|**ADC_CH5**, LP_UART_RXD, DMIC_DATA, PCM_CLK|
|17|**PB[1]**|**ADC_CH4**, LP_UART_TXD, DMIC_CLK|
|18|**PB[3]**|**SWD_CLK (기본 기능)**, PCM_SYNC, ADC_CH6|
|19|**PB[20]**|HS_UART_TXD / HS_UART0_CTS, SPI0_CLK, HS_I2C_SCL, SD_CMD, I2S_CLK|
|20|**PB[21]**|HS_UART_RXD / HS_UART0_RTS, SPI0_CS, HS_I2C_SDA, SD_CLK, I2S_WS|
|21|**PB[18]**|HS_UART0_RXD, SPI0_MOSI, SD_D2, **SWD_CLK (대체)**|
|22|**PB[19]**|HS_UART0_TXD, SPI0_MISO, SD_D3, **SWD_DATA (대체)**, I2S_SD_TX0|
|23|**PB[22]**|IR_RX, SD_D0, I2S_SD_RX, LCD_RD, QDEC_PHB|
|24|**PB[23]/PB[24]**|**co-bonded — 동시 사용 불가**|
|25|**CHIP_EN**|1 = enable, 0 = shutdown|
|26|**PA[8]**|**LOGUART_RX**, ANT_SEL_N|
|27|**PA[7]**|**LOGUART_TX**, ANT_SEL_P, **스트랩 UART_DOWNLOAD**|
|28–31|GND| |

**기본 풀 (Table 2-2)**: 내부 풀**업** = PA7, PA8, PA27.
eFuse 제어 = PA13/PA15/PA25/PA28/PB1/PB19/PB22. 모듈에 외부 10kΩ 풀업 실장 = **PA30**.

**LOGUART 주의 (p.10)**: *"LOGUART is used for bootloader communication at **1.5 Mbps**"*,
*"PA[7]과 PA[8]은 기본적으로 LOGUART이며 범용 UART로 쓰는 것을 권장하지 않는다"*.

**UART 인스턴스 (Table 4-1)**
- HS_UART0: TX=PB19 / RX=PB18 / RTS=PB21 / CTS=PB20 (6 Mbps)
- HS_UART: TX=PB20 / RX=PB21 / RTS=PB18 / CTS=PB19
- LP_UART0: TX=PA12 / RX=PA13 / RTS=PA14 / CTS=PA15
- LP_UART1: TX=PA26 or PB1 / RX=PA25 or PB2
- **LOGUART: TX=PA7 / RX=PA8**

**ADC (Table 4-10)**: ADC4=PB1, ADC5=PB2, ADC6=PB3. 12비트 SAR, 0~3.3V, 1 MSPS.
**I2C**: LP_I2C SDA=PA26 / SCL=PA25 (USB와 충돌), HS_I2C SDA=PB21 / SCL=PB20.

## 1.9 이 보드에서의 결론

- **LED 3채널(PA13/PA12/PA14, Active-High)** — 1단계 검증 대상
- **콘솔/플래싱 = LOGUART(PA7/PA8) → CP2102N → `/dev/cu.usbserial-0001`** (단일 포트로 둘 다)
- **SWD = P3 헤더(PA27/PB3) + 외부 프로브** — 실제 동작 검증 완료, [03-debug-swd.md](03-debug-swd.md)
- 1단계에서 쓸 수 있는 GPIO: SW1(PA7 — LOGUART와 공유이므로 주의), P1/P2 헤더의 나머지 핀
