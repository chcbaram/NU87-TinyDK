#ifndef IPC_H_
#define IPC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_IPC

#define IPC_CH_MAX      HW_IPC_CH_MAX

/* 하드웨어 USR 레지스터 개수. 이 위를 읽으면 0xDEADBEEF 가 나온다. */
#define IPC_HW_REG_MAX  12


bool     ipcInit(void);
bool     ipcSend(uint8_t ch, uint32_t msg);
uint32_t ipcRecv(uint8_t ch);

#endif

#ifdef __cplusplus
}
#endif

#endif
