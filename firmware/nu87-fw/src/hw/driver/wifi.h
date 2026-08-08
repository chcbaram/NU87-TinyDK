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
bool wifiConnect(const char *ssid, const char *pass);

#endif

#ifdef __cplusplus
}
#endif

#endif
