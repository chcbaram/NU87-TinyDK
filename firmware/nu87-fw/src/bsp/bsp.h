#ifndef BSP_H_
#define BSP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"

/* _USE_HW_RTOS 는 CMake 가 명령줄로 정의하므로 이 시점에 이미 확정되어 있다.
 * (hw_def.h 에서 정의하면 hw_def.h -> bsp.h 순서 때문에 여기서 보이지 않는다) */
#ifdef _USE_HW_RTOS
#include "rtos/rtos.h"
#endif

/* 벤더 SDK umbrella. 이 헤더를 통해서만 Realtek 심볼이 들어온다.
 * hw/driver 와 bsp 안에서만 보여야 하고, ap/common 으로 새면 안 된다. */
#include "ameba_soc.h"
#include "rtl8721d_system.h"


/* log.c 가 제공한다. log.h 를 include 하면 순환 의존이 생기므로 선언만 둔다. */
void logPrintf(const char *fmt, ...);


bool     bspInit(void);

void     delay(uint32_t time_ms);
void     delayUs(uint32_t delay_us);
uint32_t millis(void);
uint32_t micros(void);

void     Error_Handler(void);


#ifdef __cplusplus
}
#endif

#endif
