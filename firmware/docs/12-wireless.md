# 12. 무선 — WiFi · BLE

RTL8720DF 는 WiFi 와 BLE 를 함께 갖고 있고, **2.4GHz 라디오와 안테나는 하나뿐**이다
(NU87 은 NCWB87R01VC 칩 안테나). 둘이 동시에 도는 것처럼 보이지만 실제로는 SoC 안의
조정기가 시간을 나눠 쓴다.

```
      ap/modules/network/net/net.c     무엇에 · 언제 · 왜   (정책)
                ↓
      hw/driver/wifi.c   ble.c         켜고 붙이기          (능력)
                ↓
      lib_wlan.a         btgap.a       벤더 스택
                ↘        ↙
               rtk_coex.c              라디오 중재
```

## 12.1 계층을 가르는 기준

`hw` 와 `ap` 를 나누는 선이 헷갈리기 쉽다. wifi 가 이미 정착시킨 형태가 답이다.

| | hw/driver/wifi.c | ap/.../net/net.c |
|---|---|---|
| 하는 일 | 켜기, 끄기, 스캔, 붙기, IP 읽기 | 무엇에 붙을지, 언제 다시 붙을지, 붙은 뒤 뭘 할지 |
| 구체적으로 | `wifiOn` `wifiConnect` `wifiGetIp` | SSID 를 NVS 에, 실패하면 10초 뒤 재시도, DHCP 시점, SNTP, 검색 응답 |

판단이 애매할 때의 질문 셋이다.

1. MCU 를 바꾸면 이 코드를 다시 써야 하나 → 예면 **hw**
2. 제품 요구사항이 바뀌면 이 코드가 바뀌나 → 예면 **ap**
3. 이 판단이 틀려도 하드웨어 자체는 멀쩡한가 → 예면 **ap**

가장 기계적인 기준은 **벤더 헤더를 include 해야 하는가**다. `<gap.h>` `<wifi_conf.h>` 가
필요하면 그건 hw 다. `common/hw/include/` 의 선언만 ap 가 본다.

> BLE 에는 아직 ap 모듈이 없다. 저장할 상태도, 재시도할 것도, 붙은 뒤 할 일도 없기
> 때문이다. 장치 이름을 NVS 에 두거나, 버튼을 눌러야 광고하게 하거나, 본딩 정책이
> 생기면 그때 만든다. 없는 정책을 담을 모듈을 미리 만들면 빈 껍데기만 남는다.

## 12.2 공존 — 라디오는 하나다

SDK 근거가 분명하다. `CONFIG_PLATFORM_8721D` 에서는 `CONFIG_BT_COEXIST_SOC` 가 **무조건**
정의되고, 외부 BT 칩용 `CONFIG_BT_COEXIST` 와 `CONFIG_BT_TWO_ANTENNA` 는 주석 처리되어
있다 (`wlan/include/autoconf.h`).

`rtk_coex.c` 가 BT 스택과 WiFi 드라이버 사이의 우편함이다 — `rltk_coex_mailbox_to_wifi()`.

실측으로 확인한 것들이다.

- BLE 광고 중에 WiFi 가 붙어 있고 `thread cpu` 에 `rtw_coex_mailbo` 태스크가 돈다
- **BLE 링크가 `wifi scan` 을 견딘다.** 10초 넘는 스캔 동안 연결이 유지된다
- 그래서 BLE 로 붙은 채 WiFi 를 설정하는 것이 가능하다 — 웹페이지가 그렇게 쓴다

BLE 는 WiFi 를 요구한다(`NU87_BLE` 는 `NU87_WIFI` 없이 구성 불가). 공존 중재기가 WiFi
쪽 상태를 참조하므로 `ble.c` 의 스레드가 `wifiIsOn()` 을 기다린 뒤 스택을 올린다.

## 12.3 WiFi

`net` 모듈이 상태기를 돈다.

```
IDLE ──(NVS 에 SSID 가 있고 auto)──> CONNECTING ──> DHCP ──> ONLINE
  ↑                                      ↑                     │
  └────────── net disconnect ────────────┴──── 링크 끊김 ───────┘
```

ONLINE 이 되면 SNTP 로 RTC 를 맞추고 12시간마다 다시 맞춘다. 접속 정보는 NVS 에 남아
다음 부팅부터 스스로 붙는다.

### SNTP 는 직접 썼다

SDK 의 `component/common/network/sntp/sntp.c` 는 732줄이고 lwIP raw API 와 타이머 훅,
`SNTP_SET_SYSTEM_TIME` 매크로 배선을 요구한다. 우리가 필요한 것은 48바이트 UDP 를 한 번
주고받는 것뿐이라 소켓으로 직접 쓴다 (`netSntpQuery()`, 40줄).

### 보안 방식은 WPA2/AES 로 고정해도 된다

