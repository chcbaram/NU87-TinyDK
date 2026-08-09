/*
 * platform_opts.h — NU87-TinyDK (RTL8720DF / Ameba-D KM4)
 *
 * SDK 원본은 778줄이고 WLAN / lwIP / SSL / AT커맨드 / 각종 클라우드 SDK 를 켠다
 * (src/lib/Realtek/inc_hp/platform_opts.h). 우리는 그것을 쓰지 않고 이 파일로 대체한다.
 *
 * 여기서 기능을 켜는 것이 아니라, 우리 기능 스위치는 src/hw/hw_def.h 에 있다.
 * 이 파일은 "벤더 SDK 가 무엇을 기대하는가" 에만 답하는 최소 shim 이다.
 *
 * 원본과 대조하려면 src/lib/Realtek/inc_hp/platform_opts.h 를 볼 것.
 */
#ifndef __PLATFORM_OPTS_H__
#define __PLATFORM_OPTS_H__

#include "platform_autoconf.h"

/* ── 로그 / shell ────────────────────────────────────────────────────── */
/* SDK 의 monitor shell 은 쓰지 않는다. CLI 는 common/hw/src/cli.c 가 담당한다. */
#define CONFIG_LOG_HISTORY          0
#define CONFIG_LOG_SERVICE_LOCK     0
#define CONFIG_ATCMD_MP             0
#define CONFIG_BSD_TCP              0

/* ── 무선 / 네트워크 ─────────────────────────────────────────────────── */
#ifdef _USE_HW_WIFI
#define CONFIG_WLAN                 1
#define CONFIG_LWIP_LAYER           1
#define CONFIG_INIT_NET             1   /* 부팅 시 lwIP 를 올린다 */
#define CONFIG_ETHERNET             0   /* 유선 없음 */
#else
#define CONFIG_WLAN                 0
#define CONFIG_LWIP_LAYER           0
#define CONFIG_INIT_NET             0
#define CONFIG_ETHERNET             0
#endif

/* NET_IF_NUM — 벤더 헤더 둘이 서로 다른 값을 계산한다.
 *
 *   api/lwip_netconf.h    (CONFIG_ETHERNET) + (CONFIG_WLAN)       = 1
 *   wlan/include/autoconf.h  (CONFIG_ETHERNET) + (CONFIG_WLAN) + 1 = 2
 *
 * 어느 쪽이 이기는지는 include 순서가 정한다. 실제로는 2 다 — STA(wlan0) 와
 * AP(wlan1) 자리를 함께 잡기 때문이고, 부팅 로그의
 * "interface 0/1 is initialized" 가 그 결과다.
 *
 * 그래서 여기서 먼저 못박아 모든 파일이 같은 값을 보게 한다. autoconf.h 와
 * 토큰까지 같아야 재정의 경고가 나지 않는다. xnetif[] 크기가 파일마다 달라지는
 * 것을 막는 것이 목적이다. */
#define NET_IF_NUM ((CONFIG_ETHERNET) + (CONFIG_WLAN) + 1)

/* SDK 셸의 대화형 WiFi 명령. 우리 CLI 를 쓰므로 끈다. */
#define CONFIG_INTERACTIVE_MODE     0
#define CONFIG_INTERACTIVE_EXT      0

/* ── OTA ─────────────────────────────────────────────────────────────── */
/* HTTP_OTA_UPDATE / HTTPS_OTA_UPDATE 를 정의하면 rtl8721d_ota.h 가
 * mbedtls/version.h, mbedtls/ssl.h 를 요구한다. 켤 때는 sdk_manifest.txt 에
 * mbedtls include 디렉토리도 추가해야 한다. */
#define CONFIG_OTA_UPDATE           0
#undef  HTTP_OTA_UPDATE
#undef  HTTPS_OTA_UPDATE
#undef  SDCARD_OTA_UPDATE
#define CONFIG_UART_XMODEM          0

/* ── 파일시스템 (미사용) ─────────────────────────────────────────────── */
#undef  CONFIG_FTL_ENABLED
#define CONFIG_FATFS_EN             0

/* ── 그 외 SDK 예제/클라우드 기능 전부 off ───────────────────────────── */
#define CONFIG_SSL_CLIENT           0
#define CONFIG_WEBSERVER            0
#define CONFIG_AIRKISS              0
#define CONFIG_UART_SOCKET          0
#define CONFIG_JOYLINK              0
#define CONFIG_QQ_LINK              0
#define CONFIG_GOOGLE_NEST          0
#define CONFIG_TRANSPORT            0
#define CONFIG_ALINK                0
#define CONFIG_HILINK               0
#define CONFIG_ENABLE_WPS           0
#define CONFIG_ENABLE_P2P           0
#define CONFIG_ENABLE_WPS_DISCOVERY 0
#define CONFIG_DMIC_SEL             0
#define CONFIG_AP_SECURITY          0

#endif /* __PLATFORM_OPTS_H__ */
