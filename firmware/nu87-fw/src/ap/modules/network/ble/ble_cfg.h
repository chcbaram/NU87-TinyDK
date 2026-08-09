#ifndef BLE_CFG_H_
#define BLE_CFG_H_

#include "ap_def.h"


#ifdef __cplusplus
extern "C" {
#endif


#if defined(_USE_HW_BLE) && defined(_USE_HW_WIFI)

bool bleCfgInit(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