`wifi_conf.c` 의 switch 를 보면 WPA / WPA2 / WPA3 의 **AES 계열이 전부 같은 갈래**
(CCMP + passphrase)로 들어간다. 값이 달라도 하는 일이 같으니 스캔 결과를 참조할 필요가
없다. 갈래가 다른 것은 TKIP 전용과 WEP 뿐이고 요즘 공유기에는 없다.

### 걸렸던 것들

**`wifi_get_mac_address()` 는 문자열을 `strcpy` 한다.** `uint8_t[6]` 을 주면 스택을
넘긴다. `char[32]` 를 준다.

**`wifi_scan_networks(NULL, NULL)`** 은 NULL 콜백으로 점프해 UsageFault(PC=0)를 낸다.
핸들러를 반드시 준다.

**보안 표시에 WPA/WPA2 혼용이 빠져 있으면** 국내 공유기가 전부 `?` 로 나온다. 모르는
값은 감추지 말고 `0x...` 로 보여준다.

**벤더 드라이버 로그는 줄마다 빈 줄을 남긴다.** `rtw_debug.h` 의

```c
#define _dbgdump_nr   printf("\n\r"); printf
```

가 메시지 **앞에** 개행을 하나 더 찍는데 메시지 문자열도 개행으로 끝나서 겹친다. 이미
`lib_wlan.a` 에 컴파일된 매크로라 형식은 못 고치고, 라이브러리가 노출하는
`GlobalDebugEnable` 로 껐다 켤 수만 있다 (`wifi log on|off`).

### NET_IF_NUM — 벤더 헤더 둘이 어긋나 있다

```
api/lwip_netconf.h        (CONFIG_ETHERNET) + (CONFIG_WLAN)     = 1
wlan/include/autoconf.h   (CONFIG_ETHERNET) + (CONFIG_WLAN) + 1 = 2
```

어느 쪽이 이기는지는 include 순서가 정한다. 실제로는 **2** 다 — STA(wlan0)와 AP(wlan1)
자리를 함께 잡기 때문이고, 부팅 로그의 `interface 0/1 is initialized` 가 그 결과다.
`xnetif[]` 크기가 파일마다 달라지는 상태였다.

`src/bsp/device/platform_opts.h` 에서 autoconf.h 와 **토큰까지 같은 식**으로 먼저 못박아
모든 파일이 2 를 보게 한다. 토큰이 다르면 재정의 경고가 남는다.

## 12.4 mDNS — 이름으로 찾기

```
ping nu87-tinydk.local
telnet nu87-tinydk.local
```

`lib_mdns.a` 의 외부 의존이 43개뿐이고 전부 lwIP / FreeRTOS / swlib 이라 값이 싸다.
나머지 4개는 `mDNSPlatform.c`(42줄)가 채운다.

광고할 이름은 `mDNSPlatformHostname()` 이 `xnetif[0].hostname` 에서 가져가는데, 이 필드는
`LWIP_NETIF_HOSTNAME` 이 켜져 있을 때만 존재한다. lwIP 기본값이 0 이라 꺼져 있었고 그러면
`"ameba"` 가 되어 `ameba.local` 로 광고된다. **patch 0003** 으로 켠다. 덤으로 DHCP 요청에
호스트명이 실려 공유기 단말 목록에도 보드 이름이 뜬다.

`_telnet._tcp` 서비스로도 광고하고 TXT 에 보드 이름과 펌웨어 버전을 싣는다.

### UDP 브로드캐스트 검색

여러 대를 한눈에 훑을 때는 `tools/discover.py` 를 쓴다. PC 가 50000 번으로 `NU87?` 를
던지면 보드가 이름 · 버전 · MAC · IP 로 답한다.

```
ip               이름             버전           mac
172.30.1.97      NU87-TINYDK    V260809R1    00:e0:4c:87:00:00
```

mDNS 와 성격이 다르다. mDNS 는 **한 대에 편하게 붙는** 용도, 이쪽은 **여러 대를 훑고
버전까지 확인하는** 용도다.

## 12.5 BLE

`btgap.a`(615KB, 75멤버)가 스택 본체다. 외부 의존 63개를 채우면 링크된다.

| | 어디서 |
|---|---|
| `osif_*` 45개 | `board/common/os/freertos/osif_freertos.c` |
| `hci_tp_*` 6개 | `board/common/src/hci_adapter.c` + `board/amebad/src/hci/` |
| `ftl_*` 2개 | `file_system/ftl/ftl.c` — 본딩키 저장 |
| `trace_print` | `board/common/src/trace_task.c` |

기동 순서는 이렇다.

```c
ftl_init(0x003F8000, 3)     본딩키 저장소 (docs/06 §6.4.1)
bt_trace_init()
gap_config_max_le_link_num(1)
bte_init()                  스택 기동
le_gap_init(1)
  GAP 파라미터 / 프로파일 등록
gap_start_bt_stack(evt_q, io_q, N)   이 시점부터 메시지 큐로 이벤트가 온다
bt_coex_init()              WiFi 와의 중재기
```

