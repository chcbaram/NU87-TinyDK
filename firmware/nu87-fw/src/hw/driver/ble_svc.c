/*
 * ble_svc.c — 바이트 통로 GATT 서비스
 *
 * 호스트와 임의의 바이트를 주고받는 통로 하나를 만든다. 여기서는 프로토콜을
 * 해석하지 않는다. 무엇을 주고받을지는 ap 계층이 정한다.
 *
 * 채널이 둘이다. 특성 쌍을 따로 두어 나눈다. 헤더를 붙여 다중화하지 않는 이유는
 * 호스트가 필요한 쪽만 구독하면 되고, OTA 처럼 굵게 흐르는 쪽이 CLI 에코에
 * 끼여 밀리지 않게 하기 위해서다.
 *
 *   CLI   0xFF88 write / 0xFF89 notify
 *   DATA  0xFF8A write / 0xFF8B notify
 *
 * 한 번에 보낼 수 있는 크기는 MTU-3 이다. 협상된 MTU 는 연결마다 다르므로
 * 상위가 쪼개서 넣는다.
 */

#include "ble_svc.h"


#ifdef _USE_HW_BLE
#include <profile_server.h>
#include <trace_app.h>


/* 전부 16비트 UUID 를 쓴다.
 *
 * 서비스는 16비트일 수밖에 없다. 속성 테이블의 type_value 가 2+14 바이트라
 * "PRIMARY_SERVICE(2) + 128비트 UUID(16)" 18 바이트가 들어가지 않는다.
 *
 * 특성도 16비트다. ATTRIB_FLAG_UUID_128BIT 로 128비트를 주면 서비스 등록은
 * 성공하는데 GATT 서버가 ATT 요청에 답하지 않아 연결이 supervision timeout
 * (0x0108)으로 끊긴다. 우리 서비스만 빼면 정상이고 UUID 만 16비트로 바꾸면
 * 정상이라는 것을 이분법으로 확인했다. bt_flags.h 에 관련 스위치는 없고,
 * 벤더의 128비트 예제(ble_matter_adapter)도 이 라이브러리에서는 같은 증상이다.
 *
 * Web Bluetooth 는 16비트 UUID 로도 그대로 필터하고 접근할 수 있으므로
 * 실사용에는 지장이 없다. */
#define BLE_SVC_UUID16            0xFF87    /* 서비스 */
#define BLE_SVC_UUID16_CLI_RX     0xFF88    /* CLI  호스트 -> 보드 */
#define BLE_SVC_UUID16_CLI_TX     0xFF89    /* CLI  보드 -> 호스트 */
#define BLE_SVC_UUID16_DAT_RX     0xFF8A    /* DATA 호스트 -> 보드 */
#define BLE_SVC_UUID16_DAT_TX     0xFF8B    /* DATA 보드 -> 호스트 */

/* 속성 테이블 안에서의 위치. 콜백이 어느 특성인지 이 값으로 가른다. */
#define BLE_SVC_IDX_CLI_RX        2
#define BLE_SVC_IDX_CLI_TX        4
#define BLE_SVC_IDX_CLI_CCCD      5
#define BLE_SVC_IDX_DAT_RX        7
#define BLE_SVC_IDX_DAT_TX        9
#define BLE_SVC_IDX_DAT_CCCD      10


static T_SERVER_ID    svc_id     = 0xFF;
static bool           is_notify_on[BLE_SVC_CH_MAX];
static ble_svc_rx_cb  rx_handler[BLE_SVC_CH_MAX];


