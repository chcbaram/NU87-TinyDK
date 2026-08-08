# 07. 개발환경 구축 (Windows / macOS / Linux)

**복사해서 그대로 따라가면 되도록** 단계별로 적었다. 중간을 생략하지 않았다.
각 단계 끝에 **검증 명령**이 있으니 반드시 통과하는지 확인하고 다음으로 넘어갈 것.

## 7.0 필요한 것 요약

| # | 항목 | 최소 버전 | 용도 | 필수 |
|---|---|---|---|---|
| 1 | **Arm GNU Toolchain** (`arm-none-eabi-gcc`) | **13.3** | 컴파일 | 필수 |
| 2 | **CMake** | 3.13 | 빌드 구성 | 필수 |
| 3 | **빌드 도구** (Ninja 또는 Make) | — | 빌드 실행 | 필수 |
| 4 | **Python** + `pyserial` | 3.8 | 플래싱·이미지 생성 | 필수 |
| 5 | **Git** | — | 저장소, SDK 서브모듈 | 필수 |
| 6 | **OpenOCD** | 0.12.0 | SWD 디버깅 | 선택 |
| 7 | **CP210x USB-UART 드라이버** | — | 보드 콘솔·플래싱 | **Windows만** |
| 8 | **VS Code** + 확장 | — | 편집·디버깅 | 선택 |

> **툴체인 버전 주의**: `13.3` 미만은 CMake가 configure 단계에서 거부한다.
> 그리고 **배포판/패키지 매니저의 `gcc-arm-none-eabi`는 쓰지 말 것** —
> Cortex-M33 하드 부동소수점 멀티립(`thumb/v8-m.main+fp/hard`)이 빠진 빌드가 흔하고,
> 그러면 CMake가 멀티립 검사에서 거부한다. **developer.arm.com 공식 배포본**을 쓴다.

---

## 7.1 Windows

PowerShell을 **관리자 권한으로** 열어서 진행한다 (`Win+X` → "터미널(관리자)").

### 1단계 — Arm GNU Toolchain

**방법 A: 설치 프로그램 (권장)**

1. 브라우저로 <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads> 접속
2. **"Windows (mingw-w64-i686) hosted cross toolchains"** → **"AArch32 bare-metal target (arm-none-eabi)"** 항목에서
   `arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-arm-none-eabi.exe` 다운로드
   (13.3 을 쓰려면 페이지 상단에서 버전을 13.3.Rel1 로 바꾼 뒤 같은 파일명을 찾는다)
3. 실행 → 설치 경로 기본값(`C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1`) 사용
4. **마지막 화면의 `Add path to environment variable` 체크박스를 반드시 켠다.**
   ← 이걸 놓치면 이후 모든 단계가 "명령을 찾을 수 없음"으로 실패한다.
5. **PowerShell 창을 닫고 새로 연다** (PATH 갱신 반영)

체크박스를 깜빡했다면 수동으로 PATH 추가:
```powershell
$tc = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin"
[Environment]::SetEnvironmentVariable("Path", "$env:Path;$tc", "User")
# 새 PowerShell 창을 열어야 적용된다
```

**방법 B: winget**
```powershell
winget install --id Arm.GnuArmEmbeddedToolchain
```
> winget 판은 버전이 고정되지 않아 13.3 미만이 깔릴 수 있다. 아래 검증에서 확인할 것.

**검증**
```powershell
arm-none-eabi-gcc --version
# → arm-none-eabi-gcc (Arm GNU Toolchain 14.2.Rel1 ...) 14.2.1 ...

arm-none-eabi-gcc -print-multi-directory -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
# → thumb/v8-m.main+fp/hard      ← 이 값이 나와야 한다
```
두 번째 명령이 다른 값을 내면 툴체인이 잘못된 것이다. 방법 A로 다시 설치할 것.

### 2단계 — CMake · Ninja · Git · Python

```powershell
winget install --id Kitware.CMake
winget install --id Ninja-build.Ninja
winget install --id Git.Git
winget install --id Python.Python.3.12
```

