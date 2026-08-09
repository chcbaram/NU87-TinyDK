#ifndef CLI_BLE_H_
#define CLI_BLE_H_

#include "hw_def.h"

#ifdef __cplusplus
extern "C" {
#endif


#ifdef _USE_HW_BLE

bool cliBleIsConnected(void);   /* CLI 를 넘겨도 되는 상태인가 */

#endif

#ifdef __cplusplus
}
#endif

#endif
