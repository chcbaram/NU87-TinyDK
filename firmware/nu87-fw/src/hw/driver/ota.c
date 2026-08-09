/*
 * ota.c — 펌웨어 업데이트 (슬롯 관리와 플래시 쓰기)
 *
 * 플래시에 1MB 슬롯이 둘 있고 부트로더가 하나를 골라 부팅한다. 고르는 규칙은
 * 버전 비교가 아니라 존재 여부다 (bootloader/boot_flash_lp.c).
 *
 *   OTA2 가 유효하면 OTA2, 아니면 OTA1, 둘 다 아니면 정지
 *
 * "유효"는 슬롯 첫머리의 8바이트 이미지 시그니처로 판단한다. 그래서 전환은
 * 플래시가 1->0 만 가능하다는 성질을 그대로 쓴다.
 *
 *   1. 새 슬롯에 이미지를 쓴다. 단 시그니처는 비워 둔다
 *   2. 다 받고 검증되면 시그니처를 쓴다      <- 이 순간 새 슬롯이 유효해진다
 *   3. 옛 슬롯의 시그니처를 0 으로 덮는다     <- 지우기 없이 무효화
 *
 * 어느 지점에서 전원이 끊겨도 부팅 가능한 슬롯이 하나는 남는다.
 *
 * 지우기는 미리 하지 않고 쓰기 직전에 섹터 단위로 한다. 플래시 쓰기는
 * FLASH_Write_Lock() 안에서 돌아 그동안 인터럽트가 꺼지는데, 1MB 를 한 번에
 * 지우면 십수 초 동안 꺼져 있어 무선 링크가 끊긴다.
 */

#include "ota.h"


#ifdef _USE_HW_OTA
#include "cli.h"
#include "reset.h"


#define OTA_SLOT_ADDR_1       0x00006000        /* 물리 0x08006000 */
#define OTA_SLOT_ADDR_2       0x00106000
#define OTA_SLOT_SIZE         (1024 * 1024)

#define OTA_SECTOR_SIZE       4096
#define OTA_PAGE_SIZE         256

/* IMG2 시그니처 "81958711". 슬롯 첫 8바이트다. */
#define OTA_SIG_LEN           8

/* 시그니처를 보류해 두는 동안 쓰는 값. 지운 직후 상태 그대로다. */
#define OTA_SIG_BLANK         0xFF


static bool       is_init = false;
static ota_info_t info;

static uint8_t    sig_hold[OTA_SIG_LEN];
static uint32_t   erased_upto;      /* 이 오프셋 앞까지는 지워 두었다 */

static uint8_t  otaGetRunSlot(void);
static uint32_t otaSlotAddr(uint8_t slot);
static bool     otaEraseUpto(uint32_t offset);
static uint32_t otaCrc32(uint32_t addr, uint32_t length);

#ifdef _USE_HW_CLI
static void cliOta(cli_args_t *args);
#endif




bool otaInit(void)
{
  info.slot_run    = otaGetRunSlot();
  info.slot_target = info.slot_run ^ 1;
  info.addr_run    = otaSlotAddr(info.slot_run);
  info.addr_target = otaSlotAddr(info.slot_target);
  info.size_max    = OTA_SLOT_SIZE;
  info.is_busy     = false;
  info.written     = 0;

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("ota", cliOta);
#endif

  logPrintf("[OK] otaInit() : OTA%d 실행 중\n", info.slot_run + 1);
  return true;
}

bool otaGetInfo(ota_info_t *p_info)
{
  if (!is_init) return false;

  *p_info = info;
  return true;
}

bool otaBegin(uint32_t size)
{
  if (!is_init)                return false;
  if (size < OTA_SIG_LEN)      return false;
  if (size > info.size_max)    return false;

  info.is_busy = true;
  info.written = 0;
  erased_upto  = 0;

  memset(sig_hold, OTA_SIG_BLANK, OTA_SIG_LEN);
  return true;
}

bool otaWrite(uint8_t *p_data, uint32_t length)
{
  uint32_t offset = info.written;

  if (info.is_busy == false) return false;
  if (offset + length > info.size_max) return false;

  if (otaEraseUpto(offset + length) == false) return false;

  /* 시그니처는 아직 쓰지 않는다. 다 받고 검증한 뒤에야 유효해져야 한다. */
  while (length > 0 && offset < OTA_SIG_LEN)
  {
    sig_hold[offset] = *p_data;
    p_data++;
    offset++;
    length--;
  }

  while (length > 0)
  {
    uint32_t size = (length > OTA_PAGE_SIZE) ? OTA_PAGE_SIZE : length;

    /* 페이지 경계를 넘지 않게 자른다. */
    if ((offset % OTA_PAGE_SIZE) + size > OTA_PAGE_SIZE)
    {
      size = OTA_PAGE_SIZE - (offset % OTA_PAGE_SIZE);
    }

    FLASH_TxData256BXIP(info.addr_target + offset, size, p_data);

    p_data += size;
    offset += size;
    length -= size;
  }

  info.written = offset;
  return true;
}

