#include "cli_mgr.h"


#ifdef _USE_HW_CLI
#include "driver/cli_net.h"


#define CLI_NET_PORT      23


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

#ifdef _USE_HW_WIFI
  cliNetInit(CLI_NET_PORT);
#endif

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

#ifdef _USE_HW_WIFI
    /* 채널 선택. 텔넷이 붙으면 그쪽으로 넘기되, 로컬 UART 에 입력이 들어오면
     * 언제든 되돌아온다. 원격에 물려 있어도 콘솔을 잃지 않는다. */
    cliNetPoll();

    if (cliNetIsConnected())
    {
      cli_ch = HW_UART_CH_NET;
    }
    else if (cli_ch == HW_UART_CH_NET)
    {
      cli_ch = HW_UART_CH_CLI;
    }

    if (uartAvailable(HW_UART_CH_CLI) > 0)
    {
      cli_ch = HW_UART_CH_CLI;
    }

    if (cliGetPort() != cli_ch)
    {
      cliOpen(cli_ch, cli_baud);
      logOpen(cli_ch, cli_baud);
    }
#endif

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
