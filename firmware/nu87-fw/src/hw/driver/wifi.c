/*
 * wifi.c — WiFi (RTL8720DF)
 *
 * KM0 가 MAC 펌웨어를 돌리고 KM4 가 IPC 로 붙는다. wifi_on() 이 그 기동을
 * 맡으므로 ipcInit() 이 끝난 뒤에 불러야 한다.
 */

#include "wifi.h"


#ifdef _USE_HW_WIFI
#include "cli.h"

#include "wifi_conf.h"
#include "lwip_netconf.h"
#include "lwip/netif.h"

extern struct netif xnetif[NET_IF_NUM];


#define WIFI_SCAN_MAX           24
#define WIFI_SCAN_TIMEOUT_MS    12000


typedef struct
{
  int  len;
  bool done;
  rtw_scan_result_t ap[WIFI_SCAN_MAX];
} wifi_scan_list_t;


/* wifi_conf.c 는 CONFIG_MBED_ENABLED 일 때만 이 전역을 정의하고
 * 그 밖에는 애플리케이션이 제공한다고 보고 extern 으로 참조한다. */
rtw_mode_t wifi_mode = RTW_MODE_STA;

static bool is_init = false;
static bool is_on   = false;

static wifi_scan_list_t scan_list;

static rtw_result_t scanResult(rtw_scan_handler_result_t *result);
static const char  *wifiSecurityName(rtw_security_t sec);

#ifdef _USE_HW_CLI
static void cliWifi(cli_args_t *args);
#endif




bool wifiInit(void)
{
  LwIP_Init();

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("wifi", cliWifi);
#endif

  logPrintf("[OK] wifiInit()\n");
  return true;
}

bool wifiOn(void)
{
  if (is_on) return true;

  if (wifi_on(RTW_MODE_STA) < 0)
  {
    return false;
  }

  is_on = true;
  return true;
}

bool wifiOff(void)
{
  if (!is_on) return true;

  wifi_off();
  is_on = false;
  return true;
}

bool wifiIsOn(void)
{
  return is_on;
}

bool wifiConnect(const char *ssid, const char *pass)
{
  rtw_security_t sec = (pass != NULL && pass[0] != 0) ? RTW_SECURITY_WPA2_AES_PSK
                                                      : RTW_SECURITY_OPEN;
  int ret;

  if (!wifiOn()) return false;

  ret = wifi_connect((char *)ssid, sec,
                     (char *)(pass ? pass : ""),
                     strlen(ssid),
                     pass ? strlen(pass) : 0,
                     -1, NULL);

  if (ret != RTW_SUCCESS)
  {
    return false;
  }

  /* 연결만으로는 IP 가 없다. DHCP 는 상위 모듈로 옮길 정책이지만
   * 지금은 하드웨어 경로 검증을 위해 여기서 돌린다. */
  return (LwIP_DHCP(0, DHCP_START) == DHCP_ADDRESS_ASSIGNED);
}


/* 스캔은 비동기다. AP 마다 한 번, 끝날 때 scan_complete 로 한 번 더 불린다. */
static rtw_result_t scanResult(rtw_scan_handler_result_t *result)
{
  wifi_scan_list_t *p_list = (wifi_scan_list_t *)result->user_data;

  if (result->scan_complete)
  {
    p_list->done = true;
    return RTW_SUCCESS;
  }

  if (p_list->len < WIFI_SCAN_MAX)
  {
    p_list->ap[p_list->len] = result->ap_details;
    p_list->ap[p_list->len].SSID.val[result->ap_details.SSID.len] = 0;
    p_list->len++;
  }
  return RTW_SUCCESS;
}

static const char *wifiSecurityName(rtw_security_t sec)
{
  switch (sec)
  {
    case RTW_SECURITY_OPEN:             return "OPEN";
    case RTW_SECURITY_WEP_PSK:          return "WEP";
    case RTW_SECURITY_WPA_TKIP_PSK:     return "WPA/TKIP";
    case RTW_SECURITY_WPA_AES_PSK:      return "WPA/AES";
    case RTW_SECURITY_WPA2_TKIP_PSK:    return "WPA2/TKIP";
    case RTW_SECURITY_WPA2_AES_PSK:     return "WPA2/AES";
    case RTW_SECURITY_WPA2_MIXED_PSK:   return "WPA2/MIXED";
    case RTW_SECURITY_WPA2_WPA3_MIXED:  return "WPA2/WPA3";
    case RTW_SECURITY_WPA3_AES_PSK:     return "WPA3/AES";
    default:                            return "?";
  }
}


#ifdef _USE_HW_CLI
void cliWifi(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("init   : %s\n", is_init ? "True" : "False");
    cliPrintf("on     : %s\n", is_on ? "True" : "False");

    if (is_on)
    {
      /* wifi_get_mac_address() 는 "xx:xx:.." 문자열을 strcpy 한다.
       * 6 바이트 버퍼를 주면 스택을 넘긴다. */
      char mac[32] = {0};
      uint8_t *ip;

      wifi_get_mac_address(mac);
      cliPrintf("mac    : %s\n", mac);

      ip = LwIP_GetIP(&xnetif[0]);
      cliPrintf("ip     : %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "on"))
  {
    cliPrintf("wifi on : %s\n", wifiOn() ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "off"))
  {
    cliPrintf("wifi off : %s\n", wifiOff() ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "scan"))
  {
    if (wifiOn())
    {
      scan_list.len  = 0;
      scan_list.done = false;

      if (wifi_scan_networks(scanResult, &scan_list) != RTW_SUCCESS)
      {
        cliPrintf("scan 시작 실패\n");
      }
      else
      {
        uint32_t pre_time = millis();

        while (!scan_list.done && millis() - pre_time < WIFI_SCAN_TIMEOUT_MS)
        {
          delay(10);
        }

        cliPrintf("\n%-3s %-32s %5s %5s  %s\n", "no", "ssid", "rssi", "chan", "security");
        for (int i = 0; i < scan_list.len; i++)
        {
          cliPrintf("%-3d %-32s %5d %5d  %s\n",
                    i,
                    scan_list.ap[i].SSID.val,
                    (int)scan_list.ap[i].signal_strength,
                    (int)scan_list.ap[i].channel,
                    wifiSecurityName(scan_list.ap[i].security));
        }
        cliPrintf("%d 개%s\n", scan_list.len, scan_list.done ? "" : " (시간 초과)");
      }
    }
    ret = true;
  }

  if ((args->argc == 2 || args->argc == 3) && args->isStr(0, "connect"))
  {
    const char *ssid = args->getStr(1);
    const char *pass = (args->argc == 3) ? args->getStr(2) : NULL;

    cliPrintf("connect %s ... ", ssid);
    cliPrintf("%s\n", wifiConnect(ssid, pass) ? "OK" : "Fail");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("wifi info\n");
    cliPrintf("wifi on\n");
    cliPrintf("wifi off\n");
    cliPrintf("wifi scan\n");
    cliPrintf("wifi connect ssid [pass]\n");
  }
}
#endif

#endif