bool otaEnd(uint32_t crc)
{
  uint32_t calc;

  if (info.is_busy == false) return false;

  /* 시그니처는 아직 플래시에 없으므로 그 부분만 보류분으로 대신 계산한다. */
  calc = otaCrc32(info.addr_target, info.written);

  if (calc != crc)
  {
    logPrintf("[E_] ota : crc 불일치  받은 0x%08X  계산 0x%08X\n",
              (unsigned int)crc, (unsigned int)calc);
    otaAbort();
    return false;
  }

  /* 여기부터가 전환이다. 순서를 지켜야 한다. */
  FLASH_TxData256BXIP(info.addr_target, OTA_SIG_LEN, sig_hold);

  {
    uint8_t empty[4] = {0, 0, 0, 0};

    FLASH_TxData256BXIP(info.addr_run, sizeof(empty), empty);
  }

  info.is_busy = false;
  logPrintf("[OK] ota : OTA%d 로 전환. 재부팅하면 적용된다\n", info.slot_target + 1);
  return true;
}

bool otaAbort(void)
{
  info.is_busy = false;
  info.written = 0;
  return true;
}

/* 지금 어느 슬롯에서 도는지는 부트로더가 건 Flash MMU 매핑이 알고 있다.
 * 가상주소가 어느 물리주소로 가는지 되읽으면 된다.
 *
 * SDK 의 ota_get_cur_index() 와 같은 계산이지만 직접 한다. 그 함수가 있는
 * misc/rtl8721d_ota.c 는 lwIP 와 FatFs 를 include 해서, 레지스터 두 개
 * 읽자고 네트워크 스택 전체를 끌어오게 된다. */
static uint8_t otaGetRunSlot(void)
{
  RSIP_REG_TypeDef *RSIP = ((RSIP_REG_TypeDef *)RSIP_REG_BASE);
  uint32_t ctrl = RSIP->FLASH_MMU[0].MMU_ENTRYx_CTRL;
  uint32_t phy;

  if ((ctrl & MMU_BIT_ENTRY_VALID) == 0) return 0;

  if (ctrl & MMU_BIT_ENTRY_OFFSET_MINUS)
    phy = RSIP->FLASH_MMU[0].MMU_ENTRYx_STRADDR - RSIP->FLASH_MMU[0].MMU_ENTRYx_OFFSET;
  else
    phy = RSIP->FLASH_MMU[0].MMU_ENTRYx_STRADDR + RSIP->FLASH_MMU[0].MMU_ENTRYx_OFFSET;

  return (phy == (SPI_FLASH_BASE + OTA_SLOT_ADDR_2)) ? 1 : 0;
}

static uint32_t otaSlotAddr(uint8_t slot)
{
  return (slot == 0) ? OTA_SLOT_ADDR_1 : OTA_SLOT_ADDR_2;
}

/* 필요한 만큼만 앞서서 지운다. 이미 지운 구간은 건너뛴다. */
static bool otaEraseUpto(uint32_t offset)
{
  while (erased_upto < offset)
  {
    FLASH_EraseXIP(EraseSector, info.addr_target + erased_upto);
    erased_upto += OTA_SECTOR_SIZE;
  }
  return true;
}

/* 플래시를 메모리맵으로 읽으며 계산한다. 시그니처 자리는 아직 비어 있으므로
 * 보류해 둔 값으로 바꿔 넣는다. 그래야 호스트가 원본으로 계산한 값과 맞는다. */
static uint32_t otaCrc32(uint32_t addr, uint32_t length)
{
  const volatile uint8_t *p = (const volatile uint8_t *)(SPI_FLASH_BASE + addr);
  uint32_t crc = 0xFFFFFFFF;

  for (uint32_t i = 0; i < length; i++)
  {
    uint8_t data = (i < OTA_SIG_LEN) ? sig_hold[i] : p[i];

    crc ^= data;
    for (int bit = 0; bit < 8; bit++)
    {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}


#ifdef _USE_HW_CLI
void cliOta(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("실행  : OTA%d  (0x%08X)\n", info.slot_run + 1,
              (unsigned int)(SPI_FLASH_BASE + info.addr_run));
    cliPrintf("대상  : OTA%d  (0x%08X)\n", info.slot_target + 1,
              (unsigned int)(SPI_FLASH_BASE + info.addr_target));
    cliPrintf("크기  : %d KB\n", (int)(info.size_max / 1024));
    cliPrintf("진행  : %s  %d B\n", info.is_busy ? "받는 중" : "대기",
              (int)info.written);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ota info\n");
  }
}
#endif

#endif
