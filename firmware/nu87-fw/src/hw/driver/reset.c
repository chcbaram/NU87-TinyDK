/*
 * reset.c — 리셋 사유와 부팅 모드 (RTL8720DF)
 *
 * 리셋 사유
 *   BOOT_Reason() 은 백업 레지스터 0 의 사유 비트를 16 비트 왼쪽으로 민 값을 준다.
 *   부트로더(boot_flash_hp.c)의 다음 두 줄이 그것이다:
 *       tmp_reason = BACKUP_REG->DWORD[0] & BIT_MASK_BOOT_REASON;
 *       tmp_reason = tmp_reason << BIT_BOOT_REASON_SHIFT;
 *   위쪽에 딥슬립/BOD 비트를 따로 OR 해서 넘긴다.
 *
 *   되돌린 뒤의 비트는 백업 레지스터 정의와 같다:
 *     BIT_SYS_RESET_HAPPEN(0)      KM0 시스템 리셋
 *     BIT_WDG_RESET_HAPPEN(1)      KM0 워치독
 *     BIT_KM4SYS_RESET_HAPPEN(3)   KM4 시스템 리셋
 *     BIT_KM4WDG_RESET_HAPPEN(4)   KM4 워치독
 *
 *   전부 0 이면 파워온이다. 이 칩에는 핀 리셋을 따로 알리는 플래그가 없다.
 *   CHIP_EN 을 당기면 파워온과 같은 경로를 타므로 RESET_BIT_PIN 은 세우지 않는다.
 *
 * 부팅 모드
 *   백업 레지스터 1 에 둔다. BKUP_REG1~5 는 사용자 몫이다.
 *   이 레지스터는 CPU/시스템 리셋으로는 지워지지 않고 파워오프와 딥슬립에서만
 *   지워진다. "리셋해서 업데이트 모드로 올라오기" 에 필요한 성질이다.
 *
 *   읽은 뒤 바로 0 으로 지운다. 모드는 한 번만 소비되어야 하기 때문이다.
 *
 *   ROM 부트로더도 같은 방식으로 백업 레지스터 0 의 BIT_UARTBURN_BOOT 를 보고
 *   UART 다운로드 모드로 들어간다. 우리 부팅 모드와는 별개의 레지스터다.
 */

#include "reset.h"
#include "cli.h"

#ifdef _USE_HW_RESET


/* 부팅 모드를 담는 백업 레지스터. 0/6/7 은 시스템이 쓰므로 건드리지 않는다. */
#define BKUP_IDX_BOOT_MODE    BKUP_REG1

/* 이 SDK 스냅샷에는 BIT_BOOT_* 와 BIT_BOOT_REASON_SHIFT 정의가 빠져 있다.
 * 부트로더 소스는 쓰는데 헤더에 없다. 실측으로 확인한 값이다:
 *   BKUP_Set(BKUP_REG0, BIT_KM4SYS_RESET_HAPPEN) = BIT(3) 후 리셋
 *   -> BOOT_Reason() == 0x00080000 = BIT(19) = BIT(3) << 16 */
#define BOOT_REASON_SHIFT     16


static void cliReset(cli_args_t *args);
static void resetToUpdate(void);


static bool     is_init     = false;
static uint32_t reset_bits  = 0;
static uint32_t boot_mode   = 0;
static uint32_t boot_reason = 0;    // BOOT_Reason() 원본. 진단용으로 남긴다.


static const char *reset_bit_str[RESET_BIT_MAX] =
  {
    "RESET_BIT_POWER",
    "RESET_BIT_PIN",
    "RESET_BIT_WDG",
    "RESET_BIT_SOFT",
    "RESET_BIT_ETC",
  };

static const char *mode_bit_str[MODE_BIT_MAX] =
  {
    "MODE_BIT_BOOT",
    "MODE_BIT_UPDATE",
  };




