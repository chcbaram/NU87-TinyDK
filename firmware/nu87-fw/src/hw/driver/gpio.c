/*
 * gpio.c — P1/P2 확장 헤더 GPIO
 *
 * PA15 와 PB23 은 내부 풀업이 듣지 않아 입력으로 두면 0 으로 읽힌다.
 * 출력으로는 정상 동작하므로 입력으로 쓸 때만 외부 풀업을 단다.
 */

#include "gpio.h"


#ifdef _USE_HW_GPIO
#include "cli.h"


#define NAME_DEF(x)  x, #x


typedef struct
{
  uint8_t       pin;
  uint8_t       mode;
  bool          init_value;
  GpioPinName_t pin_name;
  const char   *p_name;
} gpio_tbl_t;


static const gpio_tbl_t gpio_tbl[GPIO_MAX_CH] =
{
  {_PA_15, _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P1_5_PA15) },
  {_PB_2,  _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P2_4_PB2)  },
  {_PB_1,  _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P2_5_PB1)  },
  {_PB_20, _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P2_7_PB20) },
  {_PB_21, _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P2_8_PB21) },
  {_PB_18, _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P2_9_PB18) },
  {_PB_19, _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P2_10_PB19)},
  {_PB_22, _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P2_11_PB22)},
  {_PB_23, _DEF_INPUT_PULLUP, _DEF_LOW, NAME_DEF(P2_12_PB23)},
};


#ifdef _USE_HW_CLI
static void cliGpio(cli_args_t *args);
#endif




bool gpioInit(void)
{
  for (int i = 0; i < GPIO_MAX_CH; i++)
  {
    gpioPinMode(i, gpio_tbl[i].mode);

    if (gpio_tbl[i].mode & _DEF_OUTPUT)
    {
      gpioPinWrite(i, gpio_tbl[i].init_value);
    }
  }

#ifdef _USE_HW_CLI
  cliAdd("gpio", cliGpio);
#endif

  return true;
}

bool gpioPinMode(uint8_t ch, uint8_t mode)
{
  GPIO_InitTypeDef gpio_init;

  if (ch >= GPIO_MAX_CH) return false;

  /* GPIO_Init() 이 Pinmux_Config() 와 PAD_PullCtrl() 까지 수행한다 */
  gpio_init.GPIO_Pin  = gpio_tbl[ch].pin;
  gpio_init.GPIO_Mode = (mode & _DEF_OUTPUT) ? GPIO_Mode_OUT : GPIO_Mode_IN;

  if (mode & _DEF_PULLUP)
  {
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
  }
  else if (mode & _DEF_PULLDOWN)
  {
    gpio_init.GPIO_PuPd = GPIO_PuPd_DOWN;
  }
  else
  {
    gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
  }

  GPIO_Init(&gpio_init);

  return true;
}

void gpioPinWrite(uint8_t ch, bool value)
{
  if (ch >= GPIO_MAX_CH) return;

  GPIO_WriteBit(gpio_tbl[ch].pin, value ? 1 : 0);
}

bool gpioPinRead(uint8_t ch)
{
  if (ch >= GPIO_MAX_CH) return false;

  return GPIO_ReadDataBit(gpio_tbl[ch].pin) ? true : false;
}

void gpioPinToggle(uint8_t ch)
{
  if (ch >= GPIO_MAX_CH) return;

  /* toggle API 가 없어 읽어서 반전한다 */
  GPIO_WriteBit(gpio_tbl[ch].pin, GPIO_ReadDataBit(gpio_tbl[ch].pin) ? 0 : 1);
}




#ifdef _USE_HW_CLI
void cliGpio(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    for (int i=0; i<GPIO_MAX_CH; i++)
    {
      cliPrintf("%d %-16s - %d\n", i, gpio_tbl[i].p_name, gpioPinRead(i));
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show") == true)
  {
    while(cliKeepLoop())
    {
      for (int i=0; i<GPIO_MAX_CH; i++)
      {
        cliPrintf("%02d %-16s - %d\n", i, gpio_tbl[i].p_name, gpioPinRead(i));
      }
      delay(100);
      cliMoveUp(GPIO_MAX_CH);
    }
    cliMoveDown(GPIO_MAX_CH);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "mode") == true)
  {
    uint8_t ch;
    uint8_t mode = 0;

    ch = (uint8_t)args->getData(1);

    if      (args->isStr(2, "in"))       mode = _DEF_INPUT;
    else if (args->isStr(2, "in_pu"))    mode = _DEF_INPUT_PULLUP;
    else if (args->isStr(2, "in_pd"))    mode = _DEF_INPUT_PULLDOWN;
    else if (args->isStr(2, "out"))      mode = _DEF_OUTPUT;

    if (mode != 0 && ch < GPIO_MAX_CH)
    {
      gpioPinMode(ch, mode);
      cliPrintf("gpio mode %d : %s\n", ch, args->getStr(2));
      ret = true;
    }
  }

  if (args->argc == 2 && args->isStr(0, "read") == true)
  {
    uint8_t ch;

    ch = (uint8_t)args->getData(1);

    while(cliKeepLoop())
    {
      cliPrintf("gpio read %d : %d\n", ch, gpioPinRead(ch));
      delay(100);
    }

    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "write") == true)
  {
    uint8_t ch;
    uint8_t data;

    ch   = (uint8_t)args->getData(1);
    data = (uint8_t)args->getData(2);

    gpioPinWrite(ch, data);

    cliPrintf("gpio write %d : %d\n", ch, data);
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("gpio info\n");
    cliPrintf("gpio show\n");
    cliPrintf("gpio mode ch[0~%d] in:in_pu:in_pd:out\n", GPIO_MAX_CH-1);
    cliPrintf("gpio read ch[0~%d]\n", GPIO_MAX_CH-1);
    cliPrintf("gpio write ch[0~%d] 0:1\n", GPIO_MAX_CH-1);
  }
}
#endif


#endif
