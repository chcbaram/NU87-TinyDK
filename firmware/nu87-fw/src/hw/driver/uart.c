/*
 * uart.c — LOGUART 드라이버 (RTL8720DF)
 *
 * 채널 1 개. LOGUART(PA7 TX / PA8 RX) 가 CP2102N 을 거쳐 USB-C 로 나간다.
 * 플래싱과 콘솔이 같은 포트를 공유한다.
 *
 * RX 는 인터럽트로 받아 qbuffer 에 쌓는다. 폴링으로 두면 CLI 가 도는 사이에
 * 하드웨어 FIFO 가 넘쳐 붙여넣기 한 문자열이 잘린다.
 *
 * TX 는 폴링이다. fault handler 가 인터럽트를 쓸 수 없는 상태에서 레지스터를
 * 덤프해야 한다.
 */

#include "uart.h"


#ifdef _USE_HW_UART
#include "qbuffer.h"
#include "cli.h"


#define UART_RX_BUF_LEN     256

/* RX 데이터 도착 + FIFO 가 덜 찬 채로 멈췄을 때의 타임아웃 */
#define UART_RX_IMR         (RUART_IER_ERBI | RUART_IER_ETOI)


typedef struct
{
  bool     is_open;
  uint32_t baud;
  uint32_t rx_cnt;
  uint32_t tx_cnt;

  uart_driver_t *p_driver;
} uart_tbl_t;


static const char *uart_name[UART_MAX_CH] =
{
  "LOGUART  PA7/PA8",
#ifdef _USE_HW_WIFI
  /* BLE 를 끄더라도 자리를 비워 두지 않는다. 채널 번호가 구성에 따라 바뀌면
   * ota write 에 넘기는 번호도 달라져 호스트 도구가 구성을 알아야 한다. */
  "NET      telnet",
  "BLE      cli",
  "BLE      data",
  "NET      data",
#endif
};

static uart_tbl_t uart_tbl[UART_MAX_CH];
static bool       is_init = false;

static qbuffer_t  rx_q;
static uint8_t    rx_buf[UART_RX_BUF_LEN];


static uint32_t uartLogIrq(void *data);
static bool     uartIsHw(uint8_t ch);

#ifdef _USE_HW_CLI
static void cliUart(cli_args_t *args);
#endif




bool uartInit(void)
{
  for (int i = 0; i < UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open  = false;
    uart_tbl[i].baud     = 115200;
    uart_tbl[i].rx_cnt   = 0;
    uart_tbl[i].tx_cnt   = 0;
    uart_tbl[i].p_driver = NULL;
  }

  qbufferCreate(&rx_q, rx_buf, UART_RX_BUF_LEN);

  InterruptRegister((IRQ_FUN)uartLogIrq, UART_LOG_IRQ, (uint32_t)NULL, 5);
  InterruptEn(UART_LOG_IRQ, 5);
  LOGUART_SetIMR(UART_RX_IMR);

#ifdef _USE_HW_CLI
  cliAdd("uart", cliUart);
#endif

  is_init = true;
  return true;
}

bool uartDeInit(void)
{
  is_init = false;
  return true;
}

bool uartIsInit(void)
{
  return is_init;
}

bool uartSetDriver(uint8_t ch, uart_driver_t *p_driver)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].p_driver = p_driver;
  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].baud = baud;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].is_open = uart_tbl[ch].p_driver->open(baud);
    return uart_tbl[ch].is_open;
  }

  if (uartIsHw(ch) == false) return false;

  /* KM0 부트로더가 LOGUART 를 115200 8N1 로 이미 설정해 둔다.
   * 플래싱 툴과 콘솔이 같은 포트를 공유하므로 보레이트를 바꾸지 않는다. */
  uart_tbl[ch].is_open = true;
  return true;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].p_driver->close();
  }

  uart_tbl[ch].is_open = false;
  return true;
}

bool uartIsOpen(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  return uart_tbl[ch].is_open;
}

uint32_t uartAvailable(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->available();
  }
  if (uartIsHw(ch) == false) return 0;

  return qbufferAvailable(&rx_q);
}

bool uartFlush(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->flush();
  }
  if (uartIsHw(ch) == false) return false;

  qbufferFlush(&rx_q);
  return true;
}

uint8_t uartRead(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->read();
  }
  if (uartIsHw(ch) == false) return 0;

  uint8_t data = 0;

  qbufferRead(&rx_q, &data, 1);
  uart_tbl[ch].rx_cnt++;
  return data;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  if (ch >= UART_MAX_CH) return 0;
  if (p_data == NULL) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->write(p_data, length);
  }
  if (uartIsHw(ch) == false) return 0;

  for (uint32_t i = 0; i < length; i++)
  {
    LOGUART_PutChar(p_data[i]);
  }
  LOGUART_WaitBusy();

  uart_tbl[ch].tx_cnt += length;
  return length;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len <= 0) return 0;
  if (len > (int)sizeof(buf)) len = sizeof(buf);

  return uartWrite(ch, (uint8_t *)buf, (uint32_t)len);
}

uint32_t uartGetBaud(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  return uart_tbl[ch].baud;
}

uint32_t uartGetRxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  return uart_tbl[ch].rx_cnt;
}

uint32_t uartGetTxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  return uart_tbl[ch].tx_cnt;
}

/* HW_UART_CH_LOG 만 실제 하드웨어다. 나머지는 드라이버를 등록해야 열린다.
 * 이 구분이 없으면 드라이버가 아직 안 붙은 가상 채널로 쓴 것이 LOGUART 로 샌다. */
bool uartIsHw(uint8_t ch)
{
  return (ch == HW_UART_CH_LOG);
}

/* ROM shell 핸들러와 같은 형태다. 처리 중에 IMR 을 내려 재진입을 막고,
 * FIFO 가 빌 때까지 한 번에 비운다. */
uint32_t uartLogIrq(void *data)
{
  uint32_t imr;
  uint8_t  ch;

  (void)data;

  imr = LOGUART_GetIMR();
  LOGUART_SetIMR(0);

  while (LOGUART_Readable())
  {
    ch = LOGUART_GetChar(_FALSE);
    qbufferWrite(&rx_q, &ch, 1);
  }

  LOGUART_SetIMR(imr);
  return 0;
}


#ifdef _USE_HW_CLI
void cliUart(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i = 0; i < UART_MAX_CH; i++)
    {
      cliPrintf("_DEF_UART%d : %s, %d bps%s\n",
                i + 1,
                uart_name[i],
                (int)uartGetBaud(i),
                (i == cliGetPort()) ? "  <- cli" : "");
      cliPrintf("             rx %d, tx %d bytes, rx queue %d/%d\n",
                (int)uartGetRxCnt(i), (int)uartGetTxCnt(i),
                (int)qbufferAvailable(&rx_q), UART_RX_BUF_LEN);
    }
    ret = true;
  }

  /* 수신 바이트를 16진으로 계속 찍는다. q 로 빠져나온다. */
  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint8_t ch = constrain(args->getData(1), 1, UART_MAX_CH) - 1;

    if (ch == cliGetPort())
    {
      cliPrintf("cli 가 쓰는 포트다\n");
    }
    else
    {
      while (cliKeepLoop())
      {
        if (uartAvailable(ch) > 0)
        {
          cliPrintf("<- _DEF_UART%d : 0x%02X\n", ch + 1, uartRead(ch));
        }
      }
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uart info\n");
    cliPrintf("uart test ch[1~%d]\n", UART_MAX_CH);
  }
}
#endif

#endif