bool resetInit(void)
{
  uint32_t reason;


  boot_reason = BOOT_Reason();
  reason = (boot_reason >> BOOT_REASON_SHIFT) & BIT_MASK_BOOT_REASON;

  if (reason & (BIT_SYS_RESET_HAPPEN | BIT_KM4SYS_RESET_HAPPEN))
  {
    /* 부트로더는 SYS 와 WDG 가 함께 서면 WDG 를 지운다. 소프트 리셋이
     * 워치독을 물려서 만들어지기 때문이다(resetToReset 참고). SYS 를 먼저 본다. */
    reset_bits |= (1<<RESET_BIT_SOFT);
  }
  else if (reason & (BIT_WDG_RESET_HAPPEN | BIT_KM4WDG_RESET_HAPPEN))
  {
    reset_bits |= (1<<RESET_BIT_WDG);
  }
  else if (boot_reason != 0)
  {
    /* 시프트 영역 밖의 비트. 딥슬립 복귀나 BOD 다. */
    reset_bits |= (1<<RESET_BIT_ETC);
  }
  else
  {
    reset_bits |= (1<<RESET_BIT_POWER);
  }

  boot_mode = BKUP_Read(BKUP_IDX_BOOT_MODE);
  BKUP_Write(BKUP_IDX_BOOT_MODE, 0);

  logPrintf("[OK] resetInit()\n");
  for (int i=0; i<RESET_BIT_MAX; i++)
  {
    if (reset_bits & (1<<i))
    {
      logPrintf("     %s\n", reset_bit_str[i]);
    }
  }
  for (int i=0; i<MODE_BIT_MAX; i++)
  {
    if (boot_mode & (1<<i))
    {
      logPrintf("     %s\n", mode_bit_str[i]);
    }
  }

  is_init = true;
  cliAdd("reset", cliReset);

  return is_init;
}

void resetLog(void)
{

}

void resetToBoot(void)
{
  resetSetBootMode(1<<MODE_BIT_BOOT);
  resetToReset();
}

void resetToReset(void)
{
  WDG_InitTypeDef wdg_init;
  uint32_t count_process;
  uint32_t div_fac_process;


  /* NVIC_SystemReset() 을 쓰면 안 된다. KM4 코어만 리셋되고 KM0 는 계속 도는데,
   * KM4 이미지는 KM0 부트로더가 SRAM 으로 복사해 넘겨주는 구조라 코어만 리셋하면
   * 다시 올라오지 못하고 멈춘다. SDK 의 ota_platform_reset() 도 같은 이유로
   * NVIC_SystemReset() 을 주석 처리하고 워치독으로 SoC 전체를 리셋한다.
   *
   * 워치독 리셋은 사유 비트를 세워 주지 않으므로 직접 표시한다. */
  BKUP_Set(BKUP_REG0, BIT_KM4SYS_RESET_HAPPEN);

  WDG_Scalar(50, &count_process, &div_fac_process);
  wdg_init.CountProcess  = count_process;
  wdg_init.DivFacProcess = div_fac_process;
  WDG_Init(&wdg_init);

  WDG_Cmd(ENABLE);

  while (1)
  {
    /* 50ms 뒤 워치독이 문다. */
  }
}

uint32_t resetGetBits(void)
{
  return reset_bits;
}

void resetSetBits(uint32_t data)
{
  reset_bits = data;
}

void resetSetBootMode(uint32_t data)
{
  boot_mode = data;
  BKUP_Write(BKUP_IDX_BOOT_MODE, data);
}

uint32_t resetGetBootMode(void)
{
  return boot_mode;
}

void resetToUpdate(void)
{
  resetSetBootMode(1<<MODE_BIT_UPDATE);
  resetToReset();
}


void cliReset(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("Boot Reason : 0x%08X\n", (unsigned int)boot_reason);

    cliPrintf("Reset Bits\n");
    for (int i=0; i<RESET_BIT_MAX; i++)
    {
      if (reset_bits & (1<<i))
      {
        cliPrintf("      %s\n", reset_bit_str[i]);
      }
    }

    cliPrintf("Boot Mode\n");
    for (int i=0; i<MODE_BIT_MAX; i++)
    {
      if (boot_mode & (1<<i))
      {
        cliPrintf("      %s\n", mode_bit_str[i]);
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    resetToBoot();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "update"))
  {
    resetToUpdate();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    resetToReset();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("reset info\n");
    cliPrintf("reset boot\n");
    cliPrintf("reset update\n");
    cliPrintf("reset reset\n");
  }
}


#endif
