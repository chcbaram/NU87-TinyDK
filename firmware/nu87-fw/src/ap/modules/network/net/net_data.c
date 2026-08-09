/*
 * net_data.c — TCP 로 여는 원시 데이터 채널
 *
 * cli_net.c 와 같은 방법이다. 소켓을 uart_driver_t 로 위장시켜 HW_UART_CH_NET_DATA
 * 에 물린다. 다른 점은 여기에 프로토콜이 없다는 것뿐이다 — 텔넷 협상도 줄바꿈
 * 정규화도 하지 않고 바이트를 그대로 나른다.
 *
 * 쓰는 쪽은 펌웨어 업데이트다. CLI(텔넷 23)로 "ota write <크기> <crc> 5" 를 보내고
 * 굵은 흐름은 이 포트로 흘린다. BLE 가 CLI 와 데이터를 다른 특성으로 나눈 것과
 * 같은 구조다.
 *
 * 브라우저는 raw TCP 를 열 수 없으므로 이 채널은 PC 도구(tools/ota.py) 몫이다.
 */

#include "net_data.h"


#ifdef _USE_HW_WIFI
#include "uart.h"
#include "qbuffer.h"

#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "lwip/errno.h"


#define NET_DATA_BUF_LEN      4096


static uint16_t data_port   = 0;
static int      listen_sock = -1;
static int      client_sock = -1;

static qbuffer_t     rx_q;
static uint8_t       rx_buf[NET_DATA_BUF_LEN];
static uart_driver_t net_drv;


static bool     netDataOpen(uint32_t baud);
static bool     netDataClose(void);
static uint32_t netDataAvailable(void);
static bool     netDataFlush(void);
static uint8_t  netDataRead(void);
static uint32_t netDataWrite(uint8_t *p_data, uint32_t length);
static void     netDataClientClose(void);




bool netDataInit(uint16_t port)
{
  data_port   = port;
  listen_sock = -1;
  client_sock = -1;

  qbufferCreate(&rx_q, rx_buf, NET_DATA_BUF_LEN);

  net_drv.open      = netDataOpen;
  net_drv.close     = netDataClose;
  net_drv.available = netDataAvailable;
  net_drv.flush     = netDataFlush;
  net_drv.read      = netDataRead;
  net_drv.write     = netDataWrite;

  return uartSetDriver(HW_UART_CH_NET_DATA, &net_drv);
}

void netDataPoll(void)
{
  if (listen_sock < 0)
  {
    struct sockaddr_in addr;
    int opt = 1;

    if (netif_default == NULL || netif_is_up(netif_default) == 0) return;

    listen_sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) return;

    lwip_setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
    addr.sin_port        = lwip_htons(data_port);

    if (lwip_bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        lwip_listen(listen_sock, 1) < 0)
    {
      lwip_close(listen_sock);
      listen_sock = -1;
      return;
    }

    lwip_fcntl(listen_sock, F_SETFL, O_NONBLOCK);
    logPrintf("[OK] netData : port %d\n", data_port);
  }

  if (client_sock < 0)
  {
    int fd = lwip_accept(listen_sock, NULL, NULL);

    if (fd >= 0)
    {
      int opt_on = 1;

      client_sock = fd;
      qbufferFlush(&rx_q);

      lwip_fcntl(client_sock, F_SETFL, O_NONBLOCK);
      lwip_setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &opt_on, sizeof(opt_on));
      logPrintf("[  ] netData : 접속\n");
    }
  }
}

bool netDataIsConnected(void)
{
  return (client_sock >= 0);
}

static bool netDataOpen(uint32_t baud)
{
  (void)baud;
  return true;
}

static bool netDataClose(void)
{
  return true;
}

/* 소켓에서 꺼내 큐에 옮긴다. 상위는 한 바이트씩 읽어가므로 여기서 모아 둔다. */
static uint32_t netDataAvailable(void)
{
  uint8_t buf[1024];
  int     rx_size;

  if (client_sock < 0) return 0;

  while (qbufferAvailable(&rx_q) + sizeof(buf) <= NET_DATA_BUF_LEN)
  {
    rx_size = lwip_recv(client_sock, buf, sizeof(buf), 0);

    if (rx_size > 0)
    {
      qbufferWrite(&rx_q, buf, rx_size);
      continue;
    }

    if (rx_size == 0)
    {
      netDataClientClose();
      break;
    }

    if (errno != EWOULDBLOCK && errno != EAGAIN) netDataClientClose();
    break;
  }

  return qbufferAvailable(&rx_q);
}

static bool netDataFlush(void)
{
  qbufferFlush(&rx_q);
  return true;
}

static uint8_t netDataRead(void)
{
  uint8_t data = 0;

  qbufferRead(&rx_q, &data, 1);
  return data;
}

static uint32_t netDataWrite(uint8_t *p_data, uint32_t length)
{
  uint32_t offset = 0;

  if (client_sock < 0) return 0;

  while (offset < length)
  {
    int tx_size = lwip_send(client_sock, p_data + offset, length - offset, 0);

    if (tx_size <= 0)
    {
      if (tx_size < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) continue;
      netDataClientClose();
      break;
    }
    offset += (uint32_t)tx_size;
  }

  return offset;
}

static void netDataClientClose(void)
{
  if (client_sock >= 0)
  {
    lwip_close(client_sock);
    client_sock = -1;
  }
  qbufferFlush(&rx_q);
}

#endif