static const T_ATTRIB_APPL ble_svc_tbl[] =
{
  /* 서비스 선언 */
  {
    (ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_LE),
    {
      LO_WORD(GATT_UUID_PRIMARY_SERVICE),
      HI_WORD(GATT_UUID_PRIMARY_SERVICE),
      LO_WORD(BLE_SVC_UUID16),
      HI_WORD(BLE_SVC_UUID16)
    },
    UUID_16BIT_SIZE,
    NULL,
    GATT_PERM_READ
  },

  /* CLI RX 특성 선언 */
  {
    ATTRIB_FLAG_VALUE_INCL,
    {
      LO_WORD(GATT_UUID_CHARACTERISTIC),
      HI_WORD(GATT_UUID_CHARACTERISTIC),
      (GATT_CHAR_PROP_WRITE | GATT_CHAR_PROP_WRITE_NO_RSP)
    },
    1,
    NULL,
    GATT_PERM_READ
  },
  /* CLI RX 값 */
  {
    ATTRIB_FLAG_VALUE_APPL,
    { LO_WORD(BLE_SVC_UUID16_CLI_RX), HI_WORD(BLE_SVC_UUID16_CLI_RX) },
    0,
    NULL,
    GATT_PERM_WRITE
  },

  /* CLI TX 특성 선언 */
  {
    ATTRIB_FLAG_VALUE_INCL,
    {
      LO_WORD(GATT_UUID_CHARACTERISTIC),
      HI_WORD(GATT_UUID_CHARACTERISTIC),
      (GATT_CHAR_PROP_NOTIFY)
    },
    1,
    NULL,
    GATT_PERM_READ
  },
  /* CLI TX 값 */
  {
    ATTRIB_FLAG_VALUE_APPL,
    { LO_WORD(BLE_SVC_UUID16_CLI_TX), HI_WORD(BLE_SVC_UUID16_CLI_TX) },
    0,
    NULL,
    GATT_PERM_NONE
  },
  /* CLI TX 의 CCCD. 호스트가 여기에 써야 notify 가 시작된다. */
  {
    (ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_CCCD_APPL),
    {
      LO_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
      HI_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
      LO_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT),
      HI_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT)
    },
    2,
    NULL,
    (GATT_PERM_READ | GATT_PERM_WRITE)
  },

  /* DATA RX 특성 선언 */
  {
    ATTRIB_FLAG_VALUE_INCL,
    {
      LO_WORD(GATT_UUID_CHARACTERISTIC),
      HI_WORD(GATT_UUID_CHARACTERISTIC),
      (GATT_CHAR_PROP_WRITE | GATT_CHAR_PROP_WRITE_NO_RSP)
    },
    1,
    NULL,
    GATT_PERM_READ
  },
  /* DATA RX 값 */
  {
    ATTRIB_FLAG_VALUE_APPL,
    { LO_WORD(BLE_SVC_UUID16_DAT_RX), HI_WORD(BLE_SVC_UUID16_DAT_RX) },
    0,
    NULL,
    GATT_PERM_WRITE
  },
  /* DATA TX 특성 선언 */
  {
    ATTRIB_FLAG_VALUE_INCL,
    {
      LO_WORD(GATT_UUID_CHARACTERISTIC),
      HI_WORD(GATT_UUID_CHARACTERISTIC),
      (GATT_CHAR_PROP_NOTIFY)
    },
    1,
    NULL,
    GATT_PERM_READ
  },
  /* DATA TX 값 */
  {
    ATTRIB_FLAG_VALUE_APPL,
    { LO_WORD(BLE_SVC_UUID16_DAT_TX), HI_WORD(BLE_SVC_UUID16_DAT_TX) },
    0,
    NULL,
    GATT_PERM_NONE
  },
  /* DATA TX 의 CCCD */
  {
    (ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_CCCD_APPL),
    {
      LO_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
      HI_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
      LO_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT),
      HI_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT)
    },
    2,
    NULL,
    (GATT_PERM_READ | GATT_PERM_WRITE)
  },
};


static T_APP_RESULT bleSvcAttrRead(uint8_t conn_id, T_SERVER_ID service_id,
                                   uint16_t attrib_index, uint16_t offset,
                                   uint16_t *p_length, uint8_t **pp_value);