**PowerShell 창을 닫고 새로 연 뒤** 검증:
```powershell
cmake --version     # 3.13 이상
ninja --version
git --version
python --version    # 3.8 이상
```

> `python` 이 Microsoft Store 페이지를 열면 winget 설치가 PATH에 안 잡힌 것이다.
> `py --version` 을 대신 써 보고, 그래도 안 되면
> <https://www.python.org/downloads/windows/> 에서 설치하면서
> **"Add python.exe to PATH"** 를 체크한다.

### 3단계 — pyserial

```powershell
python -m pip install --upgrade pip
python -m pip install pyserial
```

**검증**
```powershell
python -c "import serial; print('pyserial', serial.__version__)"
# → pyserial 3.5
```

### 4단계 — CP210x USB-UART 드라이버 (Windows만 필요)

보드의 CP2102N은 **Windows에서 드라이버가 필요하다.** (macOS/Linux는 내장)

1. <https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers> 접속
2. **"CP210x Universal Windows Driver"** ZIP 다운로드 → 압축 해제
3. 해제한 폴더에서 `silabser.inf` 우클릭 → **"설치"**
4. 보드를 USB-C 케이블로 연결

**검증**
```powershell
# 장치 관리자에서 "포트(COM & LPT)" 아래 "Silicon Labs CP210x USB to UART Bridge (COMx)"
Get-PnpDevice -Class Ports | Format-Table Name, Status
# 또는 PowerShell 에서
[System.IO.Ports.SerialPort]::GetPortNames()
# → COM3  같은 값
```
포트 번호(`COM3` 등)를 기억해 둔다. 이후 플래싱에 쓴다.

> **장치가 아예 안 보이면 드라이버 문제가 아니다.** USB 케이블이 충전 전용일 가능성이 높다.
> 데이터 전송이 되는 USB-C 케이블로 바꿔 볼 것. (실제로 이 문제로 한 번 막혔다)

### 5단계 — OpenOCD (SWD 디버깅, 선택)

winget에는 없다. xPack 배포본을 쓴다.

1. <https://github.com/xpack-dev-tools/openocd-xpack/releases> 에서
   최신 `xpack-openocd-0.12.0-*-win32-x64.zip` 다운로드
2. `C:\tools\openocd` 에 압축 해제 (경로에 공백이 없게)
3. PATH 추가:
```powershell
[Environment]::SetEnvironmentVariable("Path", "$env:Path;C:\tools\openocd\bin", "User")
# 새 PowerShell 창을 열어야 적용된다
```

**검증**
```powershell
openocd --version
# → Open On-Chip Debugger 0.12.0 ...
```

### 6단계 — ST-LINK를 OpenOCD에서 쓰기 (libusb 드라이버)

ST의 기본 ST-LINK 드라이버는 OpenOCD가 쓰지 못한다. **WinUSB로 바꿔야 한다.**

1. <https://zadig.akeo.ie/> 에서 Zadig 다운로드 (설치 불필요, 실행 파일)
2. ST-LINK를 USB에 연결
3. Zadig 실행 → 메뉴 **Options → List All Devices** 체크
4. 드롭다운에서 **`STM32 STLink`** 선택
   - ⚠️ ST-LINK/V2-1은 인터페이스가 여러 개다. `STM32 STLink` 를 고를 것.
     `ST-Link Debug` 나 `USB Mass Storage` 를 고르면 안 된다.
5. 오른쪽 드라이버 칸을 **`WinUSB`** 로 맞춘다
6. **Replace Driver** 클릭

**검증**
```powershell
openocd -f interface/stlink-dap.cfg -c "transport select dapdirect_swd" -c "init" -c "shutdown"
# → Info : STLINK V2J46M33 (API v2) VID:PID 0483:3752
#    Info : Target voltage: 3.2...
```
`Error: open failed` 가 나면 Zadig에서 잘못된 인터페이스를 바꾼 것이다. 4단계로 돌아갈 것.

