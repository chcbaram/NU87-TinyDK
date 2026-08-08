# 08. UART 다운로드

CP2102N 을 통해 플래시에 이미지를 굽는다. 보드에 자동 다운로드 회로가 있어
버튼을 누르지 않아도 된다.

> **상태**: 다운로드 모드 진입과 프로토콜 형태까지 실측으로 확정했다.
> `firm-sdk/tools/flash.py` 구현은 아직이다. 현재 실행 검증은
> `load-sram`(SWD → SRAM)으로 한다. ([09-bsp.md](09-bsp.md) §9.6)

## 8.1 자동 다운로드 회로

**Q1 = MMDT3904** 듀얼 NPN 크로스커플 (ESP32 보드와 동일한 구성):

```
CP2102_DTR ─┬─→ Q1 pin 1
            └─ R10 18K ─→ Q1 pin 5
CP2102_RTS ─┬─→ Q1 pin 4
            └─ R12 18K ─→ Q1 pin 2
Q1 pin 3 (collector A) ─→ NU87_RESET (= CHIP_EN)
Q1 pin 6 (collector B) ─→ PA7 (= UART_DOWNLOAD 부팅 스트랩)
```

- **RTS → CHIP_EN** (리셋)
- **DTR → PA7** (스트랩. 파워온 시 Low 면 UART 다운로드 모드)

### ★ DTR 과 RTS 를 동시에 assert 하면 안 된다

크로스커플 구조라 **둘 다 assert 하면 서로 상쇄되어 아무 일도 일어나지 않는다.**
반드시 한쪽씩만 assert 한다. 실제로 여기서 한 번 막혔다 — 동시 assert 로 시작하는
시퀀스는 리셋조차 걸리지 않아 정상 부팅 로그만 나왔다.

**검증된 진입 시퀀스** (pyserial):
```python
ser.rts = True;  ser.dtr = False; time.sleep(0.5)   # CHIP_EN low  → 리셋
ser.rts = False; ser.dtr = True;  time.sleep(0.3)   # 리셋 해제, PA7 은 Low 유지
ser.rts = False; ser.dtr = False; time.sleep(0.4)   # 스트랩 해제
```

정상 부팅으로 되돌리려면 스트랩 없이 리셋만 건다:
```python
ser.rts = True;  ser.dtr = False; time.sleep(0.4)
ser.rts = False; ser.dtr = False
```

> 콘솔 프로그램이 포트를 열 때 DTR/RTS 를 assert 하면 **보드가 리셋된다.**
> `miniterm --rts 0 --dtr 0` 처럼 명시적으로 끄고 열어야 한다.
> SWD 로 SRAM 에 올린 코드가 이것 때문에 날아간 적이 있다.

## 8.2 진입 확인

성공하면 115200 에서 다음이 나온다:

```
\r#Flash Download Start \n\r
0x15 0x15 0x15 0x15 ...          ← XMODEM NAK 반복 (checksum 모드 수신 대기)
```

정상 부팅일 때는 대신 공장 펌웨어의 `#calibration_ok:[...]` 가 나온다.

**NAK 스트림은 오래가지 않는다.** 실측으로 1~2초 뒤 끊기고 이후 ROM 은 어떤 명령에도
응답하지 않는다. XMODEM 수신 타임아웃이다.
→ **첫 NAK 를 보는 즉시 프레임 전송을 시작해야 한다.** 배너를 다 읽고 여유를 두면 늦는다.

핸드셰이크 반응 실측:

| 보낸 것 | 응답 |
|---|---|
| `BAUDCHK 0x07` | **`ACK 0x06`** |
| `BAUDSET 0x05` | 없음 |
| `NAK 0x15`, `'C'` | 없음 |
| NAK 스트림이 끝난 뒤 `0x33`/`0x21`/`0x31` | 없음 |

## 8.3 프로토콜 — XMODEM-1K + Realtek 확장

`firm-sdk/lib/Realtek/app/xmodem/xmodem_rom.h` 가 정의한다.
구현체는 칩 마스크 ROM 안에 있어 소스가 없다.

**제어 문자**

| 값 | 이름 | 용도 |
|---|---|---|
| `0x01` | `SOH` | 128바이트 프레임 시작 |
| `0x02` | `STX` | **1K 프레임 시작 (XModem-1K)** |
| `0x04` | `EOT` | 전송 종료 |
| `0x06` | `ACK` | |
| `0x15` | `NAK` | |
| `0x18` | `CAN` | 취소 |
| `0x05` | `BAUDSET` | 보레이트 설정 (Realtek 확장) |
| `0x07` | `BAUDCHK` | 보레이트 확인 (Realtek 확장) |
| `0x17` | `XMERASE` | 플래시 소거 |
| `0x19` / `0x20` | `XMREAD` / `XMREADV2` | 플래시 읽기 |
| `0x21` / `0x26` | `RXSTATUS` / `TXSTATUS` | 상태 레지스터 |
| `0x27` | `XM_CHECKSUM` | 쓰기 체크섬 확인 |
| `0x29` / `0x31` | `XM_TXREG` / `XM_RXREG` | REG 또는 RAM 읽고 쓰기 |
| `0x33` | `XM_ROMVER` | ROM 버전 |

