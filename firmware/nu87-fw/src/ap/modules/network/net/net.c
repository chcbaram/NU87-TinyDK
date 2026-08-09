/*
 * net.c — 네트워크 정책
 *
 * hw/driver/wifi.c 는 무선을 켜고 붙이는 것까지만 한다. 무엇에 붙을지, 언제
 * 다시 붙을지, 붙은 다음 무엇을 할지는 여기서 정한다.
 *
 * 접속 정보는 NVS 에 두고 부팅하면 스스로 붙는다. 붙으면 DHCP 를 돌리고
 * SNTP 로 RTC 를 맞춘다.
 *
 * CLI 도 같은 선으로 나뉜다.
 *   wifi ...  하드웨어가 살아 있는가 (on/off/scan)
 *   net  ...  네트워크가 쓸 수 있는 상태인가 (connect/status/sync)
 */

#include "net.h"


#ifdef _USE_HW_WIFI
#include "cli.h"

#include <time.h>

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"


#define NET_NVS_NAME          "net_cfg"
#define NET_SSID_MAX          36
#define NET_PASS_MAX          68

#define NET_SNTP_SERVER       "kr.pool.ntp.org"
#define NET_SNTP_PORT         123
#define NET_SNTP_TIMEOUT_MS   4000
#define NET_SNTP_RETRY        3

/* NTP 는 1900-01-01 을 0 으로 센다. UNIX epoch 까지의 차이다. */
#define NET_NTP_TO_UNIX       2208988800UL

#define NET_RETRY_DELAY_MS    10000
#define NET_SYNC_PERIOD_MS    (12 * 60 * 60 * 1000UL)


typedef struct
{
  char     ssid[NET_SSID_MAX];
  char     pass[NET_PASS_MAX];
  uint8_t  is_auto;
  int16_t  tz_min;              /* UTC 로부터의 분. 표시에만 쓴다 */
} net_cfg_t;


static bool       is_init  = false;
static NetState_t state    = NET_STATE_IDLE;
static uint32_t   sync_time = 0;
static net_cfg_t  net_cfg =
{
  .ssid    = "",
  .pass    = "",
  .is_auto = 1,
  .tz_min  = 9 * 60,
};

static void netThread(void *arg);
static bool netCfgLoad(void);
static bool netCfgSave(void);
static bool netSntpQuery(const char *server, uint32_t *p_epoch);

#ifdef _USE_HW_CLI
static void cliNet(cli_args_t *args);
#endif




bool netInit(void)
{
  netCfgLoad();

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("net", cliNet);
#endif

  return threadCreate("net", netThread, NULL,
                      _HW_DEF_RTOS_THREAD_PRI_NET, _HW_DEF_RTOS_THREAD_MEM_NET);
}

bool netIsOnline(void)
{
  return (state == NET_STATE_ONLINE);
}

NetState_t netGetState(void)
{
  return state;
}

bool netConnect(const char *ssid, const char *pass, bool save)
{
  if (!is_init) return false;

  strncpy(net_cfg.ssid, ssid, NET_SSID_MAX - 1);
  net_cfg.ssid[NET_SSID_MAX - 1] = 0;
  strncpy(net_cfg.pass, pass ? pass : "", NET_PASS_MAX - 1);
  net_cfg.pass[NET_PASS_MAX - 1] = 0;

  if (save && netCfgSave() == false)
  {
    logPrintf("[E_] netConnect() : nvs 저장 실패\n");
  }

  /* 실제 접속은 스레드가 한다. 여기서 붙이면 CLI 가 수 초 동안 막힌다. */
  state = NET_STATE_CONNECTING;
  return true;
}

bool netDisconnect(void)
{
  net_cfg.is_auto = 0;
  netCfgSave();

  wifiDisconnect();
  state = NET_STATE_IDLE;
  return true;
}

bool netSyncTime(void)
{
  uint32_t epoch;

  if (netIsOnline() == false) return false;

  for (int i = 0; i < NET_SNTP_RETRY; i++)
  {
    if (netSntpQuery(NET_SNTP_SERVER, &epoch))
    {
      sync_time = millis();
      return rtcSetEpochTime(epoch);
    }
  }
  return false;
}

static void netThread(void *arg)
{
  (void)arg;

  while (1)
  {
    switch (state)
    {
      case NET_STATE_IDLE:
        if (net_cfg.is_auto && net_cfg.ssid[0] != 0)
        {
          state = NET_STATE_CONNECTING;
        }
        break;

      case NET_STATE_CONNECTING:
        if (wifiConnect(net_cfg.ssid, net_cfg.pass))
        {
          state = NET_STATE_DHCP;
        }
        else
        {
          logPrintf("[E_] net : %s 접속 실패\n", net_cfg.ssid);
          delay(NET_RETRY_DELAY_MS);
        }
        break;

      case NET_STATE_DHCP:
        if (wifiDhcpStart())
        {
          uint8_t ip[4];

          wifiGetIp(ip);
          logPrintf("[OK] net : %s  %d.%d.%d.%d\n",
                    net_cfg.ssid, ip[0], ip[1], ip[2], ip[3]);
          state = NET_STATE_ONLINE;
          sync_time = 0;
        }
        else
        {
          state = NET_STATE_CONNECTING;
          delay(NET_RETRY_DELAY_MS);
        }
        break;

      case NET_STATE_ONLINE:
        if (wifiIsConnected() == false)
        {
          logPrintf("[E_] net : 연결이 끊겼다\n");
          state = NET_STATE_CONNECTING;
          break;
        }
        if (sync_time == 0 || millis() - sync_time >= NET_SYNC_PERIOD_MS)
        {
          sync_time = millis();       /* 실패해도 바로 다시 시도하지 않는다 */
          netSyncTime();
        }
        break;
    }

    delay(100);
  }
}

