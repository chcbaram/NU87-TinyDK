#ifndef NET_H_
#define NET_H_

#include "ap_def.h"


#ifdef __cplusplus
extern "C" {
#endif


#ifdef _USE_HW_WIFI

typedef enum
{
  NET_STATE_IDLE,
  NET_STATE_CONNECTING,
  NET_STATE_DHCP,
  NET_STATE_ONLINE,
} NetState_t;


bool        netInit(void);
bool        netIsOnline(void);
NetState_t  netGetState(void);
bool        netConnect(const char *ssid, const char *pass, bool save);
bool        netDisconnect(void);
bool        netSyncTime(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
