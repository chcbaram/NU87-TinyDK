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


static uart_tbl_t uart_tbl[UART_MAX_CH];
static bool       is_init = false;

static qbuffer_t  rx_q;
static uint8_t    rx_buf[UART_RX_BUF_LEN];


static uint32_t uartLogIrq(void *data);




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

  return qbufferAvailable(&rx_q);
}

bool uartFlush(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->flush();
  }

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

#endif
