/*
 * ble.c — BLE (RTL8720DF)
 *
 * 스택 본체는 btgap.a 다. 여기서 하는 일은 기동 순서를 맞추고, 스택이 올려보내는
 * 메시지를 받아 도는 것뿐이다.
 *
 *   ftl_init      본딩키 저장소. 플래시 끝단 예약 영역을 쓴다 (docs/06 §6.4.1)
 *   bte_init      스택 기동
 *   le_gap_init   GAP 계층
 *   gap_start_bt_stack  이 시점부터 메시지 큐로 이벤트가 올라온다
 *   bt_coex_init  WiFi 와 라디오를 나눠 쓰는 중재기를 붙인다
 *
 * WiFi 가 먼저 올라와야 한다. 2.4GHz 라디오와 안테나가 하나뿐이라 공존 중재기가
 * WiFi 쪽 상태를 참조하기 때문이다.
 */

#include "ble.h"


#ifdef _USE_HW_BLE
#include "cli.h"
#include "wifi.h"

#include <gap.h>
#include <gap_adv.h>
#include <gap_bond_le.h>
#include <gap_config.h>
#include <gap_le.h>
#include <gap_msg.h>
#include <profile_server.h>
#include <bas.h>
#include <bte.h>
#include <os_msg.h>
#include <os_sched.h>
#include <trace_app.h>
#include <app_msg.h>

#include "osal/thread.h"

#include "ble_svc.h"
#include "uart.h"
#include "qbuffer.h"

#include "ftl_int.h"
#include "rtk_coex.h"


#define BLE_MAX_LINKS           1

#define BLE_ADV_INT_MIN         320       /* 0.625ms 단위 = 200ms */
#define BLE_ADV_INT_MAX         480       /* = 300ms */

#define BLE_MAX_IO_MSG          16
#define BLE_MAX_EVT_MSG         (BLE_MAX_IO_MSG + BLE_MAX_GAP_MSG)
#define BLE_MAX_GAP_MSG         16

/* 본딩키 저장 영역. NVS 바로 앞이다 (docs/06 §6.4.1) */
#define BLE_FTL_ADDR            0x003F8000
#define BLE_FTL_PAGES           3

#define BLE_RX_BUF_LEN          512
#define BLE_SEND_CHUNK          20
#define BLE_SEND_RETRY          200
#define BLE_SEND_RETRY_MS       5


static bool       is_init  = false;
static BleState_t state    = BLE_STATE_OFF;
static uint8_t    conn_id  = 0xFF;

static void *io_queue = NULL;
static void *evt_queue = NULL;

static T_SERVER_ID nu87_srv_id;
static T_SERVER_ID bas_srv_id;

/* 광고 패킷. 이름은 스캔 응답이 아니라 광고에 실어야 스캐너 목록에 바로 뜬다.
 * 광고 페이로드 상한은 31 바이트다. 앞 3 바이트는 flags AD 이고 나머지를
 * bleAdvDataSetName() 이 채운다. */
static uint8_t adv_data[31] =
{
  0x02, GAP_ADTYPE_FLAGS, GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
};

static uint8_t adv_data_len = 3;


/* 통로마다 받은 것을 담아 둔다. 상위는 폴링으로 꺼내 간다. */
typedef struct
{
  uint8_t       ch;
  qbuffer_t     rx_q;
  uint8_t       rx_buf[BLE_RX_BUF_LEN];
  uart_driver_t drv;
} ble_pipe_t;

static ble_pipe_t pipe[BLE_SVC_CH_MAX];

static uint8_t bleUartToSvcCh(uint8_t uart_ch);
static void    blePipeInit(void);
static void    bleThread(void *arg);
static void    bleStackStart(void);
static void bleGapParamInit(void);
static void bleProfileInit(void);
static void bleAdvDataSetName(void);
static void bleHandleGapMsg(T_IO_MSG *p_msg);
static T_APP_RESULT bleProfileCallback(T_SERVER_ID service_id, void *p_data);
static void bleHandleDevState(T_GAP_DEV_STATE state_new);
static void bleHandleConnState(uint8_t id, T_GAP_CONN_STATE state_new, uint16_t reason);

#ifdef _USE_HW_CLI
static void cliBle(cli_args_t *args);
#endif