> ST-LINK 조건: **V2 또는 V2-1**, 펌웨어 **≥ V2J32**. V3는 비-ST 타겟을 거부한다.
> 펌웨어 버전은 위 OpenOCD 출력에서 확인한다. 낮으면 ST-LINK Upgrade 도구로 올린다.

### 7단계 — 저장소 클론 & 빌드

```powershell
cd C:\work
git clone <저장소 URL> NU87-TinyDK
cd NU87-TinyDK\firmware\nu87-fw

cmake -S . -B build -G Ninja
cmake --build build
```

**검증** — configure 출력에 이 줄이 나와야 한다:
```
-- arm-none-eabi-gcc 14.2.1 (>= 13.3) : C:/Program Files (x86)/.../arm-none-eabi-gcc.exe
```

> `Ninja` 대신 MinGW make를 쓰려면 `-G "MinGW Makefiles"` 를 준다.
> 서브모듈은 **초기화하지 않아도 된다** — 빌드에 필요 없다.

### Windows 흔한 실패

| 증상 | 원인 / 해결 |
|---|---|
| `arm-none-eabi-gcc : 명령을 찾을 수 없습니다` | 설치 시 PATH 체크박스 누락. 위 1단계 수동 PATH 추가 후 **새 창** |
| PATH를 추가했는데도 안 됨 | PowerShell 창을 새로 열지 않았다. 기존 창은 PATH를 갱신하지 않는다 |
| 멀티립이 `thumb/v8-m.main/nofp` | 잘못된 툴체인. developer.arm.com 공식 배포본으로 재설치 |
| CMake가 `CMAKE_MAKE_PROGRAM is not set` | 생성기를 지정하지 않았다. `-G Ninja` 를 붙인다 |
| COM 포트가 안 보임 | ① CP210x 드라이버 미설치 ② **충전 전용 USB 케이블** |
| OpenOCD `open failed` | Zadig로 `STM32 STLink` 인터페이스를 WinUSB로 바꾸지 않았다 |
| 경로에 한글/공백 | `C:\work\` 같은 짧은 ASCII 경로에 클론할 것 |

---

## 7.2 macOS

Apple Silicon(arm64) / Intel(x86_64) 모두 동일하다.

### 1단계 — Homebrew

```bash
# 이미 있으면 건너뛴다
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```
설치 후 안내되는 `eval "$(/opt/homebrew/bin/brew shellenv)"` 를 실행하고 셸 설정에 추가한다.

**검증**
```bash
brew --version
```

### 2단계 — Arm GNU Toolchain

```bash
brew install --cask gcc-arm-embedded
```

**검증**
```bash
arm-none-eabi-gcc --version
# → arm-none-eabi-gcc (Arm GNU Toolchain 14.2.Rel1 ...) 14.2.1 ...

arm-none-eabi-gcc -print-multi-directory -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
# → thumb/v8-m.main+fp/hard
```

> cask가 13.3 미만을 깔았거나 특정 버전이 필요하면 공식 tarball을 쓴다:
> ```bash
> cd ~/opt
> curl -fLO https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi.tar.xz
> tar xf arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi.tar.xz
> echo 'export PATH="$HOME/opt/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"' >> ~/.zshrc
> source ~/.zshrc
> ```
> Intel Mac은 파일명의 `darwin-arm64` 를 `darwin-x86_64` 로 바꾼다.
> **Gatekeeper 차단 시**: `xattr -dr com.apple.quarantine <툴체인 폴더>`

### 3단계 — CMake · Ninja · Git · OpenOCD

```bash
brew install cmake ninja git open-ocd
```

**검증**
```bash
cmake --version && ninja --version && git --version && openocd --version
```

### 4단계 — Python + pyserial

macOS 내장 `python3` 를 쓰거나 brew판을 쓴다.
```bash
brew install python
python3 -m pip install --upgrade pip
python3 -m pip install pyserial
```

**검증**
```bash
python3 -c "import serial; print('pyserial', serial.__version__)"
```

> `error: externally-managed-environment` 가 나오면:
> `python3 -m pip install --break-system-packages pyserial`
> 또는 가상환경을 쓴다: `python3 -m venv ~/.venv/nu87 && source ~/.venv/nu87/bin/activate && pip install pyserial`

### 5단계 — 보드 연결 (드라이버 설치 불필요)

macOS 11 이상은 CP210x 드라이버가 **내장**되어 있다 (`AppleUSBSLCOM.dext`).
보드를 USB-C로 연결하고:

**검증**
```bash
ls /dev/cu.usbserial-*
# → /dev/cu.usbserial-0001

