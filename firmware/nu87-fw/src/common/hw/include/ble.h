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

/* 바이트 통로. 프로토콜은 상위가 정한다. */
typedef void (*ble_rx_cb)(uint8_t *p_data, uint16_t length);

bool       bleSetRxHandler(ble_rx_cb handler);
bool       bleSend(uint8_t *p_data, uint16_t length);
bool       bleIsReady(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
