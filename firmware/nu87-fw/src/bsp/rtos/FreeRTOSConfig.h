#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "platform_autoconf.h"

#ifndef __ASSEMBLER__
extern uint32_t SystemCoreClock;
#endif


#define configENABLE_FPU                            1
#define configENABLE_MPU                            0
#define configENABLE_TRUSTZONE                      0
#define configRUN_FREERTOS_SECURE_ONLY              0

#define configUSE_PREEMPTION                        1
#define configUSE_TIME_SLICING                      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION     0
#define configMAX_PRIORITIES                        ( 11 )
#define configIDLE_SHOULD_YIELD                     0
#define configUSE_16_BIT_TICKS                      0
#define configTICK_RATE_HZ                          ( ( TickType_t ) 1000 )

/* Realtek 이 wlan 태스크 우선순위를 올릴 때 쓰는 오프셋 */
#define PRIORITIE_OFFSET                            ( 4 )

#define configCPU_CLOCK_HZ                          SystemCoreClock
#define configMINIMAL_STACK_SIZE                    ( ( unsigned short ) 512 )
#define configMAX_TASK_NAME_LEN                     ( 16 )

/* heap_5 리전 크기. rtos.c 가 이 크기의 정적 배열을 등록한다. */
#define configTOTAL_HEAP_SIZE                       ( ( size_t ) ( 128 * 1024 ) )

#define configUSE_MUTEXES                           1
#define configUSE_RECURSIVE_MUTEXES                 1
#define configUSE_COUNTING_SEMAPHORES               1
#define configUSE_QUEUE_SETS                        1
#define configUSE_TASK_NOTIFICATIONS                1
#define configUSE_APPLICATION_TASK_TAG              1
#define configUSE_NEWLIB_REENTRANT                  0
#define configUSE_CO_ROUTINES                       0
#define configMAX_CO_ROUTINE_PRIORITIES             ( 2 )

#define configUSE_TRACE_FACILITY                    1
#define configUSE_STATS_FORMATTING_FUNCTIONS        1
#define configRECORD_STACK_HIGH_ADDRESS             1
#define configQUEUE_REGISTRY_SIZE                   0

#define configUSE_IDLE_HOOK                         0
#define configUSE_TICK_HOOK                         0
#define configUSE_MALLOC_FAILED_HOOK                1
#define configCHECK_FOR_STACK_OVERFLOW              2

#define configUSE_TIMERS                            1
#define configTIMER_TASK_PRIORITY                   1
#define configTIMER_QUEUE_LENGTH                    ( 10 )
#define configTIMER_TASK_STACK_DEPTH                ( 512 )

/* 켜면 freertos_pre/post_sleep_processing() 과 vPortSuppressTicksAndSleep() 이
 * 필요하다. 그 구현은 KM0 와 IPC 로 물린 PMU 코드에 있어 무선 단계에서 들어온다. */
#define configUSE_TICKLESS_IDLE                     0

/* 카운터는 bsp 의 TIM4 를 나눠 쓴다 (micros 를 16 분주, 약 62.5kHz).
 * 틱(1kHz)보다 충분히 빠르면서 u32 이 19 시간까지 버틴다.
 * thread cpu 는 두 스냅샷의 차이로 계산하므로 오버플로해도 무방하다. */
#define configGENERATE_RUN_TIME_STATS               1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()            bspGetRunTimeCounter()

#ifndef __ASSEMBLER__
uint32_t bspGetRunTimeCounter(void);
#endif

#define INCLUDE_vTaskPrioritySet                    1
#define INCLUDE_uxTaskPriorityGet                   1
#define INCLUDE_vTaskDelete                         1
#define INCLUDE_vTaskCleanUpResources               0
#define INCLUDE_vTaskSuspend                        1
#define INCLUDE_vTaskDelayUntil                     1
#define INCLUDE_vTaskDelay                          1
#define INCLUDE_pcTaskGetTaskName                   1
#define INCLUDE_uxTaskGetStackSize                  1
#define INCLUDE_uxTaskGetFreeStackSize              1
#define INCLUDE_uxTaskGetStackHighWaterMark         1
#define INCLUDE_xTaskGetIdleTaskHandle              1
#define INCLUDE_eTaskGetState                       1
#define INCLUDE_xTaskResumeFromISR                  0
#define INCLUDE_xTaskGetCurrentTaskHandle           1
#define INCLUDE_xSemaphoreGetMutexHolder            1
#define INCLUDE_xTimerPendFunctionCall              1
#define INCLUDE_xTaskGetSchedulerState              1

#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS                           __NVIC_PRIO_BITS
#else
  #define configPRIO_BITS                           3
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY     7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
          ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
          ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

#define vPortSVCHandler                             SVC_Handler
#define xPortPendSVHandler                          PendSV_Handler
#define xPortSysTickHandler                         SysTick_Handler

#define configASSERT( x ) \
          do { if (!(x)) { rtosAssertFailed(__FILE__, __LINE__); } } while (0)

#ifndef __ASSEMBLER__
void rtosAssertFailed(const char *file, int line);
#endif

#endif /* FREERTOS_CONFIG_H */
