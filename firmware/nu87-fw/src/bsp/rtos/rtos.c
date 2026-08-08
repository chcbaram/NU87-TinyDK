#include "bsp.h"

#ifdef _USE_HW_RTOS


/* heap_5 리전. 링커 심볼로 남은 SRAM 전체를 넘기면 newlib 의 _sbrk 힙과 겹친다. */
static uint8_t rtos_heap[configTOTAL_HEAP_SIZE];

static const HeapRegion_t rtos_heap_regions[] =
{
  { rtos_heap, sizeof(rtos_heap) },
  { NULL,      0                 },
};




void rtosInit(void)
{
  /* 첫 pvPortMalloc 보다 먼저여야 한다 */
  vPortDefineHeapRegions(rtos_heap_regions);

  /* 벤더 부팅 코드가 이 자리에 자기 기본 핸들러를 넣어 두므로 덮어써야 한다.
   * 그대로 두면 첫 컨텍스트 스위치(PendSV)에서 죽는다. */
  __NVIC_SetVector(SVCall_IRQn,  (uint32_t)(void *)vPortSVCHandler);
  __NVIC_SetVector(PendSV_IRQn,  (uint32_t)(void *)xPortPendSVHandler);
  __NVIC_SetVector(SysTick_IRQn, (uint32_t)(void *)xPortSysTickHandler);
}

/* vApplicationStackOverflowHook / vApplicationMallocFailedHook 은
 * Realtek 포트(port.c)가 갖고 있다. 여기에 또 두면 중복 정의가 된다. */
void rtosAssertFailed(const char *file, int line)
{
  logPrintf("\r\n[!] FreeRTOS assert : %s:%d\r\n", file, line);
  Error_Handler();
}

#endif