static bool netCfgLoad(void)
{
  return nvsGet(NET_NVS_NAME, &net_cfg, sizeof(net_cfg_t));
}

static bool netCfgSave(void)
{
  return nvsSet(NET_NVS_NAME, &net_cfg, sizeof(net_cfg_t));
}

/* SNTP 요청 한 번. 48바이트를 보내고 같은 크기를 받는다.
 * 우리가 쓰는 것은 transmit timestamp(오프셋 40) 의 초 부분뿐이다. */
static bool netSntpQuery(const char *server, uint32_t *p_epoch)
{
  struct hostent    *p_host;
  struct sockaddr_in addr;
  struct timeval     tv;
  uint8_t  packet[48] = {0};
  uint32_t sec;
  int      sock;
  bool     ret = false;

  p_host = gethostbyname(server);
  if (p_host == NULL || p_host->h_addr_list[0] == NULL)
  {
    return false;
  }

  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) return false;

  tv.tv_sec  = NET_SNTP_TIMEOUT_MS / 1000;
  tv.tv_usec = (NET_SNTP_TIMEOUT_MS % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(NET_SNTP_PORT);
  memcpy(&addr.sin_addr, p_host->h_addr_list[0], sizeof(addr.sin_addr));

  packet[0] = 0x1B;   /* LI 0 / VN 3 / Mode 3 (client) */

  if (sendto(sock, packet, sizeof(packet), 0,
             (struct sockaddr *)&addr, sizeof(addr)) == sizeof(packet))
  {
    if (recv(sock, packet, sizeof(packet), 0) == sizeof(packet))
    {
      memcpy(&sec, &packet[40], sizeof(sec));
      sec = ntohl(sec);

      if (sec > NET_NTP_TO_UNIX)
      {
        *p_epoch = sec - NET_NTP_TO_UNIX;
        ret = true;
      }
    }
  }

  close(sock);
  return ret;
}


#ifdef _USE_HW_CLI
void cliNet(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const char *state_str[] = { "IDLE", "CONNECTING", "DHCP", "ONLINE" };
    time_t      local = (time_t)(rtcGetEpochTime() + net_cfg.tz_min * 60);
    struct tm  *p_tm  = gmtime(&local);
    uint8_t     ip[4] = {0};
    char        mac[32] = {0};

    cliPrintf("state : %s\n", state_str[state]);
    cliPrintf("ssid  : %s\n", net_cfg.ssid[0] ? net_cfg.ssid : "(없음)");
    cliPrintf("auto  : %s\n", net_cfg.is_auto ? "True" : "False");

    if (wifiGetMac(mac, sizeof(mac))) cliPrintf("mac   : %s\n", mac);
    if (wifiGetIp(ip))                cliPrintf("ip    : %d.%d.%d.%d\n",
                                                ip[0], ip[1], ip[2], ip[3]);

    cliPrintf("time  : %04d-%02d-%02d %02d:%02d:%02d UTC%+d  (%s)\n",
              p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday,
              p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec,
              net_cfg.tz_min / 60, rtcIsTimeSet() ? "동기됨" : "미설정");
    ret = true;
  }

  if ((args->argc == 2 || args->argc == 3) && args->isStr(0, "connect"))
  {
    const char *ssid = args->getStr(1);
    const char *pass = (args->argc == 3) ? args->getStr(2) : NULL;

    net_cfg.is_auto = 1;
    cliPrintf("net connect %s : %s\n", ssid,
              netConnect(ssid, pass, true) ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "disconnect"))
  {
    cliPrintf("net disconnect : %s\n", netDisconnect() ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "sync"))
  {
    cliPrintf("net sync : %s\n", netSyncTime() ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "auto"))
  {
    net_cfg.is_auto = args->isStr(1, "on") ? 1 : 0;
    cliPrintf("net auto %s : %s\n", net_cfg.is_auto ? "on" : "off",
              netCfgSave() ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "tz"))
  {
    net_cfg.tz_min = (int16_t)(args->getData(1) * 60);
    cliPrintf("net tz UTC%+d : %s\n", net_cfg.tz_min / 60,
              netCfgSave() ? "OK" : "Fail");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("net info\n");
    cliPrintf("net connect ssid [pass]\n");
    cliPrintf("net disconnect\n");
    cliPrintf("net sync\n");
    cliPrintf("net auto on|off\n");
    cliPrintf("net tz [hour]\n");
  }
}
#endif


MODULE_DEF(net)
{
  .name     = "net",
  .priority = MODULE_PRI_LOW,
  .init     = netInit,
};

#endif