# 장치가 USB 레벨에서 보이는지
system_profiler SPUSBDataType | grep -A3 CP2102N
```

> **`/dev/cu.*` 를 쓴다. `/dev/tty.*` 는 쓰지 않는다** — 후자는 DCD 신호를 기다려 블록된다.
>
> `ls` 가 아무것도 못 찾고 `system_profiler` 에도 CP2102N이 없으면 드라이버 문제가 아니다.
> **충전 전용 USB 케이블**일 가능성이 높다. 데이터 케이블로 바꿀 것.

### 6단계 — 빌드

```bash
git clone <저장소 URL> NU87-TinyDK
cd NU87-TinyDK/firmware/nu87-fw
cmake -S . -B build -G Ninja
cmake --build build
```

### macOS 참고

- `st-info --probe` 는 `Found 0 stlink programmers` 를 낸다 (libusb가 커널 드라이버
  detach 권한을 못 얻음). **OpenOCD는 정상 동작하므로 무시**한다.
  ST-LINK 펌웨어 버전은 OpenOCD 출력에서 확인한다.
- 문서의 SVG 다이어그램을 직접 렌더링해 보려면 `brew install librsvg` (선택).

---

## 7.3 Linux (Ubuntu 22.04 / 24.04 · Debian)

### 1단계 — 기본 패키지

```bash
sudo apt update
sudo apt install -y cmake ninja-build git python3 python3-pip python3-venv \
                    openocd curl xz-utils
```

**검증**
```bash
cmake --version && ninja --version && git --version && openocd --version
```

> `apt` 의 openocd가 0.12 미만이면 xPack 배포본을 쓴다:
> <https://github.com/xpack-dev-tools/openocd-xpack/releases> 의
> `xpack-openocd-0.12.0-*-linux-x64.tar.gz` 를 `/opt` 에 풀고 PATH에 추가.

### 2단계 — Arm GNU Toolchain

**`sudo apt install gcc-arm-none-eabi` 를 쓰지 말 것.** 버전이 낮고 멀티립이 빠진 경우가 있다.
공식 tarball을 쓴다:

```bash
cd /tmp
curl -fLO https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
sudo mkdir -p /opt
sudo tar xf arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz -C /opt
sudo ln -sfn /opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi /opt/arm-gnu-toolchain

echo 'export PATH="/opt/arm-gnu-toolchain/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```
> ARM64 호스트(라즈베리파이 등)는 파일명의 `x86_64` 를 `aarch64` 로 바꾼다.
> 13.3을 쓰려면 URL의 `14.2.rel1` 두 곳을 `13.3.rel1` 로 바꾼다.

**검증**
```bash
arm-none-eabi-gcc --version
arm-none-eabi-gcc -print-multi-directory -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
# → thumb/v8-m.main+fp/hard
```

> 32비트 라이브러리 오류(`No such file or directory` 인데 파일은 있음)가 나면
> 구형 32비트 툴체인을 받은 것이다. 위 x86_64 URL을 다시 확인할 것.

### 3단계 — pyserial

```bash
python3 -m pip install --user pyserial
```
**검증**
```bash
python3 -c "import serial; print('pyserial', serial.__version__)"
```
> `externally-managed-environment` 오류 시:
> `sudo apt install python3-serial` 또는 `pip install --break-system-packages pyserial`

### 4단계 — 시리얼 포트 권한

CP210x 드라이버는 커널에 내장(`cp210x` 모듈)되어 있지만 **권한 설정이 필요하다.**

