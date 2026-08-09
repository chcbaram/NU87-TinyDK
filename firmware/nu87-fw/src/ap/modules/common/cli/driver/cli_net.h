#ifndef CLI_NET_H_
#define CLI_NET_H_

#include "hw_def.h"

#ifdef __cplusplus
extern "C" {
#endif


#ifdef _USE_HW_WIFI

bool cliNetInit(uint16_t port);   /* HW_UART_CH_NET 에 소켓 드라이버를 등록한다 */
void cliNetPoll(void);            /* 주기 호출. listen / accept / 연결 관리 */
bool cliNetIsConnected(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
