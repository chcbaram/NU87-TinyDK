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

/* 두 채널을 가상 UART 로 내보낸다. 상위는 uartRead/uartWrite 만 쓰면 되고
 * 자기가 BLE 를 타는지 몰라도 된다.
 *
 *   HW_UART_CH_BLE        CLI       cli_mgr 가 채널을 여기로 돌린다
 *   HW_UART_CH_BLE_DATA   원시 데이터  펌웨어 업데이트 등
 */
bool       bleIsReady(uint8_t uart_ch);

#endif

#ifdef __cplusplus
}
#endif

#endif
