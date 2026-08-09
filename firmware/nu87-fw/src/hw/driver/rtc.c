/*
 * rtc.c — RTC (RTL8720DF)
 *
 * 이 RTC 는 날짜를 모른다. 9비트 Days(0~511) 와 시:분:초만 센다.
 * 그래서 "기준 자정의 epoch" 를 백업 레지스터에 두고 실제 시각을 이렇게 만든다.
 *
 *     epoch = base + Days*86400 + H*3600 + M*60 + S
 *
 * 백업 레지스터는 always-on 도메인이라 리셋으로 지워지지 않는다. RTC 도 같은
 * 도메인이라 둘은 항상 같이 살고 같이 죽는다.
 *
 * Days 가 넘치기 전에 다시 기준을 잡는다. rtcGetEpochTime() 이 그 일을 한다.
 */

#include "rtc.h"


#ifdef _USE_HW_RTC
#include "cli.h"
#include <time.h>


/* REG0/6/7 은 시스템 몫이고 REG2 는 생산 테스트가 쓴다. REG1 은 문서상 사용자
 * 영역이지만 실측하면 소프트 리셋마다 0 으로 지워진다(REG3~5 는 유지된다). */
#define RTC_BKUP_BASE       BKUP_REG3        /* 기준 자정의 epoch. 0 이면 시각 미설정 */
#define RTC_BKUP_USER       BKUP_REG4        /* 여기부터 rtcSetReg/rtcGetReg 몫 */
#define RTC_BKUP_USER_MAX   2                /* REG4 ~ REG5 */

#define RTC_REBASE_DAYS     400              /* Days 는 9비트라 511 이 한계다 */

/* 시각을 아직 못 받았을 때 읽히는 값. rtc_date_t 의 year 는 2000 기준 오프셋이라
 * epoch 0(1970) 을 그대로 내보내면 음수가 되어 날짜가 깨진다. */
#define RTC_EPOCH_UNSET     1767225600UL     /* 2026-01-01 00:00:00 UTC */


static bool is_init = false;

static uint32_t rtcCivilToEpoch(uint32_t year, uint32_t month, uint32_t day,
                                uint32_t hour, uint32_t min, uint32_t sec);

#ifdef _USE_HW_CLI
static void cliRtc(cli_args_t *args);
#endif




bool rtcInit(void)
{
  RTC_InitTypeDef rtc_init;

  RCC_PeriphClockSource_RTC(0);

  RTC_StructInit(&rtc_init);
  rtc_init.RTC_HourFormat = RTC_HourFormat_24;
  RTC_Init(&rtc_init);

  /* shadow 를 거치면 읽을 때마다 동기화를 기다려야 한다. */
  RTC_BypassShadowCmd(ENABLE);

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("rtc", cliRtc);
#endif

  logPrintf("[OK] rtcInit()\n");
  return true;
}

bool rtcGetInfo(rtc_info_t *rtc_info)
{
  time_t     now = (time_t)rtcGetEpochTime();
  struct tm *p_tm = gmtime(&now);

  if (p_tm == NULL) return false;

  rtc_info->time.hours   = p_tm->tm_hour;
  rtc_info->time.minutes = p_tm->tm_min;
  rtc_info->time.seconds = p_tm->tm_sec;

  rtc_info->date.year    = (p_tm->tm_year + 1900) - 2000;
  rtc_info->date.month   = p_tm->tm_mon + 1;
  rtc_info->date.day     = p_tm->tm_mday;
  rtc_info->date.week    = p_tm->tm_wday;

  return true;
}

bool rtcGetTime(rtc_time_t *rtc_time)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) == false) return false;

  *rtc_time = info.time;
  return true;
}

bool rtcGetDate(rtc_date_t *rtc_date)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) == false) return false;

  *rtc_date = info.date;
  return true;
}

bool rtcSetTime(rtc_time_t *rtc_time)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) == false) return false;

  info.time = *rtc_time;
  return rtcSetInfo(&info);
}

bool rtcSetDate(rtc_date_t *rtc_date)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) == false) return false;

  info.date = *rtc_date;
  return rtcSetInfo(&info);
}

bool rtcSetInfo(rtc_info_t *rtc_info)
{
  return rtcSetEpochTime(rtcCivilToEpoch(2000 + rtc_info->date.year,
                                         rtc_info->date.month,
                                         rtc_info->date.day,
                                         rtc_info->time.hours,
                                         rtc_info->time.minutes,
                                         rtc_info->time.seconds));
}

