#ifndef INCLUDE_RTC_H_
#define INCLUDE_RTC_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_RTC

typedef struct
{
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;
} rtc_time_t;

typedef struct
{
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t week;
} rtc_date_t;

typedef struct
{
  rtc_time_t time;
  rtc_date_t date;
} rtc_info_t;


bool rtcInit(void);
bool rtcGetInfo(rtc_info_t *rtc_info);
bool rtcGetTime(rtc_time_t *rtc_time);
bool rtcGetDate(rtc_date_t *rtc_date);
bool rtcSetInfo(rtc_info_t *rtc_info);
bool rtcSetTime(rtc_time_t *rtc_time);
bool rtcSetDate(rtc_date_t *rtc_date);

/* epoch 는 언제나 UTC 다. SNTP 가 주는 값이 이 단위다.
 * 반대로 rtc_info_t / rtc_time_t / rtc_date_t 는 시간대가 적용된 지역 시각이다. */
uint32_t rtcGetEpochTime(void);
bool     rtcSetEpochTime(uint32_t epoch);
bool     rtcIsTimeSet(void);

int16_t  rtcGetTimeZone(void);          /* UTC 로부터의 분 */
bool     rtcSetTimeZone(int16_t offset_min);

bool rtcSetReg(uint32_t index, uint32_t data);
bool rtcGetReg(uint32_t index, uint32_t *p_data);

#endif

#ifdef __cplusplus
}
#endif

#endif 