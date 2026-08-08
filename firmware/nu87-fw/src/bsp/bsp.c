/*
 * bsp.c — 플랫폼 시작과 시간 기반 (RTL8720DF / KM4)
 *
 * ap / common 계층은 이 파일이 제공하는 API 만 보고, Realtek SDK 심볼은 여기서 끝난다.
 *
 * SysTick 은 FreeRTOS 가 커널 틱으로 가져가므로 시간 기반은 TIM4 로 만든다.
 * TIM0~3 은 32비트지만 32kHz 라 us 분해능이 안 나온다.
 */

#include "bsp.h"
#include "cli.h"
#include "swtimer.h"


#define BSP_TIM             TIM4
#define BSP_TIM_IDX         4
#define BSP_TIM_IRQ         TIMER4_IRQ
#define BSP_TIM_CLOCK_HZ    40000000UL              /* XTAL */
#define BSP_TIM_TICK_HZ     1000000UL               /* 카운트 1 = 1us */
#define BSP_TIM_PRESCALER   ((BSP_TIM_CLOCK_HZ / BSP_TIM_TICK_HZ) - 1)
#define BSP_TIM_PERIOD      ((BSP_TIM_TICK_HZ / 1000UL) - 1)    /* 1ms 주기 */


static volatile uint32_t bsp_tick_ms = 0;
static uint32_t          bsp_cpu_hz  = 0;


static uint32_t bspTimHandler(void *data)
{
  (void)data;

  RTIM_INTClear(BSP_TIM);

  bsp_tick_ms++;

#ifdef _USE_HW_SWTIMER
  swtimerISR();
#endif

  return 0;
}

bool bspInit(void)
{
  RTIM_TimeBaseInitTypeDef tim_init;

  /* 캐시·클럭·MPU·벡터테이블은 진입점(nu87_app_start.c)이 이미 설정했다.
   * 여기서는 시간 기반과 인터럽트만 세운다. */

  SystemCoreClockUpdate();
  bsp_cpu_hz = SystemGetCpuClk();
  if (bsp_cpu_hz == 0)
  {
    bsp_cpu_hz = PLATFORM_CLOCK;      /* 조회 실패 시 설정값(200MHz) 사용 */
  }

  RCC_PeriphClockCmd(APBPeriph_GTIMER, APBPeriph_GTIMER_CLOCK, ENABLE);

  RTIM_TimeBaseStructInit(&tim_init);
  tim_init.TIM_Idx        = BSP_TIM_IDX;
  tim_init.TIM_Prescaler  = BSP_TIM_PRESCALER;
  tim_init.TIM_Period     = BSP_TIM_PERIOD;
  tim_init.TIM_UpdateEvent  = ENABLE;
  tim_init.TIM_UpdateSource = TIM_UpdateSource_Overflow;
  tim_init.TIM_ARRProtection = DISABLE;

  RTIM_TimeBaseInit(BSP_TIM, &tim_init, BSP_TIM_IRQ, (IRQ_FUN)bspTimHandler, 0);
  RTIM_INTConfig(BSP_TIM, TIM_IT_Update, ENABLE);

  /* RTIM_TimeBaseInit() 은 핸들러 등록만 하고 NVIC 는 켜지 않는다 */
  InterruptEn(BSP_TIM_IRQ, 10);

  RTIM_Cmd(BSP_TIM, ENABLE);

  /* KM4 부트로더가 PRIMASK=1 로 진입시킨다 */
  __enable_irq();

#ifdef _USE_HW_RTOS
  rtosInit();
#endif

  return true;
}

uint32_t millis(void)
{
  return bsp_tick_ms;
}

uint32_t micros(void)
{
  /* 두 벌 읽어 어긋나지 않은 쌍을 쓴다. 사이에 틱 인터럽트가 끼면 시간이 뒤로 간다. */
  uint32_t      m0 = bsp_tick_ms;
  __IO uint32_t u0 = RTIM_GetCount(BSP_TIM);
  uint32_t      m1 = bsp_tick_ms;
  __IO uint32_t u1 = RTIM_GetCount(BSP_TIM);

  if (m1 != m0)
  {
    return (m1 * 1000U) + u1;
  }

  return (m0 * 1000U) + u0;
}

void delayUs(uint32_t delay_us)
{
  /* ROM 의 캘리브레이션된 busy-wait */
  DelayUs(delay_us);
}

void delay(uint32_t time_ms)
{
  uint32_t pre_time;

#ifdef _USE_HW_RTOS
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    vTaskDelay(pdMS_TO_TICKS(time_ms));
    return;
  }
#endif

  pre_time = millis();

  /* cliLoopIdle() 이 moduleUpdate() 를 부르므로 모듈 그래프가 재진입한다.
   * delay() 를 쓰는 모듈 update 는 자신이 다시 불릴 수 있음을 전제해야 한다. */
  while (millis() - pre_time < time_ms)
  {
#ifdef _USE_HW_CLI
    cliLoopIdle();
#endif
  }
}

void Error_Handler(void)
{
  /* 디버거가 붙어 있으면 여기서 멈춘다 */
  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }

  __disable_irq();
  while (1)
  {
  }
}
