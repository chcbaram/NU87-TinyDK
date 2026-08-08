#include "hw.h"


/* 이미지가 로드된 시작 주소. 링커스크립트가 만드는 심볼이다. */
extern uint8_t __ram_image2_text_start__[];


/* 펌웨어 식별 정보. 이미지 시작에서 고정 오프셋 +32 에 놓인다.
 *
 * 부트로더나 호스트 툴이 펌웨어를 실행하지 않고 버전을 읽을 수 있어야 하기 때문이다.
 * OTA 슬롯 검증, 다운그레이드 방지, 업데이트 전 버전 비교에 쓴다.
 * 배치는 src/bsp/ldscript/nu87_km4_img2.ld 에서
 * '. = __ram_image2_text_start__ + 32' 로 고정한다.
 *
 * used 속성은 static 이고 참조가 없어도 컴파일러가 버리지 않게 한다.
 * 링커스크립트의 KEEP() 과 짝을 이룬다. */
static const firm_ver_t firm_ver __attribute__((used, section(".version"))) =
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)__ram_image2_text_start__
};



bool hwInit(void)
{
  cliInit();
  logInit();
  swtimerInit();
  ledInit();
  uartInit();

  for (int i = 0; i < HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", firm_ver.name_str);
  logPrintf("Booting..Ver  \t\t: %s\r\n", firm_ver.version_str);
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)(SystemGetCpuClk() / 1000000));
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__);
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__);
  logPrintf("Booting..Reason\t\t: 0x%X\r\n", (unsigned int)BOOT_Reason());
  logPrintf("\n");

  logBoot(false);

  return true;
}
