#include "cli_mgr.h"


#ifdef _USE_HW_CLI


static uint8_t  cli_ch    = HW_UART_CH_CLI;
static uint32_t cli_baud  = 115200;

#ifdef _USE_HW_RTOS
static void cliMgrThread(void *arg);
#else
static bool     is_enable = true;
static void cliMgrUpdate(void const *arg);
#endif



bool cliMgrInit(void)
{
  bool ret;

  ret = cliOpen(cli_ch, cli_baud);
  cliBegin();

#ifdef _USE_HW_RTOS
  ret &= threadCreate("cli", cliMgrThread, NULL,
                      _HW_DEF_RTOS_THREAD_PRI_CLI, _HW_DEF_RTOS_THREAD_MEM_CLI);
#endif

  return ret;
}

#ifdef _USE_HW_RTOS

void cliMgrEnable(bool enable)
{
  (void)enable;
}

void cliMgrThread(void *arg)
{
  (void)arg;

  while (1)
  {
    cliMain();
    delay(1);
  }
}

MODULE_DEF(cli){
  .name     = "cli",
  .priority = MODULE_PRI_LOW,
  .init     = cliMgrInit,
};

#else

/* bare-metal 에서는 모듈 update 로 돈다. delay() 안에서 cliLoopIdle() 이
 * moduleUpdate() 를 재진입시키므로 그때는 스스로를 끈다. */
void cliMgrEnable(bool enable)
{
  is_enable = enable;
}

void cliMgrUpdate(void const *arg)
{
  (void)arg;

  if (is_enable)
  {
    cliMain();
  }
}

MODULE_DEF(cli){
  .name     = "cli",
  .priority = MODULE_PRI_LOW,
  .init     = cliMgrInit,
  .update   = cliMgrUpdate,
};

#endif

#endif
