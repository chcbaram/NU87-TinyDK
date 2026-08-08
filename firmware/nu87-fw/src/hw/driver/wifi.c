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


/* wifi_conf.c 는 CONFIG_MBED_ENABLED 일 때만 이 전역을 정의하고
 * 그 밖에는 애플리케이션이 제공한다고 보고 extern 으로 참조한다. */
rtw_mode_t wifi_mode = RTW_MODE_STA;

static bool is_init = false;
static bool is_on   = false;

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

  return (ret == RTW_SUCCESS);
}


#ifdef _USE_HW_CLI
void cliWifi(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uint8_t mac[6] = {0};

    cliPrintf("init   : %s\n", is_init ? "True" : "False");
    cliPrintf("on     : %s\n", is_on ? "True" : "False");

    if (is_on)
    {
      wifi_get_mac_address((char *)mac);
      cliPrintf("mac    : %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      cliPrintf("ip     : %s\n", (char *)LwIP_GetIP(0));
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
      wifi_scan_networks(NULL, NULL);
      cliPrintf("scan 요청\n");
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
