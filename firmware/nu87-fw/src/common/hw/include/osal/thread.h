#ifndef THREAD_H_
#define THREAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_THREAD

#define THREAD_MAX_CNT  HW_THREAD_MAX_CNT


typedef int16_t thread_id_t;


/* threadCreate() 는 등록만 하고 threadBegin() 이 한꺼번에 만든다.
 * priority 는 벤더 SDK 와 같은 축(0 ~ configMAX_PRIORITIES-1), stack_bytes 는 바이트다. */
bool threadInit(void);
bool threadCreate(const char *name, void (*func)(void *arg), void *arg,
                  uint32_t priority, uint32_t stack_bytes);
bool threadBegin(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
