/*
 * cli_ble.c — BLE 로 붙는 CLI
 *
 * cli_net.c 와 같은 자리, 같은 역할이다. 다른 점은 통로 관리가 필요없다는 것뿐이다.
 * 소켓은 listen/accept 를 여기서 해야 했지만 BLE 링크는 hw/driver/ble.c 가
 * 이미 관리하고 HW_UART_CH_BLE 로 내보내 준다.
 *
 * 그래서 여기 남는 일은 "지금 CLI 를 넘겨도 되는가" 한 가지다.
 */

#include "cli_ble.h"


#ifdef _USE_HW_BLE
#include "ble.h"


bool cliBleIsConnected(void)
{
  /* 연결만으로는 부족하다. 호스트가 notify 를 켜야 출력이 나간다. */
  return bleIsReady(HW_UART_CH_BLE);
}

#endif
