#ifndef NET_DATA_H_
#define NET_DATA_H_

#include "ap_def.h"


#ifdef __cplusplus
extern "C" {
#endif


#ifdef _USE_HW_WIFI

bool netDataInit(uint16_t port);   /* HW_UART_CH_NET_DATA 에 소켓을 물린다 */
void netDataPoll(void);            /* 주기 호출. listen / accept */
bool netDataIsConnected(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
