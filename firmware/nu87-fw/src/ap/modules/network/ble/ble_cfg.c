/*
 * ble_cfg.c — BLE 통로로 오는 설정 요청 처리
 *
 * hw/driver/ble.c 는 바이트만 나른다. 그 바이트가 무슨 뜻인지는 여기서 정한다.
 *
 * 프로토콜은 줄 단위 텍스트다. 브라우저 콘솔이나 어떤 도구로도 바로 두드려 볼
 * 수 있어야 하고, 나중에 Web Serial 같은 다른 통로에 그대로 얹을 수 있어야
 * 하기 때문이다. 파서는 전송을 모른다.
 *
 *   요청   info
 *          wifi set <ssid> <pass>
 *   응답   key=value 여러 줄, 마지막에 ok 또는 err <사유>
 */

#include "ble_cfg.h"


#if defined(_USE_HW_BLE) && defined(_USE_HW_WIFI)
#include "network/net/net.h"


#define BLE_CFG_LINE_MAX      160
#define BLE_CFG_ARGS_MAX      8

/* notify 한 번에 실을 수 있는 크기는 MTU-3 이다. 협상 결과를 모르는 상태에서도
 * 안전하도록 최소 MTU(23) 기준으로 자른다. */
#define BLE_CFG_CHUNK         20


static char     rx_line[BLE_CFG_LINE_MAX];
static uint16_t rx_len = 0;


static void bleCfgRx(uint8_t *p_data, uint16_t length);
static void bleCfgHandleLine(char *p_line);
static void bleCfgPrintf(const char *fmt, ...);




bool bleCfgInit(void)
{
  return bleSetRxHandler(bleCfgRx);
}

/* 통로는 바이트 단위로 쪼개져 들어온다. 줄바꿈을 만날 때까지 모은다. */
static void bleCfgRx(uint8_t *p_data, uint16_t length)
{
  for (uint16_t i = 0; i < length; i++)
  {
    char data = (char)p_data[i];

    if (data == '\r') continue;

    if (data == '\n')
    {
      rx_line[rx_len] = 0;
      if (rx_len > 0) bleCfgHandleLine(rx_line);
      rx_len = 0;
      continue;
    }

    if (rx_len < BLE_CFG_LINE_MAX - 1)
    {
      rx_line[rx_len++] = data;
    }
  }
}

static void bleCfgHandleLine(char *p_line)
{
  char *argv[BLE_CFG_ARGS_MAX];
  int   argc = 0;
  char *p    = p_line;

  while (argc < BLE_CFG_ARGS_MAX && *p != 0)
  {
    while (*p == ' ') p++;
    if (*p == 0) break;

    argv[argc++] = p;
    while (*p != 0 && *p != ' ') p++;
    if (*p != 0) *p++ = 0;
  }

  if (argc == 0) return;

  if (strcmp(argv[0], "info") == 0)
  {
    rtc_info_t info;
    char       mac[32] = {0};
    uint8_t    ip[4]   = {0};

    bleCfgPrintf("board=%s\n", _DEF_BOARD_NAME);
    bleCfgPrintf("ver=%s\n",   _DEF_FIRMWATRE_VERSION);

    if (bleGetMac(mac, sizeof(mac)))  bleCfgPrintf("btmac=%s\n", mac);
    if (wifiGetMac(mac, sizeof(mac))) bleCfgPrintf("wifimac=%s\n", mac);

    bleCfgPrintf("wifi=%s\n", netIsOnline() ? "online" : "offline");
    if (wifiGetIp(ip))
    {
      bleCfgPrintf("ip=%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    }

    rtcGetInfo(&info);
    bleCfgPrintf("time=20%02d-%02d-%02d %02d:%02d:%02d\n",
                 info.date.year, info.date.month, info.date.day,
                 info.time.hours, info.time.minutes, info.time.seconds);

    bleCfgPrintf("ok\n");
    return;
  }

  if (strcmp(argv[0], "wifi") == 0 && argc >= 3 && strcmp(argv[1], "set") == 0)
  {
    const char *ssid = argv[2];
    const char *pass = (argc >= 4) ? argv[3] : NULL;

    bleCfgPrintf(netConnect(ssid, pass, true) ? "ok\n" : "err connect\n");
    return;
  }

  bleCfgPrintf("err unknown\n");
}

static void bleCfgPrintf(const char *fmt, ...)
{
  char    buf[BLE_CFG_LINE_MAX];
  va_list args;
  int     len;
  int     offset = 0;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len <= 0) return;

  while (offset < len)
  {
    int size = ((len - offset) > BLE_CFG_CHUNK) ? BLE_CFG_CHUNK : (len - offset);

    if (bleSend((uint8_t *)&buf[offset], size) == false) return;
    offset += size;
  }
}


MODULE_DEF(ble_cfg)
{
  .name     = "ble_cfg",
  .priority = MODULE_PRI_LOW,
  .init     = bleCfgInit,
};

#endif
