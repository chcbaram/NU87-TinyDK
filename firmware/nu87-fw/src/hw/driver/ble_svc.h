#ifndef BLE_SVC_H_
#define BLE_SVC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_BLE
#include <profile_server.h>


typedef void (*ble_svc_rx_cb)(uint8_t conn_id, uint8_t *p_data, uint16_t length);


T_SERVER_ID bleSvcAddService(void *p_func);
bool        bleSvcSetRxHandler(ble_svc_rx_cb handler);
bool        bleSvcIsNotifyEnabled(void);
bool        bleSvcSend(uint8_t conn_id, uint8_t *p_data, uint16_t length);

#endif

#ifdef __cplusplus
}
#endif

#endif