**프레임 구조** (`XMODEM_FRAME`, `FRAME_SIZE_1K = 1028`)
```
STX(1) | recordNo(1) | ~recordNo(1) | buffer[1024] | checksum(1)
```
필드 이름은 `CRC` 지만 **1바이트 체크섬**이다 (1028 = 1+1+1+1024+1).
`recordNo` 는 1 부터 시작해 255 에서 0 으로 넘어간다.

핸드셰이크 보레이트는 `HANDSHAKE_BAUD = 115200` 고정이다.
타임아웃 상수: 프레임 대기 1초, 문자 대기 0.5초, 핸드셰이크 2초, 재시도 25회.

## 8.4 RAM 플래시로더

`firm-sdk/lib/Realtek/imgtool/imgtool_flashloader_amebad.bin` (4294 B)

ROM 부트로더는 XMODEM 수신만 하고 유연한 플래시 프로그래밍 로직이 없다.
호스트가 이 작은 프로그램을 **KM0 SRAM `0x00082000`** 에 올리면, 그 프로그램이
확장 명령(`XMERASE`, `XMREAD`, `XM_CHECKSUM` 등)을 받는 서버가 되어 실제
erase/program 을 수행한다.

바이너리 앞부분이 로드 주소를 알려준다 — 표준 32바이트 이미지 헤더가 아니라 진입 테이블이다:
```
21 20 08 00  00 00 00 00
└ 0x00082021 (thumb bit 포함)
```
→ `0x00082000` 에 로드되고 `+0x20` 에서 실행된다.
공장 KM0 IMG1 의 `ram_1` 파트도 같은 주소에 올라간다. KM0 의 부트 RAM 영역이다.

## 8.5 쓰기 주소

| 파일 | 플래시 주소 |
|---|---|
| `firm-sdk/prebuilt/km0_boot_all.bin` | `0x08000000` |
| `firm-sdk/prebuilt/km4_boot_all.bin` | `0x08004000` |
| `build/km0_km4_image2.bin` | `0x08006000` (OTA1) |

평소에는 마지막 하나만 쓰면 된다. 부트로더는 공장 것을 그대로 쓴다.

## 8.6 벽돌이 되지 않는 이유

**UART 다운로드 모드는 칩 마스크 ROM 에 있다.** 플래시 내용과 무관하게 항상 진입할 수 있다.
플래시를 잘못 써도 다시 다운로드 모드로 들어가 덮어쓰면 복구된다.

다만 다음은 주의한다:
- OpenOCD 에는 RTL872x 용 flash bank 드라이버가 없다. **SWD 로는 플래시를 못 쓴다.**
  (RAM 플래시로더를 GDB 로 구동하는 방법은 있다 — Particle `rtl872x.tcl` 참고)
- 작업 전 전체 백업을 떠 둔다: `flash-backup` 태스크 또는
  `openocd -f nu87.cfg -c init -c "nu87_backup ..." -c shutdown`
- `chip erase` 는 하지 않는다.

## 8.7 남은 구현

`firm-sdk/tools/flash.py`:

```
1) 다운로드 모드 진입 (§8.1)
2) 첫 NAK 를 보는 즉시 XMODEM-1K 전송 시작 (§8.2 — 타임아웃이 짧다)
3) BAUDSET/BAUDCHK 로 보레이트 상향 (1500000, 실패 시 921600)
   CP2102N 은 3Mbaud 정격이지만 레이아웃에 따라 다르다
4) imgtool_flashloader_amebad.bin 을 SRAM 0x00082000 에 업로드
5) 플래시로더의 확장 명령으로 erase / program / verify
```

미확정 부분:
- `BAUDSET` 의 인자 형식 (보레이트 서브커맨드 표. 1500000=0x18, 921600=0x14, 115200=0x0C 로 알려져 있다)
- 플래시 쓰기 주소를 지정하는 방식. XMODEM 프레임에는 주소 필드가 없다
- 플래시로더 업로드와 플래시 쓰기의 경계

참고할 공개 구현:
- `Ameba-AIoT/ameba-arduino-d` → `Ameba_misc/Autoflash_patch/` (C++, Realtek 저장소에 편입됨)
- `jojoling/ameba_bw16_autoflash` (위 코드의 원본)
- `tmmsunny012/ameba-arduino-d@feature-platformio-support` → `upload_amebad.py` (순수 Python)
- `Seeed-Studio/ambd_flash_tool` → `tool/macos/amebad_image_tool` (Realtek 바이너리, 크로스체크용)

> `ltchiptool` / LibreTiny 는 AmebaD 를 지원하지 않는다 (`soc/ambd` 모듈 부재).
> `uartfwburn` 은 AmebaPro2(RTL8735B) 전용이라 무관하다.
