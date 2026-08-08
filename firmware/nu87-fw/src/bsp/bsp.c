/*
 * bsp.c — 플랫폼 시작과 시간 기반 (RTL8720DF / KM4)
 *
 * ap / common 계층은 이 파일이 제공하는 API 만 보고, Realtek SDK 심볼은 여기서 끝난다.
 *
 * 시간 기반은 SysTick 1kHz 하나로 만든다.
 *   millis()  SysTick 인터럽트가 올리는 카운터. 같은 인터럽트로 swtimer(1kHz)도 구동한다.
 *   micros()  ms 카운터 + SysTick 감소 카운터를 보간한다.
 */

#include "bsp.h"
#include "cli.h"
#include "swtimer.h"


static volatile uint32_t bsp_tick_ms = 0;
static uint32_t          bsp_cpu_hz  = 0;
static uint32_t          bsp_us_div  = 1;   /* SysTick 카운트 → us 변환용 (cpu_hz / 1MHz) */


static void bspSysTickHandler(void)
{
  bsp_tick_ms++;

#ifdef _USE_HW_SWTIMER
  swtimerISR();
#endif
}

bool bspInit(void)
{
  /* 캐시·클럭·MPU·벡터테이블은 진입점(nu87_app_start.c)이 이미 설정했다.
   * 여기서는 시간 기반과 인터럽트만 세운다. */

  SystemCoreClockUpdate();
  bsp_cpu_hz = SystemGetCpuClk();
  if (bsp_cpu_hz == 0)
  {
    bsp_cpu_hz = PLATFORM_CLOCK;      /* 조회 실패 시 설정값(200MHz) 사용 */
  }
  bsp_us_div = bsp_cpu_hz / 1000000U;
  if (bsp_us_div == 0)
  {
    bsp_us_div = 1;
  }

  /* SysTick 1kHz.
   * CMSIS 의 SysTick_Config() 는 __Vendor_SysTickConfig 설정 때문에 제공되지 않으므로
   * 레지스터를 직접 쓴다.
   * app_start() 에서 irq_table_init() 을 했으므로 벡터 설정이 동작한다. */
  __NVIC_SetVector(SysTick_IRQn, (uint32_t)(void *)bspSysTickHandler);
  NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);

  SysTick->LOAD = (bsp_cpu_hz / 1000U) - 1U;
  SysTick->VAL  = 0U;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk    /* 프로세서 클럭 */
                | SysTick_CTRL_TICKINT_Msk
                | SysTick_CTRL_ENABLE_Msk;

  /* KM4 부트로더는 PRIMASK=1 (인터럽트 전체 마스킹) 상태로 이미지에 진입한다.
   * 여기서 풀어야 SysTick 을 비롯한 모든 인터럽트가 동작한다. */
  __enable_irq();

  return true;
}

uint32_t millis(void)
{
  return bsp_tick_ms;
}

uint32_t micros(void)
{
  uint32_t ms;
  uint32_t val;

  /* SysTick 카운터는 리로드값에서 0 으로 감소한다.
   * 틱 경계에서 ms 와 VAL 이 어긋나는 것을 막으려고 두 번 읽어 비교한다. */
  do
  {
    ms  = bsp_tick_ms;
    val = SysTick->VAL;
  } while (ms != bsp_tick_ms);

  return (ms * 1000U) + ((SysTick->LOAD - val) / bsp_us_div);
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
