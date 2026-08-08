# 08. UART 다운로드

CP2102N 을 통해 플래시에 이미지를 굽는다. 보드에 자동 다운로드 회로가 있어
버튼을 누르지 않아도 된다.

> **상태**: `firm-sdk/tools/flash.py` 로 소거 · 프로그램 · 체크섬 검증까지 동작한다.
> 전원을 껐다 켜도 유지된다. SWD → SRAM 로딩(`load-sram`)은 디버깅용으로 남는다.

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

**프레임 구조** — 헤더의 `XMODEM_FRAME`(1028바이트)은 구형 정의다.
AmebaD 는 **목적지 주소 4바이트가 들어간 1032바이트 프레임**을 쓴다:

```
 0    1B   0x02 STX
 1    1B   recordNo        1 부터, 255 다음 0
 2    1B   ~recordNo
 3    4B   목적지 주소 LE   ← XMODEM 에 없는 필드
 7  1024B  데이터 (부족분 0xFF)
1031  1B   체크섬 = 0xFF 에서 시작해 0..1030 바이트를 더한 u8
```

주소가 프레임마다 실리므로 별도의 주소 설정 명령이 없다. RAM(`0x00082000`)이든
플래시(`0x08006000`)든 같은 필드에 넣는다. **플래시 주소는 `0x08000000` 을 포함한
전체 주소**다 (`IS_FLASH_ADDR()` 로 RAM 과 구분한다).

핸드셰이크 보레이트는 `HANDSHAKE_BAUD = 115200` 고정이다.
타임아웃 상수: 프레임 대기 1초, 문자 대기 0.5초, 핸드셰이크 2초, 재시도 25회.

### ★ 확장 명령의 인자 길이

각 핸들러는 **정해진 바이트 수를 다 받을 때까지 블록**한다. 한 바이트라도 모자라면
ACK 없이 타임아웃한 뒤 NAK 루프로 돌아간다 — 겉보기에 "명령을 무시하는" 것처럼 보인다.

| 명령 | 총 길이 | 인자 |
|---|---|---|
| `0x17` XMERASE | 7B | 오프셋 4B LE + 섹터수 2B LE |
| `0x26` TXSTATUS | 4B+ | 레지스터 1B + 길이 1B + 길이만큼의 데이터 |
| `0x27` XM_CHECKSUM | 9B | 오프셋 4B LE + 길이 4B LE |

`0x17` / `0x27` 의 오프셋은 ROM `FLASH_Erase()` 규약대로 **0 기준**이다
(`0x08006000` 이 아니라 `0x00006000`). 프레임의 목적지 주소와 기준이 다르다.

`0x17` 은 오프셋이 64KB 정렬이고 섹터수가 16 이상이면 64KB 블록 소거로, 아니면
4KB 섹터 단위로 돈다. **오프셋 0 + 섹터수 `0xFFFF` 는 전체 칩 소거**이므로 만들지 않는다.

> 이 표는 `imgtool_flashloader_amebad.bin` 을 역어셈블해서 확정했다.
> 명령 디스패치는 `0x82724` 에 있다.
> ```
> arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
>     --adjust-vma=0x82000 imgtool_flashloader_amebad.bin
> ```

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

## 8.6 플래시가 비어 있을 때 (복구)

**UART 다운로드 모드는 칩 마스크 ROM 에 있다.** 플래시 내용과 무관하게 항상 진입할 수 있다.
완전히 빈 칩이든 잘못 쓴 칩이든 같은 방법으로 복구된다.

```bash
python3 firm-sdk/tools/flash.py --auto --with-boot
```

세 영역을 전부 쓴다 — 실행 확인 완료:

```
소거 0x08000000  2 섹터 / 0x08004000  2 섹터 / 0x08006000  34 섹터
검증 0x08000000  0x35EB30A8 OK
검증 0x08004000  0x9FBAF1BA OK
검증 0x08006000  0x027BB77C OK
```

### 플래시에는 개체별 데이터가 없다

공장 덤프를 4KB 섹터 단위로 훑으면 비어있지 않은 구간은 두 곳뿐이다:

| 구간 | 내용 |
|---|---|
| `0x08000000` - `0x08001FFF` | KM0 부트로더 |
| `0x08004000` - `0x08036FFF` | KM4 부트로더 + 애플리케이션 |

