/*
 * autoconf.h — NU87-TinyDK (RTL8720DF / Ameba-D KM4)
 *
 * SDK 원본은 WLAN 드라이버 설정 헤더다:
 *   component/common/drivers/wlan/realtek/include/autoconf.h
 *
 * WiFi 를 쓰지 않으므로 그 파일 대신 이 최소 stub 을 둔다.
 * fwlib 에서 이 헤더를 요구하는 곳은 usrcfg/rtl8721dhp_intfcfg.c 하나이고,
 * 거기서 보는 매크로는 CONFIG_REPEATER 와 CONFIG_MATTER_SECURE 두 개다:
 *
 *   PSRAMCFG_TypeDef psram_dev_config = {
 *   #if (defined(CONFIG_REPEATER) && CONFIG_REPEATER) || ...
 *       .psram_dev_enable = TRUE,     <- PSRAM 있는 파트용
 *   #else
 *       .psram_dev_enable = FALSE,    <- 우리가 원하는 값
 *   #endif
 *
 * 둘 다 정의하지 않으면 #else 분기를 타서 PSRAM 이 비활성화된다.
 * RTL8720DF 에는 PSRAM 이 없으므로 이것이 올바른 값이다.
 *
 * WiFi 를 켤 때는 sdk_manifest.txt 에
 *   component/common/drivers/wlan/realtek/include/autoconf.h
 * 를 추가하고 이 파일을 지운다.
 */
#ifndef NU87_AUTOCONF_H_
#define NU87_AUTOCONF_H_

/* PSRAM 없음 — 아래 두 매크로를 정의하지 않는 것이 곧 설정이다.
 *   #undef CONFIG_REPEATER
 *   #undef CONFIG_MATTER_SECURE
 */

#endif /* NU87_AUTOCONF_H_ */