```bash
sudo usermod -aG dialout $USER
```
**로그아웃 후 다시 로그인해야 적용된다** (또는 재부팅).

**검증**
```bash
groups | grep dialout      # dialout 이 보여야 한다
ls -l /dev/ttyUSB0         # → crw-rw---- 1 root dialout ...
```
보드를 연결한 뒤:
```bash
dmesg | tail -5
# → cp210x converter now attached to ttyUSB0
```

### 5단계 — OpenOCD USB 권한 (udev)

sudo 없이 ST-LINK를 쓰려면:
```bash
# openocd 패키지에 포함된 규칙 사용
sudo cp /usr/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```
규칙 파일이 없으면 직접 만든다:
```bash
sudo tee /etc/udev/rules.d/60-openocd.rules >/dev/null <<'EOF'
# ST-LINK V2 / V2-1 / V3
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3752", MODE="660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374e", MODE="660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374f", MODE="660", GROUP="plugdev", TAG+="uaccess"
EOF
sudo usermod -aG plugdev $USER
sudo udevadm control --reload-rules && sudo udevadm trigger
```
ST-LINK를 **뽑았다 다시 꽂는다.**

**검증**
```bash
openocd -f interface/stlink-dap.cfg -c "transport select dapdirect_swd" -c "init" -c "shutdown"
# → Info : STLINK V2J46M33 ... / Target voltage: 3.2...
```
`LIBUSB_ERROR_ACCESS` 가 나면 udev 규칙이 적용되지 않았거나 재로그인을 하지 않은 것이다.

### 6단계 — 빌드

```bash
git clone <저장소 URL> NU87-TinyDK
cd NU87-TinyDK/firmware/nu87-fw
cmake -S . -B build -G Ninja
cmake --build build
```

---

## 7.4 VS Code (선택, 3 OS 공통)

```
확장 설치:
  ms-vscode.cpptools          C/C++
  ms-vscode.cmake-tools       CMake
  marus25.cortex-debug        SWD 디버깅
```

워크스페이스 열기: `firmware/nu87-fw/prj/nu87-fw.code-workspace`
→ `nu87-fw` 와 `firm-sdk` 두 폴더가 함께 보인다.

IntelliSense는 `build/compile_commands.json` 을 참조하므로 **한 번은 빌드해야** 코드 탐색이 정상 동작한다.

---

## 7.5 최종 점검 — 한 번에 확인

**macOS / Linux**
```bash
echo "── toolchain ──"; arm-none-eabi-gcc --version | head -1
echo "── multilib ──";  arm-none-eabi-gcc -print-multi-directory -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
echo "── cmake ──";     cmake --version | head -1
echo "── ninja ──";     ninja --version
echo "── python ──";    python3 --version; python3 -c "import serial; print('pyserial', serial.__version__)"
echo "── openocd ──";   openocd --version 2>&1 | head -1
echo "── serial ──";    ls /dev/cu.usbserial-* 2>/dev/null || ls /dev/ttyUSB* 2>/dev/null || echo "(보드 미연결)"
```

**Windows (PowerShell)**
```powershell
"-- toolchain --"; arm-none-eabi-gcc --version | Select-Object -First 1
"-- multilib --";  arm-none-eabi-gcc -print-multi-directory -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
"-- cmake --";     cmake --version | Select-Object -First 1
"-- ninja --";     ninja --version
"-- python --";    python --version; python -c "import serial; print('pyserial', serial.__version__)"
"-- openocd --";   openocd --version 2>&1 | Select-Object -First 1
"-- serial --";    [System.IO.Ports.SerialPort]::GetPortNames()
```

기대값:

| 항목 | 기대 |
|---|---|
| toolchain | `13.3.1` 이상 |
| **multilib** | **`thumb/v8-m.main+fp/hard`** ← 이게 제일 중요하다 |
| cmake | `3.13` 이상 |
| python | `3.8` 이상, `pyserial 3.5` |
| openocd | `0.12.0` 이상 |
| serial | `/dev/cu.usbserial-0001` · `/dev/ttyUSB0` · `COM3` 중 하나 |

