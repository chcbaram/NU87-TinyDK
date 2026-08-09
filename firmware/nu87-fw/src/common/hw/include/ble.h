#ifndef BLE_H_
#define BLE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_BLE

typedef enum
{
  BLE_STATE_OFF,
  BLE_STATE_INIT,
  BLE_STATE_IDLE,
  BLE_STATE_ADVERTISING,
  BLE_STATE_CONNECTED,
} BleState_t;


bool       bleInit(void);
bool       bleIsInit(void);
BleState_t bleGetState(void);
bool       bleIsConnected(void);
bool       bleAdvertise(bool enable);
bool       bleGetMac(char *p_str, uint32_t length);

#endif

#ifdef __cplusplus
}
#endif

#endif
