/*
 * ipc.c — KM0 <-> KM4 프로세서간 통신
 *
 * 채널 구성은 fwlib/usrcfg/rtl8721d_ipccfg.c 의 ipc_init_config[] 가 정하고
 * KM0 와 KM4 가 같은 테이블을 써야 한다. 그래서 KM0 이미지도 같은 SDK 커밋에서
 * 빌드한다 (firm-sdk/tools/build_km0.py).
 *
 * KM0 는 km4_boot_on() 에서 자기 쪽 ipc_table_init() 을 이미 끝낸 상태다.
 */

#include "ipc.h"


#ifdef _USE_HW_IPC
#include "cli.h"


#ifdef _USE_HW_CLI
static void cliIpc(cli_args_t *args);
#endif




bool ipcInit(void)
{
  InterruptRegister(IPC_INTHandler, IPC_IRQ, (uint32_t)IPCM0_DEV, 5);
  InterruptEn(IPC_IRQ, 5);

  ipc_table_init();

#ifdef _USE_HW_CLI
  cliAdd("ipc", cliIpc);
#endif

  logPrintf("[OK] ipcInit()\n");
  return true;
}

bool ipcSend(uint8_t ch, uint32_t msg)
{
  if (ch >= IPC_CH_MAX) return false;

  ipc_send_message(ch, msg);
  return true;
}

uint32_t ipcRecv(uint8_t ch)
{
  if (ch >= IPC_CH_MAX) return 0;

  return ipc_get_message(ch);
}

/*
 * 채널 0 은 SDK 셸이 KM0 <-> KM4 콘솔을 넘길 때 쓴다. 원본 구현은 LOGUART
 * 인터럽트를 자기 셸로 가져가므로 우리 CLI 와 충돌한다. 채널만 유효하게 두고
 * 아무것도 하지 않는다. SDK 셸을 들이게 되면 이 정의를 빼야 한다.
 */
void shell_switch_ipc_int(void *data, u32 irq_status, u32 chan_num)
{
  (void)data;
  (void)irq_status;
  (void)chan_num;
}


#ifdef _USE_HW_CLI
void cliIpc(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("CPU ID : %d (%s)\n", (int)IPC_CPUID(), IPC_CPUID() ? "KM4" : "KM0");

    /* USR 레지스터는 12 개뿐이다. 채널 11 이상은 공유 배열에 값을 넣고
     * 그 주소를 USR[11] 로 알려주는 방식이라 여기서는 보이지 않는다. */
    cliPrintf("\nch  KM4->KM0    KM0->KM4\n");
    for (int i = 0; i < IPC_HW_REG_MAX; i++)
    {
      cliPrintf("%2d  0x%08X  0x%08X%s\n", i,
                (unsigned int)IPCM4_DEV->IPCx_USR[i],
                (unsigned int)IPCM0_DEV->IPCx_USR[i],
                (i == 11) ? "   <- ch11+ 공유배열 주소" : "");
    }
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "send"))
  {
    uint8_t  ch  = (uint8_t)args->getData(1);
    uint32_t msg = (uint32_t)args->getData(2);

    if (ipcSend(ch, msg))
    {
      cliPrintf("ipc send ch%d : 0x%08X\n", ch, (unsigned int)msg);
    }
    else
    {
      cliPrintf("ch 범위 초과 (0~%d)\n", IPC_CH_MAX - 1);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ipc info\n");
    cliPrintf("ipc send ch[0~%d] data\n", IPC_CH_MAX - 1);
  }
}
#endif

#endif