uint32_t rtcGetEpochTime(void)
{
  RTC_TimeTypeDef rtc_time;
  uint32_t base = BKUP_Read(RTC_BKUP_BASE);
  uint32_t sec_of_day;
  bool     is_set = (base != 0);

  if (!is_set) base = RTC_EPOCH_UNSET;

  RTC_GetTime(RTC_Format_BIN, &rtc_time);

  sec_of_day = rtc_time.RTC_Hours * 3600UL
             + rtc_time.RTC_Minutes * 60UL
             + rtc_time.RTC_Seconds;

  /* Days 가 한계에 닿기 전에 기준을 오늘로 당긴다. 시:분:초는 건드리지 않으므로
   * 이 재조정으로 시각이 흔들리지 않는다. */
  if (is_set && rtc_time.RTC_Days >= RTC_REBASE_DAYS)
  {
    base += (uint32_t)rtc_time.RTC_Days * 86400UL;
    BKUP_Write(RTC_BKUP_BASE, base);

    rtc_time.RTC_Days = 0;
    RTC_SetTime(RTC_Format_BIN, &rtc_time);

    return base + sec_of_day;
  }

  return base + (uint32_t)rtc_time.RTC_Days * 86400UL + sec_of_day;
}

bool rtcIsTimeSet(void)
{
  return (BKUP_Read(RTC_BKUP_BASE) != 0);
}

bool rtcSetEpochTime(uint32_t epoch)
{
  RTC_TimeTypeDef rtc_time;
  uint32_t        sec_of_day = epoch % 86400UL;

  RTC_TimeStructInit(&rtc_time);
  rtc_time.RTC_Days    = 0;
  rtc_time.RTC_Hours   = sec_of_day / 3600UL;
  rtc_time.RTC_Minutes = (sec_of_day % 3600UL) / 60UL;
  rtc_time.RTC_Seconds = sec_of_day % 60UL;

  if (RTC_SetTime(RTC_Format_BIN, &rtc_time) != _SUCCESS) return false;

  BKUP_Write(RTC_BKUP_BASE, epoch - sec_of_day);
  return true;
}

bool rtcSetReg(uint32_t index, uint32_t data)
{
  if (index >= RTC_BKUP_USER_MAX) return false;

  BKUP_Write(RTC_BKUP_USER + index, data);
  return true;
}

bool rtcGetReg(uint32_t index, uint32_t *p_data)
{
  if (index >= RTC_BKUP_USER_MAX) return false;

  *p_data = BKUP_Read(RTC_BKUP_USER + index);
  return true;
}

/* 그레고리력 -> epoch. Howard Hinnant 의 days_from_civil 이다.
 * 3월을 한 해의 시작으로 옮기면 윤일이 400년 주기(era)의 맨 끝으로 가서
 * 분기 없이 나눗셈만으로 일수가 나온다. 719468 은 0000-03-01 부터 1970-01-01 까지의 일수다. */
static uint32_t rtcCivilToEpoch(uint32_t year, uint32_t month, uint32_t day,
                                uint32_t hour, uint32_t min, uint32_t sec)
{
  int32_t y = (int32_t)year - (month <= 2);
  int32_t era = y / 400;
  int32_t yoe = y - era * 400;
  int32_t doy = (153 * ((int32_t)month + (month > 2 ? -3 : 9)) + 2) / 5 + (int32_t)day - 1;
  int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int32_t days = era * 146097 + doe - 719468;

  return (uint32_t)days * 86400UL + hour * 3600UL + min * 60UL + sec;
}


#ifdef _USE_HW_CLI
void cliRtc(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    rtc_info_t info;

    rtcGetInfo(&info);
    cliPrintf("init  : %s\n", is_init ? "True" : "False");
    cliPrintf("set   : %s\n", rtcIsTimeSet() ? "True" : "False");
    cliPrintf("epoch : %u\n", (unsigned int)rtcGetEpochTime());
    cliPrintf("time  : 20%02d-%02d-%02d %02d:%02d:%02d (UTC)\n",
              info.date.year, info.date.month, info.date.day,
              info.time.hours, info.time.minutes, info.time.seconds);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get") && args->isStr(1, "info"))
  {
    rtc_info_t info;

    while (cliKeepLoop())
    {
      rtcGetInfo(&info);
      cliPrintf("20%02d-%02d-%02d %02d:%02d:%02d\n",
                info.date.year, info.date.month, info.date.day,
                info.time.hours, info.time.minutes, info.time.seconds);
      delay(1000);
    }
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") && args->isStr(1, "time"))
  {
    rtc_time_t rtc_time;

    rtc_time.hours   = args->getData(2);
    rtc_time.minutes = args->getData(3);
    rtc_time.seconds = args->getData(4);

    cliPrintf("rtc set time : %s\n", rtcSetTime(&rtc_time) ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") && args->isStr(1, "date"))
  {
    rtc_date_t rtc_date;

    rtc_date.year  = args->getData(2);
    rtc_date.month = args->getData(3);
    rtc_date.day   = args->getData(4);

    cliPrintf("rtc set date : %s\n", rtcSetDate(&rtc_date) ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "set") && args->isStr(1, "epoch"))
  {
    cliPrintf("rtc set epoch : %s\n",
              rtcSetEpochTime((uint32_t)args->getData(2)) ? "OK" : "Fail");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("rtc info\n");
    cliPrintf("rtc get info\n");
    cliPrintf("rtc set time [h] [m] [s]\n");
    cliPrintf("rtc set date [y] [m] [d]\n");
    cliPrintf("rtc set epoch [sec]\n");
  }
}
#endif

#endif
