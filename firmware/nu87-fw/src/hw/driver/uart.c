/*
 * uart.c — LOGUART 드라이버 (RTL8720DF)
 *
 * 채널 1 개. LOGUART(PA7 TX / PA8 RX) 가 CP2102N 을 거쳐 USB-C 로 나간다.
 * 플래싱과 콘솔이 같은 포트를 공유한다.
 *
 * 송수신 모두 ROM 함수를 직접 호출하는 폴링 방식이다:
 *   LOGUART_PutChar / LOGUART_GetChar / LOGUART_Readable / LOGUART_WaitBusy
 * LOGUART 에 하드웨어 FIFO 가 있고 cliMain() 이 moduleUpdate() 마다 호출되므로
 * 115200 타이핑 속도에는 충분하다.
 *
 * 한계: 긴 텍스트를 붙여넣으면 FIFO 를 넘겨 문자가 떨어질 수 있다. 그때는
 * InterruptRegister(UART_LOG_IRQ) + qbuffer 로 바꾼다. p_driver vtable 을
 * 유지했으므로 교체 범위가 이 파일 안에 머문다.
 */

#include "uart.h"


#ifdef _USE_HW_UART

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

  return LOGUART_Readable() ? 1 : 0;
}

bool uartFlush(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->flush();
  }

  while (LOGUART_Readable())
  {
    LOGUART_GetChar(_FALSE);
  }
  return true;
}

uint8_t uartRead(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->read();
  }

  uart_tbl[ch].rx_cnt++;
  return LOGUART_GetChar(_FALSE);
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

#endif