bool bleInit(void)
{
  bleAdvDataSetName();
  blePipeInit();

  is_init = true;
  state   = BLE_STATE_INIT;

#ifdef _USE_HW_CLI
  cliAdd("ble", cliBle);
#endif

  logPrintf("[OK] bleInit()\n");

  return threadCreate("ble", bleThread, NULL,
                      _HW_DEF_RTOS_THREAD_PRI_BLE, _HW_DEF_RTOS_THREAD_MEM_BLE);
}

bool bleIsInit(void)
{
  return is_init;
}

BleState_t bleGetState(void)
{
  return state;
}

bool bleIsConnected(void)
{
  return (state == BLE_STATE_CONNECTED);
}

bool bleAdvertise(bool enable)
{
  if (state == BLE_STATE_OFF || state == BLE_STATE_INIT) return false;

  if (enable)
    return (le_adv_start() == GAP_CAUSE_SUCCESS);

  return (le_adv_stop() == GAP_CAUSE_SUCCESS);
}

bool bleGetMac(char *p_str, uint32_t length)
{
  uint8_t addr[GAP_BD_ADDR_LEN] = {0};

  if (!is_init) return false;
  if (gap_get_param(GAP_PARAM_BD_ADDR, addr) != GAP_CAUSE_SUCCESS) return false;

  snprintf(p_str, length, "%02X:%02X:%02X:%02X:%02X:%02X",
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  return true;
}

/* 호스트가 CCCD 를 켜야 보낼 수 있다. 연결만으로는 부족하다. */
bool bleIsReady(uint8_t uart_ch)
{
  return (bleIsConnected() && bleSvcIsNotifyEnabled(bleUartToSvcCh(uart_ch)));
}

static uint8_t bleUartToSvcCh(uint8_t uart_ch)
{
  return (uart_ch == HW_UART_CH_BLE_DATA) ? BLE_SVC_CH_DATA : BLE_SVC_CH_CLI;
}

static void bleSvcRxCli(uint8_t id, uint8_t *p_data, uint16_t length)
{
  (void)id;
  qbufferWrite(&pipe[BLE_SVC_CH_CLI].rx_q, p_data, length);
}

static void bleSvcRxData(uint8_t id, uint8_t *p_data, uint16_t length)
{
  (void)id;
  qbufferWrite(&pipe[BLE_SVC_CH_DATA].rx_q, p_data, length);
}

static bool blePipeOpen(uint32_t baud)
{
  (void)baud;
  return true;
}

static bool blePipeClose(void)
{
  return true;
}

static uint32_t blePipeAvailCli(void)  { return qbufferAvailable(&pipe[BLE_SVC_CH_CLI].rx_q); }
static uint32_t blePipeAvailData(void) { return qbufferAvailable(&pipe[BLE_SVC_CH_DATA].rx_q); }
static bool     blePipeFlushCli(void)  { qbufferFlush(&pipe[BLE_SVC_CH_CLI].rx_q);  return true; }
static bool     blePipeFlushData(void) { qbufferFlush(&pipe[BLE_SVC_CH_DATA].rx_q); return true; }

static uint8_t blePipeReadCli(void)
{
  uint8_t data = 0;
  qbufferRead(&pipe[BLE_SVC_CH_CLI].rx_q, &data, 1);
  return data;
}

static uint8_t blePipeReadData(void)
{
  uint8_t data = 0;
  qbufferRead(&pipe[BLE_SVC_CH_DATA].rx_q, &data, 1);
  return data;
}

/* notify 한 번에 실을 수 있는 크기는 MTU-3 이다. 협상 결과를 모르는 상태에서도
 * 안전하도록 최소 MTU(23) 기준으로 자른다.
 *
 * 스택의 송신 큐가 차면 server_send_data() 가 실패한다. 그냥 넘어가면 출력이
 * 소리없이 잘린다 — help 목록이 중간에서 끊기는 식이다. 큐가 빌 때까지 잠깐
 * 쉬었다 다시 넣는다. 여기는 CLI 스레드라 기다려도 된다. */
static uint32_t blePipeWrite(uint8_t svc_ch, uint8_t *p_data, uint32_t length)
{
  uint32_t offset = 0;

  if (bleIsConnected() == false || bleSvcIsNotifyEnabled(svc_ch) == false) return 0;

  while (offset < length)
  {
    uint32_t size = ((length - offset) > BLE_SEND_CHUNK) ? BLE_SEND_CHUNK : (length - offset);
    int      retry;

    for (retry = 0; retry < BLE_SEND_RETRY; retry++)
    {
      if (bleSvcSend(svc_ch, conn_id, &p_data[offset], size)) break;
      if (bleIsConnected() == false) return offset;
      delay(BLE_SEND_RETRY_MS);
    }
    if (retry >= BLE_SEND_RETRY) break;

    offset += size;
  }

  return offset;
}

static uint32_t blePipeWriteCli(uint8_t *p_data, uint32_t length)
{
  return blePipeWrite(BLE_SVC_CH_CLI, p_data, length);
}

static uint32_t blePipeWriteData(uint8_t *p_data, uint32_t length)
{
  return blePipeWrite(BLE_SVC_CH_DATA, p_data, length);
}

static void blePipeInit(void)
{
  const uint8_t uart_ch[BLE_SVC_CH_MAX] = { HW_UART_CH_BLE, HW_UART_CH_BLE_DATA };

  for (int i = 0; i < BLE_SVC_CH_MAX; i++)
  {
    pipe[i].ch = uart_ch[i];
    qbufferCreate(&pipe[i].rx_q, pipe[i].rx_buf, BLE_RX_BUF_LEN);

    pipe[i].drv.open  = blePipeOpen;
    pipe[i].drv.close = blePipeClose;

    uartSetDriver(uart_ch[i], &pipe[i].drv);
  }

  pipe[BLE_SVC_CH_CLI].drv.available  = blePipeAvailCli;
  pipe[BLE_SVC_CH_CLI].drv.flush      = blePipeFlushCli;
  pipe[BLE_SVC_CH_CLI].drv.read       = blePipeReadCli;
  pipe[BLE_SVC_CH_CLI].drv.write      = blePipeWriteCli;

  pipe[BLE_SVC_CH_DATA].drv.available = blePipeAvailData;
  pipe[BLE_SVC_CH_DATA].drv.flush     = blePipeFlushData;
  pipe[BLE_SVC_CH_DATA].drv.read      = blePipeReadData;
  pipe[BLE_SVC_CH_DATA].drv.write     = blePipeWriteData;
}

static void bleThread(void *arg)
{
  uint8_t event;

  (void)arg;

  /* 공존 중재기가 WiFi 상태를 참조한다. 라디오가 하나뿐이라 순서를 지켜야 한다. */
  while (wifiIsOn() == false)
  {
    delay(500);
  }

  bleStackStart();

  while (1)
  {
    if (os_msg_recv(evt_queue, &event, 0xFFFFFFFF) == true)
    {
      if (event == EVENT_IO_TO_APP)
      {
        T_IO_MSG io_msg;

        if (os_msg_recv(io_queue, &io_msg, 0) == true &&
            io_msg.type == IO_MSG_TYPE_BT_STATUS)
        {
          bleHandleGapMsg(&io_msg);
        }
      }
      else
      {
        gap_handle_msg(event);
      }
    }
  }
}

static void bleStackStart(void)
{
  ftl_init(BLE_FTL_ADDR, BLE_FTL_PAGES);

  bt_trace_init();

  gap_config_max_le_link_num(BLE_MAX_LINKS);
  gap_config_max_le_paired_device(BLE_MAX_LINKS);

  bte_init();
  le_gap_init(BLE_MAX_LINKS);

  bleGapParamInit();
  bleProfileInit();

  os_msg_queue_create(&io_queue,  BLE_MAX_IO_MSG,  sizeof(T_IO_MSG));
  os_msg_queue_create(&evt_queue, BLE_MAX_EVT_MSG, sizeof(uint8_t));

  gap_start_bt_stack(evt_queue, io_queue, BLE_MAX_GAP_MSG);

  bt_coex_init();
}

static void bleGapParamInit(void)
{
  uint8_t  device_name[GAP_DEVICE_NAME_LEN] = _DEF_BOARD_NAME;
  uint16_t appearance = GAP_GATT_APPEARANCE_UNKNOWN;
  uint8_t  slave_init_mtu_req = false;

  uint8_t  adv_evt_type      = GAP_ADTYPE_ADV_IND;
  uint8_t  adv_direct_type   = GAP_REMOTE_ADDR_LE_PUBLIC;
  uint8_t  adv_direct_addr[GAP_BD_ADDR_LEN] = {0};
  uint8_t  adv_chann_map     = GAP_ADVCHAN_ALL;
  uint8_t  adv_filter_policy = GAP_ADV_FILTER_ANY;
  uint16_t adv_int_min       = BLE_ADV_INT_MIN;
  uint16_t adv_int_max       = BLE_ADV_INT_MAX;

  uint8_t  auth_pair_mode = GAP_PAIRING_MODE_PAIRABLE;
  uint16_t auth_flags     = GAP_AUTHEN_BIT_BONDING_FLAG;
  uint8_t  auth_io_cap    = GAP_IO_CAP_NO_INPUT_NO_OUTPUT;
  uint8_t  auth_sec_req_enable = false;
  uint16_t auth_sec_req_flags  = GAP_AUTHEN_BIT_BONDING_FLAG;

  le_set_gap_param(GAP_PARAM_DEVICE_NAME, GAP_DEVICE_NAME_LEN, device_name);
  le_set_gap_param(GAP_PARAM_APPEARANCE, sizeof(appearance), &appearance);
  le_set_gap_param(GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ, sizeof(slave_init_mtu_req),
                   &slave_init_mtu_req);

  le_adv_set_param(GAP_PARAM_ADV_EVENT_TYPE, sizeof(adv_evt_type), &adv_evt_type);
  le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR_TYPE, sizeof(adv_direct_type), &adv_direct_type);
  le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR, sizeof(adv_direct_addr), adv_direct_addr);
  le_adv_set_param(GAP_PARAM_ADV_CHANNEL_MAP, sizeof(adv_chann_map), &adv_chann_map);
  le_adv_set_param(GAP_PARAM_ADV_FILTER_POLICY, sizeof(adv_filter_policy), &adv_filter_policy);
  le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MIN, sizeof(adv_int_min), &adv_int_min);
  le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MAX, sizeof(adv_int_max), &adv_int_max);
  le_adv_set_param(GAP_PARAM_ADV_DATA, adv_data_len, adv_data);

  gap_set_param(GAP_PARAM_BOND_PAIRING_MODE, sizeof(auth_pair_mode), &auth_pair_mode);
  gap_set_param(GAP_PARAM_BOND_AUTHEN_REQUIREMENTS_FLAGS, sizeof(auth_flags), &auth_flags);
  gap_set_param(GAP_PARAM_BOND_IO_CAPABILITIES, sizeof(auth_io_cap), &auth_io_cap);
  gap_set_param(GAP_PARAM_BOND_SEC_REQ_ENABLE, sizeof(auth_sec_req_enable), &auth_sec_req_enable);
  gap_set_param(GAP_PARAM_BOND_SEC_REQ_REQUIREMENT, sizeof(auth_sec_req_flags), &auth_sec_req_flags);
}

