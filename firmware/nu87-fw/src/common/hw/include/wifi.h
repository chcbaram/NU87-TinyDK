#ifndef WIFI_H_
#define WIFI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_WIFI

bool wifiInit(void);
bool wifiOn(void);
bool wifiOff(void);
bool wifiIsOn(void);
bool wifiIsConnected(void);
bool wifiConnect(const char *ssid, const char *pass);
bool wifiDisconnect(void);
bool wifiGetMac(char *p_str, uint32_t length);
bool wifiGetIp(uint8_t *p_ip);
bool wifiDhcpStart(void);
bool wifiSetLogEnable(bool enable);

#endif

#ifdef __cplusplus
}
#endif

#endif
