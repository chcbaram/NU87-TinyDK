/*
 * platform_autoconf.h — NU87-TinyDK (RTL8720DF / Ameba-D KM4)
 *
 * SDK 원본은 menuconfig 산출물이다 (src/lib/Realtek/inc_hp/platform_autoconf.h, 211줄).
 * 우리는 그것을 쓰지 않고 이 파일로 대체한다. 이유:
 *   - 벤더 예제 설정은 WiFi / lwIP / mbedTLS / SDIO 를 전부 켜서
 *     1단계 bare-metal 빌드에 불필요한 의존성을 끌어온다
 *     (예: rtl8721d_ota.h 가 HTTPS_OTA_UPDATE 때문에 mbedtls/version.h 를 요구)
 *   - 설정은 우리 계층에 있어야 한다. STM32 프로젝트가 stm32h5xx_hal_conf.h 를
 *     src/bsp/device/ 에 두었던 것과 같은 자리다.
 *
 * 원본과 대조하려면 src/lib/Realtek/inc_hp/platform_autoconf.h 를 볼 것.
 * (그 디렉토리는 참조용이며 include 경로에 넣지 않는다)
 */
#ifndef _PLATFORM_AUTOCONF_H_
#define _PLATFORM_AUTOCONF_H_

#define AUTOCONF_INCLUDED

/* ── 칩 ──────────────────────────────────────────────────────────────── */
#define CONFIG_RTL8721D             1
#define ARM_CORE_CM4                1     /* KM4 (Cortex-M33급) 빌드 */
#define CONFIG_CHIP_A_CUT           1
#undef  CONFIG_CHIP_B_CUT
#undef  CONFIG_FPGA

/* ── 클럭 ────────────────────────────────────────────────────────────── */
#define CONFIG_CPU_CLK              1
#define CONFIG_CPU_200MHZ           1
#define PLATFORM_CLOCK              (200000000)
#define CPU_CLOCK_SEL_VALUE         (0)    /* 0 = 200MHz. app_start() 가 사용 */

/* ── 테스트 모드 (전부 off) ───────────────────────────────────────────── */
#undef  CONFIG_MP
#undef  CONFIG_CP
#undef  CONFIG_FT
#undef  CONFIG_EQC

/* ── TrustZone / 보안부팅 (미사용) ───────────────────────────────────── */
#undef  CONFIG_TRUSTZONE
#undef  CONFIG_SBootIMG2
#undef  CONFIG_SEC_VERIFY
#undef  CONFIG_ENABLE_RDP

/* ── RTOS ────────────────────────────────────────────────────────────── */
/* 1단계는 bare-metal 이다. ap 계층이 while(1) moduleUpdate() 로 돈다.
 * 무선 단계에서 FreeRTOS 로 전환할 때 아래 세 줄을 되살린다:
 *   #define CONFIG_KERNEL 1
 *   #define PLATFORM_FREERTOS 1
 *   #define TASK_SCHEDULER_DISABLED (0)
 * fwlib 안에서 이 매크로를 보는 곳은 basic_types.h 와 ameba_soc.h 두 곳뿐이다. */
#undef  CONFIG_KERNEL
#undef  PLATFORM_FREERTOS
#define TASK_SCHEDULER_DISABLED     (1)

/* ── 무선 (1단계 off) ────────────────────────────────────────────────── */
#undef  CONFIG_WIFI_EN
#undef  CONFIG_WIFI_NORMAL
#undef  CONFIG_WIFI_MODULE
#undef  CONFIG_NETWORK
#undef  CONFIG_BT_EN
#undef  CONFIG_BT

/* ── 암호화 (1단계 off) ──────────────────────────────────────────────── */
#undef  CONFIG_USE_MBEDTLS_ROM
#undef  CONFIG_MBED_TLS_ENABLED

/* ── 주변장치 서브시스템 ─────────────────────────────────────────────── */
#undef  CONFIG_SOC_PS_EN           /* 전력관리. SWD 디버깅을 방해하므로 off */
#undef  CONFIG_SOC_PS_MODULE
#undef  CONFIG_SDIO_DEVICE_EN
#undef  CONFIG_USB_OTG_EN          /* 네이티브 USB 는 Type-C 에 연결되지 않음 */
#undef  CONFIG_PINMAP_ENABLE       /* 핀먹스는 우리 hw/driver 에서 직접 한다 */
#undef  CONFIG_MBED_API_EN
#undef  CONFIG_SPI_NAND_EN

/* ── 로그 ────────────────────────────────────────────────────────────── */
#define CONFIG_DEBUG_LOG            1

/* ── 툴체인 / 링크 ───────────────────────────────────────────────────── */
/* 표준 Arm GNU Toolchain 을 쓴다 (벤더 asdk 아님).
 * 이 두 매크로는 SDK Makefile 에서만 쓰이고 C 코드에는 영향이 없으나
 * 의도를 명시해 둔다. */
#undef  CONFIG_TOOLCHAIN_ASDK
#define CONFIG_TOOLCHAIN_ARM_GCC    1

/* ROM 라이브러리(.a)를 링크하지 않고 ROM 심볼 링커스크립트로 해석한다.
 * → ld/rlx8721d_rom_symbol_acut.ld */
#undef  CONFIG_LINK_ROM_LIB
#define CONFIG_LINK_ROM_SYMB        1

#endif /* _PLATFORM_AUTOCONF_H_ */
