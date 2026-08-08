#ifndef RTOS_H_
#define RTOS_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "def.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "event_groups.h"


void rtosInit(void);


#ifdef __cplusplus
}
#endif

#endif