static void bleProfileInit(void)
{
  server_init(2);

  nu87_srv_id = bleSvcAddService((void *)bleProfileCallback);
  bleSvcSetRxHandler(BLE_SVC_CH_CLI,  bleSvcRxCli);
  bleSvcSetRxHandler(BLE_SVC_CH_DATA, bleSvcRxData);
  bas_srv_id  = bas_add_service((void *)bleProfileCallback);

  /* server_init() 에 알린 개수만큼 등록되지 않으면 GATT 서버가 데이터베이스를
   * 완성하지 못한다. 그러면 연결은 되는데 ATT 응답이 없어 supervision timeout
   * 으로 끊긴다. 조용히 넘기면 원인을 찾기 어렵다. */
  logPrintf("[%s] ble : 서비스 등록  nu87=%d bas=%d\n",
            (nu87_srv_id != 0xFF && bas_srv_id != 0xFF) ? "OK" : "E_",
            nu87_srv_id, bas_srv_id);

  server_register_app_cb(bleProfileCallback);
}

/* 광고 패킷의 이름 필드를 보드 이름으로 채운다. AD 구조는 [길이][타입][값] 이고
 * 길이는 타입까지 포함하므로 이름 길이 + 1 이다. */
static void bleAdvDataSetName(void)
{
  const char *p_name = _DEF_BOARD_NAME;
  uint32_t name_len = strlen(p_name);
  uint32_t index = 3;                     /* flags AD 3 바이트 뒤 */

  if (index + 2 + name_len > sizeof(adv_data)) return;

  adv_data[index++] = name_len + 1;
  adv_data[index++] = GAP_ADTYPE_LOCAL_NAME_COMPLETE;
  memcpy(&adv_data[index], p_name, name_len);

  adv_data_len = index + name_len;
}

