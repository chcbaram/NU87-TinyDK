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
#ifdef _USE_HW_THREAD
  /* 스레드 레지스트리. 뮤텍스를 만들므로 스케줄러가 돈 뒤여야 한다.
   * hwInit() 은 mainThread 안에서 불리므로 조건이 맞는다. */
  threadInit();
#endif
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

  resetInit();
  rtcInit();
  gpioInit();
  ipcInit();
  /* nvs 는 플래시를 쓴다. FLASH_Write_Lock() 이 IPC 로 KM0 를 재우고 응답을
   * 기다리므로 ipcInit() 보다 뒤여야 한다. */
  nvsInit();
  otaInit();
#ifdef _USE_HW_WIFI
  wifiInit();
#endif
#ifdef _USE_HW_BLE
  bleInit();
#endif

  logBoot(false);

  return true;
}
