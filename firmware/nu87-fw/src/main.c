#include "main.h"



#ifdef _USE_HW_RTOS
static void mainThread(void *arg);


static void ledErrorBlink(void)
{
  ledInit();

  while (1)
  {
    ledOn(_DEF_LED1);
    delayUs(50 * 1000);
    ledOff(_DEF_LED1);
    delayUs(50 * 1000);
    ledOn(_DEF_LED1);
    delayUs(500 * 1000);
    ledOff(_DEF_LED1);
    delayUs(500 * 1000);
  }
}

int main(void)
{
  bspInit();

  if (xTaskCreate(mainThread,
                  "main",
                  _HW_DEF_RTOS_THREAD_MEM_MAIN / sizeof(StackType_t),
                  NULL,
                  _HW_DEF_RTOS_THREAD_PRI_MAIN,
                  NULL) != pdPASS)
  {
    ledErrorBlink();
  }

  vTaskStartScheduler();

  return 0;
}

void mainThread(void *arg)
{
  (void)arg;

  hwInit();
  apInit();
  apMain();
}
#else
int main(void)
{
  bspInit();

  hwInit();
  apInit();
  apMain();

  return 0;
}
#endif
