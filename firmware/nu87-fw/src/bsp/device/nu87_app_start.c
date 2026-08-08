/*
 * nu87_app_start.c — KM4 이미지 진입점 (RTL8720DF)
 *
 * KM4 부트로더가 .image2.entry.data 의 Img2EntryFun0 를 읽어 RamStartFun 으로 점프한다.
 * 진입 시점의 상태:
 *   - MSP 가 MSP_RAM_NS(0x10004000~0x10005000, 4K) 로 설정되어 있다
 *   - .ram_image2.{entry,text,data} 가 SRAM 에 로드되어 있다
 *   - BSS 는 아직 0 이 아니다
 *   - PRIMASK = 1 (인터럽트 전체 마스킹). bspInit() 에서 해제한다
 */

#include "ameba_soc.h"
#include "rtl8721d_system.h"   /* SystemSetCpuClk / SystemGetCpuClk */

/* C 표준 진입점. main() 은 src/main.c 에 있다.
 * bsp 계층이 main.h(→ hw.h, ap.h)를 include 하면 계층 방향이 뒤집히므로
 * 프로토타입만 둔다. */
int main(void);

/* newlib 이 제공한다 (__init_array 순회). 툴체인 함수라 헤더가 없다. */
void __libc_init_array(void);

/* 링커스크립트가 만드는 심볼(__bss_start__, __bss_end__,
 * __ram_nocache_start__, __ram_nocache_end__)은 rtl8721d_boot.h 가 선언한다. */


/* ──────────────────────────────────────────────────────────────────────────
 *  Fault 핸들러
 *
 *  스택 프레임에서 폴트 시점의 레지스터를 꺼내 LOGUART 로 덤프한다.
 *  ROM 기본 핸들러는 아무 정보 없이 멈춘다.
 * ────────────────────────────────────────────────────────────────────────── */
static const char *nu87FaultName(uint32_t id)
{
  switch (id)
  {
    case 3:  return "HardFault";
    case 4:  return "MemManage";
    case 5:  return "BusFault";
    case 6:  return "UsageFault";
    default: return "Fault";
  }
}

void nu87FaultHandlerC(uint32_t *msp, uint32_t *psp, uint32_t lr_value, uint32_t fault_id)
{
  /* EXC_RETURN bit2 가 1 이면 폴트 직전에 쓰던 스택은 PSP 다 */
  uint32_t *sp = (lr_value & 0x4) ? psp : msp;

  DiagPrintf("\r\n\r\n[ %s ]\r\n", nu87FaultName(fault_id));
  DiagPrintf("  R0  0x%08X   R1  0x%08X\r\n", sp[0], sp[1]);
  DiagPrintf("  R2  0x%08X   R3  0x%08X\r\n", sp[2], sp[3]);
  DiagPrintf("  R12 0x%08X   LR  0x%08X\r\n", sp[4], sp[5]);
  DiagPrintf("  PC  0x%08X   PSR 0x%08X\r\n", sp[6], sp[7]);
  DiagPrintf("  CFSR 0x%08X  HFSR 0x%08X\r\n", SCB->CFSR, SCB->HFSR);
  DiagPrintf("  MMFAR 0x%08X BFAR 0x%08X\r\n", SCB->MMFAR, SCB->BFAR);

  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }
  while (1)
  {
  }
}

/* 진입 시점의 MSP/PSP/LR 을 그대로 C 핸들러에 넘긴다.
 * MSP 를 128바이트 내려서 C 핸들러의 스택 사용이 예외 프레임을 덮지 않게 한다. */
#define NU87_FAULT_STUB(name, id)                       \
  static void __attribute__((naked)) name(void)         \
  {                                                     \
    __ASM volatile(                                     \
      "MRS  R0, MSP           \n\t"                     \
      "MRS  R1, PSP           \n\t"                     \
      "MOV  R2, LR            \n\t"                     \
      "MOV  R3, %0            \n\t"                     \
      "SUB.W R4, R0, #128     \n\t"                     \
      "MSR  MSP, R4           \n\t"                     \
      "LDR  R4, =nu87FaultHandlerC \n\t"                \
      "BX   R4                \n\t"                     \
      :: "i"(id) : "r0", "r1", "r2", "r3", "r4");       \
  }

NU87_FAULT_STUB(nu87HardFault,   3)
NU87_FAULT_STUB(nu87MemFault,    4)
NU87_FAULT_STUB(nu87BusFault,    5)
NU87_FAULT_STUB(nu87UsageFault,  6)

