#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"
#include "assert_def.h"


#define _DEF_FIRMWATRE_VERSION    "V260808R1"
#define _DEF_BOARD_NAME           "NU87-TINYDK"


//-- HW
//
#define _USE_HW_ASSERT

#define _USE_HW_LED
#define      HW_LED_MAX_CH          3      // _DEF_LED1=RED, _DEF_LED2=GREEN, _DEF_LED3=BLUE

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_LOG         _DEF_UART1   // LOGUART (PA7 TX / PA8 RX) -> CP2102N
#define      HW_UART_CH_CLI         HW_UART_CH_LOG

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_CLI_GUI
#define      HW_CLI_GUI_WIDTH       80
#define      HW_CLI_GUI_HEIGHT      24

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_LOG
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    4096

#define _USE_HW_SWTIMER
#define      HW_SWTIMER_MAX_CH      16

/* 이벤트 pub/sub. ap/modules/module.h 의 module_t 가 event_func_t 를 필드로
 * 갖고 있어 모듈 레지스트리를 쓰는 한 이 스위치가 필요하다. */
#define _USE_HW_EVENT
#define      HW_EVENT_Q_MAX         8
#define      HW_EVENT_NODE_MAX      16

/* GPIO — P1/P2 확장 헤더로 나온 핀만 채널로 연다.
 *
 * 보드의 두 버튼은 채널에 넣지 않는다. 둘 다 다른 기능과 공유한다:
 *   SW1 = PA7   LOGUART_TX 와 공유이고 UART_DOWNLOAD 부팅 스트랩이다.
 *               콘솔을 쓰는 동안 입력으로 읽을 수 없다.
 *   SW3 = PA27  SWDIO 와 공유. 파워온 시 Low 면 부팅에 실패한다.
 *
 * 채널에서 뺀 나머지 헤더 핀은 hw/driver/gpio.c 상단에 이유와 함께 적었다. */
#define _USE_HW_GPIO
#define      HW_GPIO_MAX_CH         9

typedef enum
{
  P1_5_PA15,
  P2_4_PB2,
  P2_5_PB1,
  P2_7_PB20,
  P2_8_PB21,
  P2_9_PB18,
  P2_10_PB19,
  P2_11_PB22,
  P2_12_PB23,
  GPIO_PIN_MAX
} GpioPinName_t;

/* 리셋 사유(BOOT_Reason)와 부팅 모드(백업 레지스터 1). */
#define _USE_HW_RESET


//-- CLI
//
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_ASSERT          1
#define _USE_CLI_HW_UART            1

#endif