전부 통과하면:
```bash
cd firmware/nu87-fw
cmake -S . -B build -G Ninja && cmake --build build
```

configure 출력에 다음 줄이 보이면 성공이다:
```
-- arm-none-eabi-gcc 14.2.1 (>= 13.3) : /opt/homebrew/bin/arm-none-eabi-gcc
```

---

## 7.6 툴체인 버전 정책

`firm-sdk/tools/arm-none-eabi-gcc.cmake` 가 configure 단계에서 두 가지를 검사한다.

**① 버전 하한 — 13.3**

```
CMake Error: arm-none-eabi-gcc 12.2.1 은 너무 낮습니다. 최소 13.3 이 필요합니다.
```

13.3을 하한으로 정한 근거:
- `cortex-m33` / `armv8-m.main` / `fpv5-sp-d16` / `-mcmse` 는 GCC 7부터 있으므로
  **이론상 더 낮은 버전도 가능**하다. 다만 **실제로 검증한 것은 13.3부터**다.
- 벤더 SDK 코드는 GCC 6.4.1 기준이라 최신 GCC의 엄격해진 진단에 걸린다.
  어느 진단이 error인지가 버전마다 달라서 하한을 고정해야 재현성이 생긴다.

**검증 결과 (fwlib 소스 30개 컴파일)**

| GCC | 멀티립 | `ameba_soc.h` | fwlib 30개 | 결과 |
|---|---|---|---|---|
| **13.3.1** (Arm 13.3.Rel1) | `thumb/v8-m.main+fp/hard` | OK | 성공 30 / 실패 0 | ✅ |
| **14.2.1** (Arm 14.2.Rel1) | `thumb/v8-m.main+fp/hard` | OK | 성공 30 / 실패 0 | ✅ |

**② 멀티립 일치**

```
CMake Error: 멀티립이 맞지 않습니다: 'thumb/v8-m.main/nofp'
             (기대값 'thumb/v8-m.main+fp/hard')
```
ROM 코드와 ABI가 맞아야 한다. Realtek은
`-march=armv8-m.main+dsp` + `arm-none-eabi/lib/v8-m.main/fpu/fpv5-sp-d16` 으로 링크한다.
배포판 패키지에서 이 멀티립이 빠지는 일이 흔해서 명시적으로 검사한다.

### 버전별 진단 차이 — 완화 플래그가 갈리는 이유

GCC 14가 아래 진단을 **기본 error로 승격**했다. GCC 13은 전부 warning이다.

| 진단 | GCC 13.3 | GCC 14.2 |
|---|---|---|
| `-Wint-conversion` | warning | **error** |
| `-Wimplicit-function-declaration` | warning | **error** |
| `-Wincompatible-pointer-types` | warning | **error** |
| `-Wreturn-mismatch` | **옵션 자체가 없음** | **error** |

벤더 소스에만 `-Wno-error=` 로 되돌리는데, **`-Wreturn-mismatch` 는 GCC 13에 존재하지 않는
이름이라 그냥 주면 하드 에러가 난다**:

```
cc1: error: '-Wno-error=return-mismatch': no option '-Wreturn-mismatch';
     did you mean '-Wargument-mismatch'?
```

그래서 툴체인 파일이 버전으로 가른다:
```cmake
set(NU87_VENDOR_RELAX_FLAGS
  -Wno-error=int-conversion
  -Wno-error=implicit-function-declaration
  -Wno-error=incompatible-pointer-types)

if(NU87_GCC_VERSION VERSION_GREATER_EQUAL 14)
  list(APPEND NU87_VENDOR_RELAX_FLAGS -Wno-error=return-mismatch)
endif()
```

이 완화는 **벤더 소스에만** 적용한다. 우리 코드(`src/ap`, `src/hw`, `src/bsp`)는
기존처럼 `-Wall` 을 엄격하게 유지한다. CMake `set_source_files_properties()` 로 분리한다.
