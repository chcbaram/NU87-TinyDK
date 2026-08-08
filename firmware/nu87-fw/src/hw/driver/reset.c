/*
 * reset.c — 리셋 사유와 부팅 모드
 *
 * 부팅 모드는 백업 레지스터에 둔다. 시스템 리셋으로는 지워지지 않고
 * 파워오프/딥슬립에서만 지워지므로 "리셋해서 업데이트 모드로 올라오기" 에 맞는다.
 * 이 칩에는 핀 리셋을 알리는 플래그가 없어 CHIP_EN 리셋은 파워온으로 읽힌다.
 */

#include "reset.h"
#include "cli.h"

#ifdef _USE_HW_RESET


/* BKUP_REG0/6/7 은 시스템이 쓴다 */
#define BKUP_IDX_BOOT_MODE    BKUP_REG1

/* BOOT_Reason() 은 백업 레지스터 0 의 사유 비트를 이만큼 민 값을 준다.
 * SDK 헤더에 BIT_BOOT_REASON_SHIFT 정의가 빠져 있어 여기서 만든다. */
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

  /* 소프트 리셋이 워치독을 물려서 만들어지므로 SYS 를 먼저 본다 */
  if (reason & (BIT_SYS_RESET_HAPPEN | BIT_KM4SYS_RESET_HAPPEN))
  {
    reset_bits |= (1<<RESET_BIT_SOFT);
  }
  else if (reason & (BIT_WDG_RESET_HAPPEN | BIT_KM4WDG_RESET_HAPPEN))
  {
    reset_bits |= (1<<RESET_BIT_WDG);
  }
  else if (boot_reason != 0)
  {
    /* 시프트 영역 밖의 비트 = 딥슬립 복귀 / BOD */
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


  /* ★ NVIC_SystemReset() 을 쓰면 KM4 코어만 리셋되어 다시 올라오지 못한다.
   * KM4 이미지는 KM0 부트로더가 SRAM 으로 복사해 넘겨주기 때문이다.
   * 워치독으로 SoC 전체를 리셋하고, 사유 비트는 직접 표시한다. */
  BKUP_Set(BKUP_REG0, BIT_KM4SYS_RESET_HAPPEN);

  WDG_Scalar(50, &count_process, &div_fac_process);
  wdg_init.CountProcess  = count_process;
  wdg_init.DivFacProcess = div_fac_process;
  WDG_Init(&wdg_init);

  WDG_Cmd(ENABLE);

  while (1)
  {
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
