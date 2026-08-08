#include "ap.h"




void apInit(void)
{
  moduleInit();

#ifdef _USE_HW_THREAD
  threadBegin();
#endif
}

void apMain(void)
{
  while(1)
  {
    moduleUpdate();
#ifdef _USE_HW_RTOS
    delay(1);
#endif
  }
}

void updateLED(void)
{
  static uint32_t pre_time = 0;


  if (millis() - pre_time >= 500)
  {
    pre_time = millis();
    ledToggle(_DEF_LED1);
  }
}

void update(void const *arg)
{
  updateLED();
}

void cliLoopIdle(void)
{
  cliMgrEnable(false);
  moduleUpdate();
  cliMgrEnable(true);
}


MODULE_DEF(ap)
{
  .name     = "ap",
  .priority = MODULE_PRI_LOW,
  .update   = update,
};