static T_APP_RESULT bleSvcAttrWrite(uint8_t conn_id, T_SERVER_ID service_id,
                                    uint16_t attrib_index, T_WRITE_TYPE write_type,
                                    uint16_t length, uint8_t *p_value,
                                    P_FUN_WRITE_IND_POST_PROC *p_post_proc);
static void bleSvcCccdUpdate(uint8_t conn_id, T_SERVER_ID service_id,
                             uint16_t index, uint16_t cccbits);

static const T_FUN_GATT_SERVICE_CBS ble_svc_cbs =
{
  bleSvcAttrRead,
  bleSvcAttrWrite,
  bleSvcCccdUpdate
};




T_SERVER_ID bleSvcAddService(void *p_func)
{
  (void)p_func;

  if (server_add_service(&svc_id,
                         (uint8_t *)ble_svc_tbl,
                         sizeof(ble_svc_tbl),
                         ble_svc_cbs) == false)
  {
    svc_id = 0xFF;
  }

  return svc_id;
}

bool bleSvcSetRxHandler(uint8_t ch, ble_svc_rx_cb handler)
{
  if (ch >= BLE_SVC_CH_MAX) return false;

  rx_handler[ch] = handler;
  return true;
}

bool bleSvcIsNotifyEnabled(uint8_t ch)
{
  if (ch >= BLE_SVC_CH_MAX) return false;

  return is_notify_on[ch];
}

bool bleSvcSend(uint8_t ch, uint8_t conn_id, uint8_t *p_data, uint16_t length)
{
  if (ch >= BLE_SVC_CH_MAX)      return false;
  if (svc_id == 0xFF)            return false;
  if (is_notify_on[ch] == false) return false;

  return server_send_data(conn_id, svc_id,
                          (ch == BLE_SVC_CH_CLI) ? BLE_SVC_IDX_CLI_TX : BLE_SVC_IDX_DAT_TX,
                          p_data, length, GATT_PDU_TYPE_NOTIFICATION);
}

static T_APP_RESULT bleSvcAttrRead(uint8_t conn_id, T_SERVER_ID service_id,
                                   uint16_t attrib_index, uint16_t offset,
                                   uint16_t *p_length, uint8_t **pp_value)
{
  (void)conn_id; (void)service_id; (void)attrib_index;
  (void)offset;  (void)p_length;   (void)pp_value;

  return APP_RESULT_ATTR_NOT_FOUND;
}

static T_APP_RESULT bleSvcAttrWrite(uint8_t conn_id, T_SERVER_ID service_id,
                                    uint16_t attrib_index, T_WRITE_TYPE write_type,
                                    uint16_t length, uint8_t *p_value,
                                    P_FUN_WRITE_IND_POST_PROC *p_post_proc)
{
  (void)service_id; (void)write_type;

  uint8_t ch;

  *p_post_proc = NULL;

  if      (attrib_index == BLE_SVC_IDX_CLI_RX) ch = BLE_SVC_CH_CLI;
  else if (attrib_index == BLE_SVC_IDX_DAT_RX) ch = BLE_SVC_CH_DATA;
  else                                         return APP_RESULT_ATTR_NOT_FOUND;

  if (p_value == NULL) return APP_RESULT_INVALID_VALUE_SIZE;

  if (rx_handler[ch] != NULL)
  {
    rx_handler[ch](conn_id, p_value, length);
  }

  return APP_RESULT_SUCCESS;
}

static void bleSvcCccdUpdate(uint8_t conn_id, T_SERVER_ID service_id,
                             uint16_t index, uint16_t cccbits)
{
  (void)conn_id; (void)service_id;

  bool enable = (cccbits & GATT_CLIENT_CHAR_CONFIG_NOTIFY) ? true : false;

  if      (index == BLE_SVC_IDX_CLI_CCCD) is_notify_on[BLE_SVC_CH_CLI]  = enable;
  else if (index == BLE_SVC_IDX_DAT_CCCD) is_notify_on[BLE_SVC_CH_DATA] = enable;
}

#endif
