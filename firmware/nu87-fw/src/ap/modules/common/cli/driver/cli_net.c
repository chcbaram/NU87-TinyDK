/*
 * cli_net.c — 텔넷으로 붙는 CLI
 *
 * uart_driver_t 를 HW_UART_CH_NET 에 등록해 소켓을 UART 처럼 보이게 한다.
 * CLI 코어는 채널 번호만 바뀔 뿐 자기가 소켓을 쓰는지 모른다.
 *
 * 두 가지를 여기서 흡수한다.
 *   telnet IAC 시퀀스 — 옵션 협상 바이트가 입력에 섞여 들어온다
 *   줄바꿈 차이     — CLI 코어는 Enter 를 CR 로만 받고, 터미널은 CRLF 를 보낸다
 */

#include "cli_net.h"


#ifdef _USE_HW_WIFI
#include "uart.h"
#include "log.h"

#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "lwip/errno.h"


#define TN_IAC      255
#define TN_DONT     254
#define TN_DO       253
#define TN_WONT     252
#define TN_WILL     251
#define TN_SB       250
#define TN_SE       240
#define TN_OPT_ECHO 1
#define TN_OPT_SGA  3

#define CLI_NET_BUF_LEN   256


enum
{
  IAC_NORMAL,
  IAC_CMD,      /* IAC 직후. 명령 바이트 대기 */
  IAC_OPT,      /* WILL/WONT/DO/DONT 의 옵션 바이트 대기 */
  IAC_SB,       /* subnegotiation. IAC SE 까지 */
  IAC_SB_IAC,
};


static uint16_t net_port    = 23;
static int      listen_sock = -1;
static int      client_sock = -1;

static uint8_t  rx_buf[CLI_NET_BUF_LEN];
static uint16_t rx_len    = 0;
static uint16_t rx_idx    = 0;
static uint8_t  iac_state = IAC_NORMAL;
static bool     prev_cr   = false;
static uint8_t  tx_prev   = 0;

static uart_driver_t net_drv;


static bool     cliNetOpen(uint32_t baud);
static bool     cliNetClose(void);
static uint32_t cliNetAvailable(void);
static bool     cliNetFlush(void);
static uint8_t  cliNetRead(void);
static uint32_t cliNetWrite(uint8_t *p_data, uint32_t length);

static void     clientClose(void);
static void     pushByte(uint8_t data);
static bool     sendAll(const uint8_t *p_data, uint32_t length);




bool cliNetInit(uint16_t port)
{
  net_port    = port;
  listen_sock = -1;
  client_sock = -1;

  net_drv.open      = cliNetOpen;
  net_drv.close     = cliNetClose;
  net_drv.available = cliNetAvailable;
  net_drv.flush     = cliNetFlush;
  net_drv.read      = cliNetRead;
  net_drv.write     = cliNetWrite;

  return uartSetDriver(HW_UART_CH_NET, &net_drv);
}

void cliNetPoll(void)
{
  /* listen 소켓은 netif 가 올라온 뒤에 한 번만 만든다. */
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
    addr.sin_port        = lwip_htons(net_port);

    if (lwip_bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        lwip_listen(listen_sock, 1) < 0)
    {
      lwip_close(listen_sock);
      listen_sock = -1;
      return;
    }

    lwip_fcntl(listen_sock, F_SETFL, O_NONBLOCK);
    logPrintf("[OK] cliNet : port %d\n", net_port);
  }

  if (client_sock < 0)
  {
    int fd = lwip_accept(listen_sock, NULL, NULL);

    if (fd >= 0)
    {
      /* 서버가 echo 와 SGA 를 맡는다고 알린다. 그래야 클라이언트가 줄 단위로
       * 모으지 않고 키를 한 글자씩 보낸다. */
      static const uint8_t nego[] =
      {
        TN_IAC, TN_WILL, TN_OPT_ECHO,
        TN_IAC, TN_WILL, TN_OPT_SGA,
      };
      int opt_on = 1;

      client_sock = fd;
      rx_len      = 0;
      rx_idx      = 0;
      iac_state   = IAC_NORMAL;
      prev_cr     = false;
      tx_prev     = 0;

      lwip_fcntl(client_sock, F_SETFL, O_NONBLOCK);
      lwip_setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &opt_on, sizeof(opt_on));
      lwip_send(client_sock, nego, sizeof(nego), 0);
      logPrintf("[  ] cliNet : 접속\n");
    }
  }
}

bool cliNetIsConnected(void)
{
  return (client_sock >= 0);
}