/* GAP 상태 변화는 콜백이 아니라 IO 메시지 큐로 온다. 페이로드가 union 이라
 * 정렬이 보장되지 않아 벤더 예제처럼 통째로 복사한 뒤 읽는다. */
static void bleHandleGapMsg(T_IO_MSG *p_msg)
{
  T_LE_GAP_MSG gap_msg;

  memcpy(&gap_msg, &p_msg->u.param, sizeof(p_msg->u.param));

  switch (p_msg->subtype)
  {
    case GAP_MSG_LE_DEV_STATE_CHANGE:
      bleHandleDevState(gap_msg.msg_data.gap_dev_state_change.new_state);
      break;

    case GAP_MSG_LE_CONN_STATE_CHANGE:
      bleHandleConnState(gap_msg.msg_data.gap_conn_state_change.conn_id,
                         (T_GAP_CONN_STATE)gap_msg.msg_data.gap_conn_state_change.new_state,
                         gap_msg.msg_data.gap_conn_state_change.disc_cause);
      break;

    default:
      break;
  }
}

static T_APP_RESULT bleProfileCallback(T_SERVER_ID service_id, void *p_data)
{
  (void)service_id;
  (void)p_data;

  return APP_RESULT_SUCCESS;
}

static void bleHandleDevState(T_GAP_DEV_STATE state_new)
{
  if (state_new.gap_init_state == GAP_INIT_STATE_STACK_READY &&
      state == BLE_STATE_INIT)
  {
    char mac[32] = {0};

    state = BLE_STATE_IDLE;
    bleGetMac(mac, sizeof(mac));
    logPrintf("[OK] ble : 스택 준비  %s\n", mac);

    bleAdvertise(true);
  }

  if (state_new.gap_adv_state == GAP_ADV_STATE_ADVERTISING)
  {
    state = BLE_STATE_ADVERTISING;
    logPrintf("[  ] ble : 광고 시작  %s\n", _DEF_BOARD_NAME);
  }
  else if (state_new.gap_adv_state == GAP_ADV_STATE_IDLE &&
           state == BLE_STATE_ADVERTISING)
  {
    state = BLE_STATE_IDLE;
  }
}