`FLASH_RESERVED_DATA_BASE`(`0x2000`) / `FLASH_SYSTEM_DATA_ADDR`(`0x3000`) 영역은
**전부 `0xFF` 다.** 부팅 로그의 `#calibration_ok` 와 MAC 주소는 eFuse/OTP 에서 오고,
UART 플래싱은 OTP 를 건드리지 않는다.

→ **저장소에 있는 것만으로 빈 칩을 완전히 되살릴 수 있다.** 별도로 백업해 둬야 하는
개체별 값은 없다. `firm-sdk/prebuilt/` 의 세 블롭은 공장 덤프와 바이트 단위로 일치함을
확인했다.

### 자동 다운로드 회로가 듣지 않을 때

수동 진입: **SW1(PA7)을 누른 채 리셋**하고 놓는다. SW1 이 다운로드 스트랩이다.

다만 다음은 주의한다:
- OpenOCD 에는 RTL872x 용 flash bank 드라이버가 없다. **SWD 로는 플래시를 못 쓴다.**
  (RAM 플래시로더를 GDB 로 구동하는 방법은 있다 — Particle `rtl872x.tcl` 참고)
- 작업 전 전체 백업을 떠 둔다: `flash-backup` 태스크 또는
  `openocd -f nu87.cfg -c init -c "nu87_backup ..." -c shutdown`
- `chip erase` 는 하지 않는다.

## 8.7 사용법

```bash
python3 firm-sdk/tools/flash.py --auto                 # 포트 자동 선택 + 앱 이미지
python3 firm-sdk/tools/flash.py --port /dev/cu.usbserial-0001
python3 firm-sdk/tools/flash.py --auto --with-boot     # 부트로더까지 (복구용)
python3 firm-sdk/tools/flash.py --auto --image x.bin --addr 0x08006000
```

VS Code 에서는 `flash` / `flash·자동` / `flash-full` / `flash-image` 태스크를 쓴다.

전체 흐름:

```
1) 115200 으로 열고 DTR/RTS 로 다운로드 모드 진입 (§8.1)
2) 배너를 흘려보내고 0x15 두 개 확인 (§8.2 — 창이 짧다)
3) 0x05 <서브커맨드> 로 보레이트 상향 → 호스트도 전환 → 0x07 로 확인
4) imgtool_flashloader_amebad.bin 을 RAM 0x00082000 에 전송
5) 0x04 로 로더 실행. ★ 로더는 115200 으로 올라오므로 호스트도 내려가서 재협상
6) 0x26 01 01 00 (상태 레지스터) → 0x17 로 섹터 소거
7) 이미지 전송 → 0x04 → 115200 재동기화 → 0x27 로 체크섬 검증
8) RTS 로 리셋해 정상 부팅
```

보레이트 서브커맨드: 1500000=`0x18`, 921600=`0x14`, 460800=`0x12`, 115200=`0x0C`.

## 8.8 참고 구현과 그 함정

- `Ameba-AIoT/ameba-arduino-d` → `Ameba_misc/Autoflash_patch/src/upload_image_tool.cpp`
  (= `jojoling/ameba_bw16_autoflash`). 사본을 `firm-sdk/tools/reference/` 에 둔다.
- `tmmsunny012/ameba-arduino-d@feature-platformio-support` → `upload_amebad.py` (순수 Python)
- `Seeed-Studio/ambd_flash_tool` → `tool/*/amebad_image_tool`

> **`upload_image_tool.cpp` 를 그대로 따라 하면 안 된다.** `u32` 두 개를 `cmd_buff`
> offset 1 과 4 에 겹쳐 쓴 뒤 6바이트(`0x17`) / 7바이트(`0x27`)만 보내서 인자가
> 한두 바이트씩 모자란다. 로더는 나머지 바이트를 기다리다 타임아웃하고 ACK 를 주지 않는다.
> §8.3 의 표가 실제 규약이다.
>
> `Seeed-Studio` 의 `amebad_image_tool` 은 SLIP(`0xC0`/`0xDB`) 프로토콜을 쓰는
> Wio Terminal 전용 도구라 이 보드와 무관하다.

> `ltchiptool` / LibreTiny 는 AmebaD 를 지원하지 않는다 (`soc/ambd` 모듈 부재).
> `uartfwburn` 은 AmebaPro2(RTL8735B) 전용이라 무관하다.
