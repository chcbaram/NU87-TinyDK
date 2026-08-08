#include "cli_mgr.h"


#ifdef _USE_HW_CLI


static uint8_t  cli_ch    = HW_UART_CH_CLI;
static uint32_t cli_baud  = 115200;
static bool     is_enable = true;



bool cliMgrInit(void)
{
  cliOpen(cli_ch, cli_baud);
  cliBegin();
  return true;
}

void cliMgrEnable(bool enable)
{
  is_enable = enable;
}

void cliMgrThread(void const *arg)
{
  if (is_enable)
  {
    cliMain();
  }
}

/* CLI 포트는 LOGUART 하나다. 포트가 늘어나면 이 모듈이 cliGetPort() 와 비교해
 * cliOpen()/logOpen() 으로 전환하는 역할을 맡는다. */

MODULE_DEF(cli){
  .name     = "cli",
  .priority = MODULE_PRI_LOW,
  .init     = cliMgrInit,
  .update   = cliMgrThread,
};

#endif