static void bleHandleConnState(uint8_t id, T_GAP_CONN_STATE state_new, uint16_t reason)
{
  switch (state_new)
  {
    case GAP_CONN_STATE_CONNECTED:
      conn_id = id;
      state   = BLE_STATE_CONNECTED;
      logPrintf("[  ] ble : 연결\n");
      break;

    case GAP_CONN_STATE_DISCONNECTED:
      conn_id = 0xFF;
      state   = BLE_STATE_IDLE;
      logPrintf("[  ] ble : 연결 해제 (0x%04X)\n", reason);
      /* 연결이 끊기면 광고를 다시 켠다. 그래야 다음 접속을 받을 수 있다. */
      bleAdvertise(true);
      break;

    default:
      break;
  }
}


#ifdef _USE_HW_CLI
void cliBle(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const char *state_str[] = { "OFF", "INIT", "IDLE", "ADVERTISING", "CONNECTED" };
    char mac[32] = {0};

    cliPrintf("init  : %s\n", is_init ? "True" : "False");
    cliPrintf("state : %s\n", state_str[state]);
    cliPrintf("name  : %s\n", _DEF_BOARD_NAME);

    if (bleGetMac(mac, sizeof(mac))) cliPrintf("mac   : %s\n", mac);
    if (bleIsConnected())            cliPrintf("conn  : %d\n", conn_id);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "adv"))
  {
    bool enable = args->isStr(1, "on");

    cliPrintf("ble adv %s : %s\n", enable ? "on" : "off",
              bleAdvertise(enable) ? "OK" : "Fail");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ble info\n");
    cliPrintf("ble adv on|off\n");
  }
}
#endif

#endif