static bool cliNetOpen(uint32_t baud)
{
  (void)baud;
  return true;
}

static bool cliNetClose(void)
{
  return true;
}

static uint32_t cliNetAvailable(void)
{
  uint8_t raw[CLI_NET_BUF_LEN];
  int     rx_size;

  if (client_sock < 0) return 0;
  if (rx_idx < rx_len) return rx_len - rx_idx;

  rx_size = lwip_recv(client_sock, raw, sizeof(raw), 0);
  if (rx_size == 0)
  {
    clientClose();
    return 0;
  }
  if (rx_size < 0)
  {
    if (errno == EWOULDBLOCK || errno == EAGAIN) return 0;
    clientClose();
    return 0;
  }

  rx_len = 0;
  rx_idx = 0;

  for (int i = 0; i < rx_size; i++)
  {
    uint8_t data = raw[i];

    switch (iac_state)
    {
      case IAC_NORMAL:
        if (data == TN_IAC)
          iac_state = IAC_CMD;
        else
          pushByte(data);
        break;

      case IAC_CMD:
        if (data == TN_IAC)
        {
          pushByte(TN_IAC);
          iac_state = IAC_NORMAL;
        }
        else if (data == TN_WILL || data == TN_WONT || data == TN_DO || data == TN_DONT)
          iac_state = IAC_OPT;
        else if (data == TN_SB)
          iac_state = IAC_SB;
        else
          iac_state = IAC_NORMAL;
        break;

      case IAC_OPT:
        iac_state = IAC_NORMAL;
        break;

      case IAC_SB:
        if (data == TN_IAC) iac_state = IAC_SB_IAC;
        break;

      case IAC_SB_IAC:
        iac_state = (data == TN_SE) ? IAC_NORMAL : IAC_SB;
        break;
    }
  }

  return rx_len;
}

static bool cliNetFlush(void)
{
  rx_len = 0;
  rx_idx = 0;
  return true;
}

static uint8_t cliNetRead(void)
{
  if (rx_idx < rx_len) return rx_buf[rx_idx++];

  return 0;
}

static uint32_t cliNetWrite(uint8_t *p_data, uint32_t length)
{
  uint8_t  buf[CLI_NET_BUF_LEN];
  uint32_t index = 0;

  if (client_sock < 0) return 0;

  for (uint32_t i = 0; i < length; i++)
  {
    uint8_t data = p_data[i];

    if (data == '\n' && tx_prev != '\r')
    {
      buf[index++] = '\r';
      if (index >= sizeof(buf))
      {
        if (sendAll(buf, index) == false) { clientClose(); return 0; }
        index = 0;
      }
    }

    buf[index++] = data;
    tx_prev      = data;

    if (index >= sizeof(buf))
    {
      if (sendAll(buf, index) == false) { clientClose(); return 0; }
      index = 0;
    }
  }

  if (index > 0 && sendAll(buf, index) == false)
  {
    clientClose();
    return 0;
  }

  return length;
}

static void clientClose(void)
{
  if (client_sock >= 0)
  {
    lwip_close(client_sock);
    client_sock = -1;
  }
  rx_len    = 0;
  rx_idx    = 0;
  iac_state = IAC_NORMAL;
  prev_cr   = false;
  tx_prev   = 0;
}

/* CLI 코어는 Enter 를 CR 로만 인식한다. 터미널이 보내는 CRLF / LF / CR NUL 을
 * 전부 CR 하나로 맞춘다. */
static void pushByte(uint8_t data)
{
  if (data == '\r')
  {
    if (rx_len < sizeof(rx_buf)) rx_buf[rx_len++] = '\r';
    prev_cr = true;
    return;
  }

  if (data == '\n')
  {
    if (prev_cr)
    {
      prev_cr = false;
      return;
    }
    if (rx_len < sizeof(rx_buf)) rx_buf[rx_len++] = '\r';
    return;
  }

  if (data == 0x00)
  {
    prev_cr = false;
    return;
  }

  /* 대부분의 터미널은 Backspace 로 DEL 을 보낸다. */
  if (data == 0x7F) data = 0x08;

  prev_cr = false;
  if (rx_len < sizeof(rx_buf)) rx_buf[rx_len++] = data;
}

static bool sendAll(const uint8_t *p_data, uint32_t length)
{
  uint32_t offset = 0;

  while (offset < length)
  {
    int tx_size = lwip_send(client_sock, p_data + offset, length - offset, 0);

    if (tx_size <= 0)
    {
      if (tx_size < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) continue;
      return false;
    }
    offset += (uint32_t)tx_size;
  }
  return true;
}

#endif
