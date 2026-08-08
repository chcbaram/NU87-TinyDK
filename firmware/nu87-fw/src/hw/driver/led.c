/*
 * led.c — NU87-TinyDK RGB LED
 *
 * 회로:
 *   PA13 --0R--> U4B --470R--> D8 pin2  RED
 *   PA12 --0R--> U4A --470R--> D8 pin1  GREEN
 *   PA14 --0R--> U4C --470R--> D8 pin3  BLUE
 *
 * U4 = SN74LVC3G17 비반전 슈미트 버퍼, D8 은 공통 캐소드(GND) 이므로
 * Active-High 다. 1 을 쓰면 켜진다.
 *
 * GPIO_Init / GPIO_WriteBit / GPIO_ReadDataBit 는 칩 마스크 ROM 에 있다.
 */

#include "led.h"


#ifdef _USE_HW_LED


typedef struct
{
  uint8_t pin;         // _PA_x / _PB_x
  uint8_t on_state;    // _DEF_HIGH = Active-High
} led_tbl_t;


static const led_tbl_t led_tbl[LED_MAX_CH] =
{
  {_PA_13, _DEF_HIGH},   // _DEF_LED1  RED
  {_PA_12, _DEF_HIGH},   // _DEF_LED2  GREEN
  {_PA_14, _DEF_HIGH},   // _DEF_LED3  BLUE
};




bool ledInit(void)
{
  GPIO_InitTypeDef gpio_init;

  for (int i = 0; i < LED_MAX_CH; i++)
  {
    /* GPIO_Init() 이 Pinmux_Config(PINMUX_FUNCTION_GPIO) 와 PAD_PullCtrl() 을
     * 내부에서 수행하므로 따로 호출하지 않는다. */
    gpio_init.GPIO_Pin  = led_tbl[i].pin;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(&gpio_init);

    ledOff(i);
  }

  return true;
}

void ledOn(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  GPIO_WriteBit(led_tbl[ch].pin, led_tbl[ch].on_state);
}

void ledOff(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  GPIO_WriteBit(led_tbl[ch].pin, led_tbl[ch].on_state ? 0 : 1);
}

void ledToggle(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  /* RTL8720DF 에는 GPIO toggle API 가 없다. 읽어서 반전한다. */
  GPIO_WriteBit(led_tbl[ch].pin, GPIO_ReadDataBit(led_tbl[ch].pin) ? 0 : 1);
}

#endif