**GAP 상태 변화는 콜백이 아니라 IO 메시지 큐로 온다.** `le_register_app_cb` 로는 오지
않는다. 페이로드가 union 이라 정렬이 보장되지 않으므로 벤더 예제처럼 통째로 복사한 뒤
읽는다.

### 걸렸던 것들

**`rtl8721d_bt.c` 는 빌드에 넣지 않는다.** 이미 `lib_wlan.a` 안에 있고
(`bt_get_patch_code_8721d` / `Hal_BT_Is_Supported` / `rtlbt_fw`) SDK 의 BT 빌드 목록에도
없다. 참조용으로만 벤더링돼 있다. `lib_wlan.a` 의 `rtlbt_fw` 는 weak 인 빈 배열이라 실제
패치는 `bt_normal_patch.c` 가 준다.

**링커스크립트에 `.BTTRACE` 를 버리면 링크가 깨진다.** `btgap.a` 의 `.text` 가 그 안의
심볼을 참조한다. 벤더 스크립트대로 `BTRACE` 리전(0x00800000)을 만들어 담는다. 이미지 밖
주소 공간이라 크기에는 영향이 없다.

**FreeRTOS 힙 128KB 로는 WiFi 와 BLE 를 같이 못 켠다.** HCI 버퍼 할당이 실패한다
(`[HCI I/F]Malloc failed [free heap size: 7840]`). 256KB 로 올렸다. BD_RAM_NS 55% → 82%.

**광고 데이터 버퍼가 작으면 상태는 ADVERTISING 인데 스캐너에 안 보인다.** 이름이 안
들어가 길이 0 으로 광고한 것인데 로그로는 전혀 드러나지 않는다. 31바이트로 잡는다.

### ★ UUID 는 16비트여야 한다

서비스는 선택의 여지가 없다. 속성 테이블의 `type_value` 가 2+14 바이트라
`PRIMARY_SERVICE(2) + 128비트 UUID(16)` 18바이트가 들어가지 않는다.

특성도 16비트로 갔다. `ATTRIB_FLAG_UUID_128BIT` 로 128비트를 주면 `server_add_service()`
는 성공하는데 **GATT 서버가 ATT 요청에 답하지 않아** 연결이 supervision timeout(0x0108)
으로 끊긴다. 이분법으로 확인했다.

```
우리 서비스 제거        -> 정상
특성 UUID 만 16비트로   -> 정상
```

`bt_flags.h` 에 관련 스위치는 없고 벤더의 128비트 예제(`ble_matter_adapter`)도 이
라이브러리에서 같은 증상이다. Web Bluetooth 는 16비트로도 그대로 필터·접근하므로 실사용에
지장은 없다.

### 서비스 구성

```
0xFF87  설정 서비스
  0xFF88 / 0xFF89   CLI   write / notify   -> HW_UART_CH_BLE
  0xFF8A / 0xFF8B   DATA  write / notify   -> HW_UART_CH_BLE_DATA
0x180F  배터리
```

**광고에 서비스 UUID 를 실어야 한다.** 브라우저는
`requestDevice({filters:[{services:[...]}]})` 를 광고에 실린 UUID 로 거르므로, 빠뜨리면
장치 선택창에 아예 뜨지 않는다.

**notify 에는 흐름 제어가 필요하다.** 스택 송신 큐가 차면 `server_send_data()` 가
실패한다. 그냥 넘어가면 출력이 소리없이 잘린다 — `uart info` 가 2채널에서, `help` 가
6개에서 멈췄다. 큐가 빌 때까지 쉬었다 다시 넣는다.

## 12.6 CLI 는 통로를 모른다

`cli_net.c` 가 텔넷 소켓을 `uart_driver_t` 로 위장시킨 것과 같은 방법을 BLE 에도 쓴다.
CLI 코어는 자기가 무엇을 타는지 모른다.

| 채널 | 정체 |
|---|---|
| `HW_UART_CH_LOG` | LOGUART (USB) — 유일한 실제 하드웨어 |
| `HW_UART_CH_NET` | 텔넷 소켓 |
| `HW_UART_CH_BLE` | GATT 0xFF88/89 |
| `HW_UART_CH_BLE_DATA` | GATT 0xFF8A/8B — 대량 전송용 |

`cli_mgr` 이 채널을 고른다. 우선순위는 **로컬 UART > BLE > 텔넷** 이다. 텔넷은 네트워크가
살아 있을 때만 되고 BLE 는 네트워크가 죽었을 때 쓰는 마지막 통로라 뒤에 본다. 로컬 UART 에
입력이 들어오면 언제든 되돌아오므로 원격에 물려 있어도 콘솔을 잃지 않는다.

> 가상 채널이 생기면서 `uartIsHw()` 가 필요해졌다. 이 구분이 없으면 드라이버가 아직 안
> 붙은 가상 채널로 쓴 것이 LOGUART 로 샌다.
