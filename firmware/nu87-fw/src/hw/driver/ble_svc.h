#ifndef BLE_SVC_H_
#define BLE_SVC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_BLE
#include <profile_server.h>


enum
{
  BLE_SVC_CH_CLI,
  BLE_SVC_CH_DATA,
  BLE_SVC_CH_MAX
};

typedef void (*ble_svc_rx_cb)(uint8_t conn_id, uint8_t *p_data, uint16_t length);


T_SERVER_ID bleSvcAddService(void *p_func);
bool        bleSvcSetRxHandler(uint8_t ch, ble_svc_rx_cb handler);
bool        bleSvcIsNotifyEnabled(uint8_t ch);
bool        bleSvcSend(uint8_t ch, uint8_t conn_id, uint8_t *p_data, uint16_t length);

#endif

#ifdef __cplusplus
}
#endif

#endif
