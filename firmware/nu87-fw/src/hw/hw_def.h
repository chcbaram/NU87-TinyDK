#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"
#include "assert_def.h"


#define _DEF_FIRMWATRE_VERSION    "V260809R1"
#define _DEF_BOARD_NAME           "NU87-TINYDK"


// _USE_HW_RTOS 는 CMake 의 NU87_RTOS 옵션이 명령줄로 정의한다 (-DNU87_RTOS=OFF 로 끔).
#define _USE_HW_ASSERT
#define _USE_HW_RESET
#define _USE_HW_RTC
#define _USE_HW_NVS
/* 4MB 플래시의 마지막 8KB. 앞쪽이 커져도(OTA 슬롯 확장, 파일시스템 추가) 부딪히지 않는다. */
#define      HW_NVS_FLASH_OFFSET    0x003FE000


#define _USE_HW_LED
#define      HW_LED_MAX_CH          3

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_LOG         _DEF_UART1
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

#define _USE_HW_EVENT
#define      HW_EVENT_Q_MAX         8
#define      HW_EVENT_NODE_MAX      16

#define _USE_HW_GPIO
#define      HW_GPIO_MAX_CH         GPIO_PIN_MAX

/* 16 번부터가 사용자 채널이다. 0~15 는 Realtek 이 쓴다. */
#define _USE_HW_IPC
#define      HW_IPC_CH_MAX          32

/* _USE_HW_WIFI 는 CMake 의 NU87_WIFI 옵션이 명령줄로 정의한다 */


//-- CLI
//
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_ASSERT          1
#define _USE_CLI_HW_UART            1


//-- RTOS
//
#ifdef _USE_HW_RTOS

#define _USE_HW_THREAD
#define      HW_THREAD_MAX_CNT                  16

#define _HW_DEF_RTOS_THREAD_PRI_MAIN            2
#define _HW_DEF_RTOS_THREAD_MEM_MAIN            (4*1024)

#define _HW_DEF_RTOS_THREAD_PRI_CLI             2
#define _HW_DEF_RTOS_THREAD_MEM_CLI             (4*1024)

#endif


// 이름은 "헤더_핀번호_MCU핀" 이라 실크와 그대로 대응된다.
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

#endif
