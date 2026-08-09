# NU87-TinyDK

NU87 모듈(Realtek RTL8720DF / Ameba-D) 개발보드의 펌웨어와 문서.

## 설정 페이지

**<https://chcbaram.github.io/NU87-TinyDK/>**

브라우저에서 보드에 바로 붙는다. Web Bluetooth 로 연결해 보드 정보를 보고
WiFi 를 설정한다. 설치할 것은 없고 Chrome / Edge 에서 열면 된다.

보드는 `NU87-TINYDK` 라는 이름으로 광고하고 있어야 한다.

## 구성

| | |
|---|---|
| [firmware/nu87-fw](firmware/nu87-fw) | 펌웨어. `ap` / `common` / `hw` / `bsp` 4계층 |
| [firmware/firm-sdk](firmware/firm-sdk) | 벤더 SDK 선별 복사본과 도구 (플래싱, 이미지 생성, 보드 검색) |
| [firmware/docs](firmware/docs) | 하드웨어 분석부터 빌드·부팅·드라이버까지 |
| [hardware](hardware) | 회로도와 데이터시트 |

## 빌드

```
cd firmware/nu87-fw
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

`arm-none-eabi-gcc` 13.3 이상이면 된다. 벤더 asdk 툴체인도, SDK 서브모듈도
필요하지 않다 — 빌드에 쓰는 것은 `firm-sdk/lib/Realtek/` 에 커밋된 사본이다.
서브모듈은 그 사본을 다시 만들 때만 쓴다.

옵션은 `-DNU87_WIFI=ON`, `-DNU87_BLE=ON` 이다. BLE 는 WiFi 를 요구한다.
2.4GHz 라디오와 안테나가 하나뿐이라 둘이 시간을 나눠 쓰기 때문이다.

## 굽기

```
python3 firmware/firm-sdk/tools/flash.py --auto --image build/km0_km4_image2.bin
```

USB-C 케이블 하나로 된다. 보드에 디버거는 없고 CP2102N 이 LOGUART 에 붙어
있어 플래싱과 콘솔이 같은 포트를 쓴다.

## 접속

| | |
|---|---|
| USB | `screen /dev/tty.usbserial-* 115200` |
| 텔넷 | `telnet nu87-tinydk.local` — WiFi 접속 후 |
| BLE | 위 설정 페이지, 또는 CLI 를 그대로 |

같은 CLI 가 세 통로로 나온다. 로컬 UART 에 입력이 들어오면 언제든 그쪽이
우선이라 원격에 물려 있어도 콘솔을 잃지 않는다.

보드를 찾을 때는 `python3 firmware/firm-sdk/tools/discover.py` 로 같은
네트워크의 보드를 이름·버전·IP 와 함께 훑을 수 있다.
