/*
 * bsp.c — 플랫폼 시작과 시간 기반 (RTL8720DF / KM4)
 *
 * ap / common 계층은 이 파일이 제공하는 API 만 보고, Realtek SDK 심볼은 여기서 끝난다.
 *
 * 시간 기반은 TIM4 하나로 만든다.
 *   1MHz 로 돌리고 1000 카운트마다 업데이트 인터럽트를 낸다.
 *   millis()  인터럽트가 올리는 ms 카운터. 같은 인터럽트로 swtimer(1kHz)도 구동한다.
 *   micros()  ms 카운터 * 1000 + 타이머 카운트. 분해능이 그대로 1us 다.
 *
 * ★ SysTick 을 쓰지 않는 이유
 *   FreeRTOS 가 SysTick 을 커널 틱으로 가져간다. 시간 기반이 SysTick 에 묶여 있으면
 *   RTOS 를 올리는 순간 millis()/micros()/swtimer 가 전부 깨진다.
 *   STM32 프로젝트가 HAL 틱을 TIM6 로 옮긴 것과 같은 이유다.
 *   하드웨어 타이머라 스케줄러가 돌기 전에도, 정지해도 계속 진행한다.
 *
 * 타이머 선택
 *   TIM0~3  32비트지만 클럭이 32kHz 뿐이라 us 분해능이 안 나온다
 *   TIM4    40MHz XTAL / 16비트 / 8비트 프리스케일러  ← 이것을 쓴다
 *   TIM5    TIM4 와 같은 사양이지만 PWM 출력용으로 남겨 둔다
 */

#include "bsp.h"
#include "cli.h"
#include "swtimer.h"


/* TIM4 를 1MHz 로 돌리고 1ms 마다 업데이트 인터럽트를 낸다.
 * XTAL 40MHz / (39+1) = 1MHz, 1000 카운트 = 1ms */
#define BSP_TIM             TIM4
#define BSP_TIM_IDX         4
#define BSP_TIM_IRQ         TIMER4_IRQ
#define BSP_TIM_PRESCALER   (40 - 1)
#define BSP_TIM_PERIOD      (1000 - 1)


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

  /* TIM4 를 1MHz / 1ms 주기로 세운다.
   * RTIM_TimeBaseInit() 이 인터럽트 핸들러 등록과 NVIC 설정까지 한다.
   * app_start() 에서 irq_table_init() 을 했으므로 동적 등록이 동작한다. */
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

  /* RTIM_TimeBaseInit() 은 핸들러를 등록만 하고 NVIC 를 켜지 않는다.
   * 이것을 빼면 타이머는 도는데 SR 의 업데이트 플래그만 걸린 채 남고
   * 인터럽트가 오지 않아 millis() 가 0 에서 멈춘다. */
  InterruptEn(BSP_TIM_IRQ, 10);

  RTIM_Cmd(BSP_TIM, ENABLE);

  /* KM4 부트로더는 PRIMASK=1 (인터럽트 전체 마스킹) 상태로 이미지에 진입한다.
   * 여기서 풀어야 타이머를 비롯한 모든 인터럽트가 동작한다. */
  __enable_irq();

  return true;
}

uint32_t millis(void)
{
  return bsp_tick_ms;
}

uint32_t micros(void)
{
  /* ms 카운터와 타이머 카운트를 두 벌 읽어 서로 어긋나지 않은 쌍을 쓴다.
   * 두 값 사이에 업데이트 인터럽트가 끼어들면 ms 는 올라갔는데 카운트는
   * 이전 주기 값이어서 시간이 뒤로 가는 일이 생긴다.
   *
   * 타이머가 이미 1MHz 라 카운트가 그대로 us 다. 스케일링이 필요 없다. */
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
  uint32_t pre_time = millis();

  /* 대기하는 동안 CLI 를 계속 돌려 블로킹 delay 안에서도 콘솔이 살아 있게 한다.
   *
   * 주의: cliLoopIdle() 이 moduleUpdate() 를 호출하므로 모듈 그래프가 재진입한다.
   * delay() 를 호출하는 모듈 update 는 자신이 다시 불릴 수 있음을 전제해야 한다. */
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
