#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "osal/thread.h"
#include "led.h"
#include "gpio.h"
#include "reset.h"
#include "uart.h"
#include "cli.h"
#include "log.h"
#include "swtimer.h"
#include "event.h"
#include "qbuffer.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif
