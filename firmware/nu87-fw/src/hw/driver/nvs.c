/*
 * nvs.c — 이름으로 찾는 설정 저장소
 *
 * 4KB 섹터 두 개를 번갈아 쓴다(핑퐁). 저장할 때마다 쓰지 않은 쪽을 지우고
 * 순번을 올려 기록하므로, 쓰는 도중 전원이 끊겨도 직전 것이 살아남는다.
 * 마모도 두 섹터에 나뉜다.
 *
 * FTL 대신 ROM 플래시 API 를 직접 쓴다. FTL 은 mbed HAL(flash_api, objects.h,
 * device.h ...) 을 끌어오는데 우리가 필요한 것은 섹터 하나 읽고 쓰는 것뿐이다.
 *
 * FLASH_EraseXIP / FLASH_TxData256BXIP 는 IMAGE2_RAM_TEXT_SECTION 이라
 * SRAM 에서 실행된다. XIP 코드에서 불러도 안전하다.
 */

#include "nvs.h"


#ifdef _USE_HW_NVS
#include "cli.h"


#define NVS_MAGIC           0x4E56534DUL      /* "NVSM" */
#define NVS_SLOT_MAX        16
#define NVS_NAME_MAX        16
#define NVS_DATA_MAX        128

#define NVS_SECTOR_SIZE     4096
#define NVS_SECTOR_A        HW_NVS_FLASH_OFFSET
#define NVS_SECTOR_B        (HW_NVS_FLASH_OFFSET + NVS_SECTOR_SIZE)


typedef struct
{
  char     name[NVS_NAME_MAX];
  uint32_t length;              /* 워드 단위로 읽으므로 슬롯 크기도 4의 배수여야 한다 */
  uint8_t  data[NVS_DATA_MAX];
} nvs_slot_t;

typedef struct
{
  uint32_t   magic;
  uint32_t   seq;           /* 큰 쪽이 최신 */
  uint32_t   count;
  uint32_t   crc;           /* magic~slot 까지의 합 */
  nvs_slot_t slot[NVS_SLOT_MAX];
} nvs_table_t;


static bool        is_init  = false;
static uint32_t    cur_addr = NVS_SECTOR_A;
static nvs_table_t nvs_table;


static int      nvsFind(const char *p_name);
static uint32_t nvsCalcCrc(nvs_table_t *p_table);
static bool     nvsReadSector(uint32_t addr, nvs_table_t *p_table);
static bool     nvsLoad(void);
static bool     nvsSave(void);

#ifdef _USE_HW_CLI
static void cliNvs(cli_args_t *args);
#endif




bool nvsInit(void)
{
  if (sizeof(nvs_table_t) > NVS_SECTOR_SIZE)
  {
    logPrintf("[E_] nvsInit() : 테이블이 섹터보다 크다\n");
    return false;
  }

  is_init = true;

  if (nvsLoad() == false)
  {
    memset(&nvs_table, 0, sizeof(nvs_table));
    nvs_table.magic = NVS_MAGIC;
    nvs_table.seq   = 0;
    cur_addr = NVS_SECTOR_B;      /* 첫 저장이 A 로 가도록 */
    nvsSave();
  }

#ifdef _USE_HW_CLI
  cliAdd("nvs", cliNvs);
#endif

  logPrintf("[OK] nvsInit()\n");
  return true;
}

bool nvsIsInit(void)
{
  return is_init;
}

bool nvsIsExist(const char *p_name)
{
  return (nvsFind(p_name) >= 0);
}

bool nvsSet(const char *p_name, void *p_data, uint32_t length)
{
  int index;

  if (!is_init) return false;
  if (length > NVS_DATA_MAX) return false;
  if (strlen(p_name) >= NVS_NAME_MAX) return false;

  index = nvsFind(p_name);
  if (index < 0)
  {
    if (nvs_table.count >= NVS_SLOT_MAX) return false;
    index = nvs_table.count;
    nvs_table.count++;
    memset(nvs_table.slot[index].name, 0, NVS_NAME_MAX);
    strncpy(nvs_table.slot[index].name, p_name, NVS_NAME_MAX - 1);
  }

  memset(nvs_table.slot[index].data, 0, NVS_DATA_MAX);
  memcpy(nvs_table.slot[index].data, p_data, length);
  nvs_table.slot[index].length = length;

  return nvsSave();
}

bool nvsGet(const char *p_name, void *p_data, uint32_t length)
{
  int index;

  if (!is_init) return false;

  index = nvsFind(p_name);
  if (index < 0) return false;
  if (length > nvs_table.slot[index].length) return false;

  memcpy(p_data, nvs_table.slot[index].data, length);
  return true;
}

static int nvsFind(const char *p_name)
{
  for (uint32_t i = 0; i < nvs_table.count && i < NVS_SLOT_MAX; i++)
  {
    if (strncmp(nvs_table.slot[i].name, p_name, NVS_NAME_MAX) == 0)
    {
      return (int)i;
    }
  }
  return -1;
}

static uint32_t nvsCalcCrc(nvs_table_t *p_table)
{
  const uint8_t *p = (const uint8_t *)p_table;
  uint32_t sum = 0;

  /* crc 필드 자체는 뺀다 */
  for (uint32_t i = 0; i < offsetof(nvs_table_t, crc); i++)
  {
    sum += p[i];
  }
  for (uint32_t i = offsetof(nvs_table_t, slot); i < sizeof(nvs_table_t); i++)
  {
    sum += p[i];
  }
  return sum;
}

static bool nvsReadSector(uint32_t addr, nvs_table_t *p_table)
{
  /* 플래시 전체가 SPI_FLASH_BASE 에 그대로 보인다. 이 읽기는 XIP 코드 인출과
   * 같은 auto 모드라 락이 필요없다. 반대로 FLASH_RxData() 는 SPIC 를 user
   * 모드로 돌리므로 XIP 에서 부르면 인출이 끊겨 그대로 멈춘다.
   * 쓰기 직후의 캐시 정합은 FLASH_EraseXIP/FLASH_TxData256BXIP 가 맞춰준다. */
  const volatile uint32_t *p_src = (const volatile uint32_t *)(SPI_FLASH_BASE + addr);
  uint32_t *p_dst = (uint32_t *)p_table;

  for (uint32_t i = 0; i < sizeof(nvs_table_t) / 4; i++)
  {
    p_dst[i] = p_src[i];
  }

  if (p_table->magic != NVS_MAGIC)        return false;
  if (p_table->count > NVS_SLOT_MAX)      return false;
  if (p_table->crc != nvsCalcCrc(p_table)) return false;

  return true;
}

/* 테이블은 2KB 가 넘는다. 두 섹터를 스택에 올려놓고 비교하면 스레드 스택을
 * 넘기므로, 하나씩 static 테이블로 읽어들이고 순번만 기억한다. */
static bool nvsLoad(void)
{
  uint32_t seq_a = 0;
  bool     ok_a  = nvsReadSector(NVS_SECTOR_A, &nvs_table);

  if (ok_a) seq_a = nvs_table.seq;

  if (nvsReadSector(NVS_SECTOR_B, &nvs_table))
  {
    if (ok_a == false || nvs_table.seq >= seq_a)
    {
      cur_addr = NVS_SECTOR_B;
      return true;
    }
  }

  if (ok_a)
  {
    nvsReadSector(NVS_SECTOR_A, &nvs_table);
    cur_addr = NVS_SECTOR_A;
    return true;
  }

  return false;
}

static bool nvsSave(void)
{
  uint32_t addr = (cur_addr == NVS_SECTOR_A) ? NVS_SECTOR_B : NVS_SECTOR_A;
  const uint8_t *p = (const uint8_t *)&nvs_table;
  uint32_t left = sizeof(nvs_table_t);
  uint32_t off  = 0;

  nvs_table.seq++;
  nvs_table.crc = nvsCalcCrc(&nvs_table);

  FLASH_EraseXIP(EraseSector, addr);

  /* 한 번에 256 바이트씩. 페이지 경계를 넘지 않는다. */
  while (left > 0)
  {
    uint32_t len = (left > 256) ? 256 : left;

    FLASH_TxData256BXIP(addr + off, len, (uint8_t *)(p + off));
    off  += len;
    left -= len;
  }

  /* 버퍼를 또 잡지 않고 플래시와 메모리를 바로 대조한다. */
  {
    const volatile uint32_t *p_flash = (const volatile uint32_t *)(SPI_FLASH_BASE + addr);
    const uint32_t          *p_ram   = (const uint32_t *)&nvs_table;

    for (uint32_t i = 0; i < sizeof(nvs_table_t) / 4; i++)
    {
      if (p_flash[i] != p_ram[i]) return false;
    }
  }

  cur_addr = addr;
  return true;
}


#ifdef _USE_HW_CLI
void cliNvs(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("init  : %s\n", is_init ? "True" : "False");
    cliPrintf("flash : 0x%08X / 0x%08X  (%d B x 2)\n",
              (unsigned int)(0x08000000 + NVS_SECTOR_A),
              (unsigned int)(0x08000000 + NVS_SECTOR_B), NVS_SECTOR_SIZE);
    cliPrintf("사용중: %s   seq %d\n",
              (cur_addr == NVS_SECTOR_A) ? "A" : "B", (int)nvs_table.seq);
    cliPrintf("slot  : %d / %d   (이름 %d B, 값 %d B, 테이블 %d B)\n",
              (int)nvs_table.count, NVS_SLOT_MAX, NVS_NAME_MAX, NVS_DATA_MAX,
              (int)sizeof(nvs_table_t));

    for (uint32_t i = 0; i < nvs_table.count && i < NVS_SLOT_MAX; i++)
    {
      cliPrintf("  %-16s %3d B\n", nvs_table.slot[i].name,
                (int)nvs_table.slot[i].length);
    }
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "set"))
  {
    const char *name = args->getStr(1);
    const char *val  = args->getStr(2);

    cliPrintf("nvs set %s : %s\n", name,
              nvsSet(name, (void *)val, strlen(val) + 1) ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get"))
  {
    char buf[NVS_DATA_MAX] = {0};
    const char *name = args->getStr(1);
    int index = nvsFind(name);

    if (index < 0)
    {
      cliPrintf("없는 이름이다\n");
    }
    else
    {
      nvsGet(name, buf, nvs_table.slot[index].length);
      cliPrintf("nvs get %s : %s\n", name, buf);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "erase"))
  {
    memset(nvs_table.slot, 0, sizeof(nvs_table.slot));
    nvs_table.count = 0;
    cliPrintf("nvs erase : %s\n", nvsSave() ? "OK" : "Fail");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("nvs info\n");
    cliPrintf("nvs set name value\n");
    cliPrintf("nvs get name\n");
    cliPrintf("nvs erase\n");
  }
}
#endif

#endif