static void nu87VectorTableOverride(void)
{
  NewVectorTable[3] = (HAL_VECTOR_FUN)nu87HardFault;
  NewVectorTable[4] = (HAL_VECTOR_FUN)nu87MemFault;
  NewVectorTable[5] = (HAL_VECTOR_FUN)nu87BusFault;
  NewVectorTable[6] = (HAL_VECTOR_FUN)nu87UsageFault;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  MPU 논캐시 리전
 *
 *  캐시를 켜므로 DMA 버퍼처럼 캐시를 우회해야 하는 데이터는
 *  SRAM_NOCACHE_DATA_SECTION 으로 표시해 .ram_image2.nocache.data 에 넣고
 *  그 범위를 MPU 논캐시로 설정한다. 32바이트 미만이면 설정하지 않는다.
 * ────────────────────────────────────────────────────────────────────────── */
static void nu87MpuNoCacheInit(void)
{
  mpu_region_config mpu_cfg;
  uint32_t entry;

  uint32_t base = (uint32_t)__ram_nocache_start__;
  uint32_t size = (uint32_t)__ram_nocache_end__ - base;

  if (size < 32)
  {
    return;
  }

  entry = mpu_entry_alloc();

  mpu_cfg.region_base = base;
  mpu_cfg.region_size = size;
  mpu_cfg.xn          = MPU_EXEC_ALLOW;
  mpu_cfg.ap          = MPU_UN_PRIV_RW;
  mpu_cfg.sh          = MPU_NON_SHAREABLE;
  mpu_cfg.attr_idx    = MPU_MEM_ATTR_IDX_NC;

  mpu_region_cfg(entry, &mpu_cfg);
}

/* ──────────────────────────────────────────────────────────────────────────
 *  이미지 진입점
 * ────────────────────────────────────────────────────────────────────────── */
#ifdef _USE_HW_WIFI
/* 벤더 드라이버가 임계구역 진입/이탈을 이 맵으로 부른다.
 * 무선 라이브러리와 크립토 엔진이 참조하므로 반드시 채워야 한다. */
struct _driver_call_os_func_map driver_call_os_func_map;

static void nu87DriverOsFuncInit(void)
{
  driver_call_os_func_map.driver_enter_critical = vPortEnterCritical;
  driver_call_os_func_map.driver_exit_critical  = vPortExitCritical;
}
#endif

static void nu87AppStart(void)
{
  /* 비보안 벡터테이블 초기화. 이후 InterruptRegister() / __NVIC_SetVector() 가 동작한다 */
  irq_table_init(MSP_RAM_HP_NS);
  nu87VectorTableOverride();

  /* BSS clear. 이 전에는 전역 변수를 신뢰할 수 없다 */
  _memset((void *)__bss_start__, 0, (__bss_end__ - __bss_start__));

  Cache_Enable(ENABLE);

  /* CPU_CLOCK_SEL_VALUE 는 platform_autoconf.h 에서 0 (= 200MHz) */
  SystemSetCpuClk(CPU_CLOCK_SEL_VALUE);

  /* C++ 전역 생성자 / __init_array 순회 */
  __libc_init_array();

#ifdef _USE_HW_WIFI
  nu87DriverOsFuncInit();
#endif

  /* 진입 시 SP 가 4바이트 정렬일 수 있다. AAPCS 는 8바이트를 요구하고
   * 가변인자 함수(DiagPrintf 등)가 이를 전제한다. */
  __ASM volatile(
    "MOV R0, SP    \n\t"
    "BIC R0, R0, #7\n\t"
    "MOV SP, R0    \n\t"
    ::: "r0");

  mpu_init();
  nu87MpuNoCacheInit();

  main();

  /* apMain() 이 while(1) 이라 돌아오지 않는다 */
  while (1)
  {
  }
}

/* ──────────────────────────────────────────────────────────────────────────
 *  부트로더가 읽는 두 구조체
 *
 *  ★ 위치와 내용이 정확해야 한다. 틀리면 부팅하지 않는다.
 * ────────────────────────────────────────────────────────────────────────── */

/* 부트로더가 검증하는 이미지 유효성 패턴.
 * 링커스크립트 .ram_image2.entry 의 .image2.validate.rodata (이미지 +12) 에 놓인다. */
IMAGE2_VALID_PATTEN_SECTION
const u8 RAM_IMG2_VALID_PATTEN[20] = {
  'R', 'T', 'K', 'W', 'i', 'n', 0x0, 0xff,
  (FW_VERSION & 0xff),        ((FW_VERSION >> 8) & 0xff),
  (FW_SUBVERSION & 0xff),     ((FW_SUBVERSION >> 8) & 0xff),
  (FW_CHIP_ID & 0xff),        ((FW_CHIP_ID >> 8) & 0xff),
  (FW_CHIP_VER),
  (FW_BUS_TYPE),
  (FW_INFO_RSV1), (FW_INFO_RSV2), (FW_INFO_RSV3), (FW_INFO_RSV4)
};

/* 이미지 진입 테이블. 이미지 +0 에 놓인다.
 *   [0] RamStartFun   부트로더가 점프할 함수
 *   [1] RamWakeupFun  파워게이팅 복귀용 (미사용)
 *   [2] VectorNS      비보안 벡터테이블 주소 */
IMAGE2_ENTRY_SECTION
RAM_START_FUNCTION Img2EntryFun0 = {
  nu87AppStart,
  NULL,
  (u32)NewVectorTable
};
