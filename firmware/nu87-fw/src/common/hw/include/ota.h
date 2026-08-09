#ifndef OTA_H_
#define OTA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_OTA

typedef struct
{
  uint8_t  slot_run;        /* 지금 실행 중인 슬롯 (0 = OTA1, 1 = OTA2) */
  uint8_t  slot_target;     /* 새 이미지를 받을 슬롯 */
  uint32_t addr_run;
  uint32_t addr_target;
  uint32_t size_max;
  bool     is_busy;
  uint32_t written;
} ota_info_t;


bool otaInit(void);
bool otaGetInfo(ota_info_t *p_info);

/* 받기 시작. 대상 슬롯을 정하고 진행 상태를 초기화한다. */
bool otaBegin(uint32_t size);

/* 이어서 쓴다. 섹터 경계에 닿으면 그때 지운다. */
bool otaWrite(uint8_t *p_data, uint32_t length);

/* 검증하고 부팅 슬롯을 바꾼다. crc 는 이미지 전체의 CRC32 다. */
bool otaEnd(uint32_t crc);

/* 받다 만 것을 버린다. 부팅 슬롯은 건드리지 않는다. */
bool otaAbort(void);

/* 한 채널에서 이미지 하나를 통째로 받는다. 전송이 무엇인지는 모른다 —
 * USB(LOGUART) / BLE 데이터 채널 / 향후 WiFi 가 모두 같은 uart 채널이다. */
bool otaReceive(uint8_t uart_ch, uint32_t size, uint32_t crc);

#endif

#ifdef __cplusplus
}
#endif

#endif
