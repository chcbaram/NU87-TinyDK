cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

# 공유 SDK 영역. 부트로더 프로젝트와 함께 쓴다. (firmware/firm-sdk/README.md)
set(FIRM_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../firm-sdk")
set(REALTEK_DIR  "${FIRM_SDK_DIR}/lib/Realtek")

# 툴체인 파일은 아키텍처 플래그와 버전 검사를 담고 있다.
# 무선 단계에서 벤더 asdk GCC 로 갈아탈 때 이 파일만 교체한다.
set(CMAKE_MODULE_PATH "${FIRM_SDK_DIR}/tools")
include(arm-none-eabi-gcc)


# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#
set(PRJ_NAME nu87-fw)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


set(EXECUTABLE ${PRJ_NAME}.elf)


# 지정한 폴더에 있는 파일만 포함한다.
#
file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.cpp
  src/*.c
  src/bsp/*.c
  src/bsp/device/*.c
  src/bsp/rtos/*.c
)

# 하위폴더에 있는 파일까지 포함한다.
#
file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/*.cpp
  src/ap/*.c
  src/common/*.c
  src/hw/*.c
)

# 벤더 SDK 소스.
# STM32 프로젝트가 HAL Src/*.c 를 전부 넣고 --gc-sections 로 떨어내던 것과 같다.
# 어떤 파일이 KM4 빌드에 들어가는지는 SDK make/target/fwlib/Makefile 을 근거로 했다.
#
file(GLOB SRC_FILES_VENDOR CONFIGURE_DEPENDS
  ${REALTEK_DIR}/fwlib/ram_hp/*.c
  ${REALTEK_DIR}/fwlib/ram_common/*.c
  ${REALTEK_DIR}/fwlib/usrcfg/*.c
)

# ── FreeRTOS 사용 여부 ──────────────────────────────────────────────────────
#   ON  (기본) 커널을 링크하고 _USE_HW_RTOS 를 정의한다.
#              main() 이 mainThread 를 만들어 그 안에서 hwInit/apInit/apMain 을 돈다.
#   OFF        bare-metal. main() 이 그대로 부른다.
#
# 스위치를 CMake 에 두는 이유는 커널 소스 포함 여부와 매크로가 한 곳에서
# 결정되어야 어긋나지 않기 때문이다. hw_def.h 는 _USE_HW_RTOS 가 켜졌을 때의
# 스레드 상수만 갖는다.
#
#   cmake -S . -B build -DNU87_RTOS=OFF
option(NU87_RTOS "FreeRTOS 사용" ON)
message(STATUS "FreeRTOS: ${NU87_RTOS}")

# ── WiFi ────────────────────────────────────────────────────────────────────
# lib_wlan.a / lib_wps.a 를 링크하고 lwIP 와 osdep 을 함께 빌드한다.
# FreeRTOS 없이는 성립하지 않는다.
option(NU87_WIFI "WiFi 사용" OFF)
if(NU87_WIFI AND NOT NU87_RTOS)
  message(FATAL_ERROR "NU87_WIFI 는 NU87_RTOS 를 요구합니다")
endif()
message(STATUS "WiFi: ${NU87_WIFI}")

# ── BLE ─────────────────────────────────────────────────────────────────────
# btgap.a 를 링크하고 osif / HCI / 프로파일 소스를 함께 빌드한다.
# WiFi 와 라디오를 나눠 쓰므로 rtk_coex.c 가 둘 사이를 중재한다.
option(NU87_BLE "BLE 사용" OFF)
if(NU87_BLE AND NOT NU87_WIFI)
  message(FATAL_ERROR "NU87_BLE 는 NU87_WIFI 를 요구합니다 (라디오 공존 + osdep 공유)")
endif()
message(STATUS "BLE: ${NU87_BLE}")

set(FREERTOS_DIR ${REALTEK_DIR}/freertos)

if(NU87_RTOS)
  # 파일 목록은 SDK 의
  # project_hp/asdk/make/os/freertos/freertos_v10.2.0/Makefile 을 그대로 따른다.
  # 커널은 우리 코드와 같은 -Wall 기준으로 빌드한다 (벤더 완화 플래그 미적용).
  set(SRC_FILES_FREERTOS
    ${FREERTOS_DIR}/Source/tasks.c
    ${FREERTOS_DIR}/Source/list.c
    ${FREERTOS_DIR}/Source/croutine.c
    ${FREERTOS_DIR}/Source/queue.c
    ${FREERTOS_DIR}/Source/timers.c
    ${FREERTOS_DIR}/Source/event_groups.c
    ${FREERTOS_DIR}/Source/stream_buffer.c
    ${FREERTOS_DIR}/Source/portable/MemMang/heap_5.c
    ${FREERTOS_DIR}/Source/portable/GCC/RTL8721D_HP/non_secure/port.c
    ${FREERTOS_DIR}/Source/portable/GCC/RTL8721D_HP/non_secure/portasm.c
    ${FREERTOS_DIR}/freertos_backtrace_ext.c
  )
else()
  set(SRC_FILES_FREERTOS)
  # bsp/rtos/ 는 커널 없이는 의미가 없다
  list(FILTER SRC_FILES EXCLUDE REGEX "src/bsp/rtos/")
endif()


# ── WiFi 소스 ───────────────────────────────────────────────────────────────
# 파일 목록은 SDK 의 project_hp/asdk/make/{os/freertos,api/wifi,network} Makefile
# 을 그대로 따랐다. lwIP 는 소스 트리에 107 개가 있지만 SDK 가 쓰는 것은 49 개다.
set(SRC_FILES_WIFI)

if(NU87_WIFI)
  set(LWIP_DIR    ${REALTEK_DIR}/network/lwip)
  set(WIFIAPI_DIR ${REALTEK_DIR}/api)

  file(GLOB SRC_CRYPTO CONFIGURE_DEPENDS ${REALTEK_DIR}/fwlib/crypto/*.c)

  file(GLOB_RECURSE SRC_LWIP CONFIGURE_DEPENDS
    ${LWIP_DIR}/src/core/*.c
    ${LWIP_DIR}/src/api/*.c
    ${LWIP_DIR}/src/netif/ethernet.c
    ${LWIP_DIR}/port/realtek/freertos/*.c
  )

  set(SRC_FILES_WIFI
    ${SRC_LWIP}
    ${REALTEK_DIR}/network/dhcp/dhcps.c

    # mDNS(Bonjour). 구현은 lib_mdns.a 에 있고 이 파일은 lwIP 접착부 4 함수다.
    ${REALTEK_DIR}/network/mDNS/mDNSPlatform.c

    ${WIFIAPI_DIR}/lwip_netconf.c
    ${WIFIAPI_DIR}/wifi/wifi_util.c
    ${WIFIAPI_DIR}/wifi/wifi_conf.c
    ${WIFIAPI_DIR}/wifi/wifi_promisc.c
    ${WIFIAPI_DIR}/wifi/wifi_ind.c

    ${REALTEK_DIR}/wlan/osdep/lwip_intf.c
    # rtw_opt_skbuf / rtw_opt_wlan_ram_map 은 이미 lib_wlan.a 안에 있다.
    # 함께 컴파일하면 중복 정의가 되고, 응용이 제공해야 하는
    # sram0_reserve_malloc/free 까지 요구하게 된다.
    ${REALTEK_DIR}/wlan/option/rtw_opt_crypto_ssl.c
    ${REALTEK_DIR}/wlan/option/rtw_opt_rf_para_rtl8721d.c

    # mbedTLS 는 구현이 ROM 에 있지만 이 함수만 밖에 있다.
    ${REALTEK_DIR}/network/ssl/mbedtls/library/platform.c
    # ROM mbedTLS 의 할당자를 등록한다 (platform_set_malloc_free)
    ${REALTEK_DIR}/network/ssl/ssl_ram_map/ssl_ram_map.c

    # 하드웨어 암호 엔진 (rtl_crypto_*)
    ${SRC_CRYPTO}
    ${REALTEK_DIR}/api/wifi/rtw_wpa_supplicant/wpa_supplicant/wifi_eap_config.c

    # PAD_CMD 하나 때문에 가져온 KM0 쪽 파일. 나머지 ram_lp 는 넣지 않는다.
    ${REALTEK_DIR}/fwlib/ram_lp_pinmap/rtl8721dlp_pinmap.c

    ${REALTEK_DIR}/os_dep/osdep_service.c
    ${REALTEK_DIR}/os_dep/device_lock.c
    ${REALTEK_DIR}/freertos/freertos_service.c
    ${REALTEK_DIR}/freertos/freertos_heap5_config.c
  )
endif()


# ── BLE 소스 ────────────────────────────────────────────────────────────────
# 목록 근거는 SDK 의 project_hp/asdk/make/rtl_bluetooth/*/Makefile 이다.
set(SRC_FILES_BLE)

if(NU87_BLE)
  set(BT_DIR ${REALTEK_DIR}/bluetooth)

  set(SRC_FILES_BLE
    # OS 추상화. btgap.a 외부 의존 63 개 중 45 개가 여기서 나온다.
    ${BT_DIR}/board/common/os/freertos/osif_freertos.c

    # HCI 전송. btgap.a 가 hci_tp_* 로 부른다.
    ${BT_DIR}/board/common/src/hci_adapter.c
    ${BT_DIR}/board/common/src/hci_process.c
    ${BT_DIR}/board/common/src/cycle_queue.c
    ${BT_DIR}/board/common/src/trace_task.c
    # hci_uart.c 가 bt_uart_tx 를 참조한다. SDK 빌드 목록에도 있다.
    ${BT_DIR}/board/common/src/bt_uart_bridge.c

    # rtl8721d_bt.c 는 넣지 않는다. 이미 lib_wlan.a 안에 있고
    # (bt_get_patch_code_8721d / Hal_BT_Is_Supported / rtlbt_fw)
    # SDK 의 BT 빌드 목록에도 없다. 참조용으로만 벤더링돼 있다.
    ${BT_DIR}/board/amebad/src/platform_utils.c
    ${BT_DIR}/board/amebad/src/trace_uart.c
    # WiFi 와의 라디오 공존 우편함
    ${BT_DIR}/board/amebad/src/rtk_coex.c
    ${BT_DIR}/board/amebad/src/hci/hci_board.c
    ${BT_DIR}/board/amebad/src/hci/hci_uart.c
    ${BT_DIR}/board/amebad/src/hci/bt_fwconfig.c
    # lib_wlan.a 의 rtlbt_fw 는 weak 인 빈 배열이다. 실제 패치는 여기서 온다.
    ${BT_DIR}/board/amebad/src/hci/bt_normal_patch.c
    ${BT_DIR}/board/amebad/src/hci/bt_mp_patch.c
    ${BT_DIR}/board/amebad/src/vendor_cmd/vendor_cmd.c

    # GATT 프로파일. 1단계는 연결 확인용이고 우리 서비스는 2단계에 추가한다.
    ${BT_DIR}/src/ble/profile/server/bas.c

    # hci_board.c 가 ota_get_cur_index() 로 어느 슬롯에서 부팅했는지 물어
    # BT ROM 패치 주소를 고른다.
    ${REALTEK_DIR}/misc/rtl8721d_ota.c

    # 본딩키 저장. 플래시 끝단 예약 영역을 쓴다 (docs/06 §6.4.1)
    ${REALTEK_DIR}/file_system/ftl/ftl.c
    ${REALTEK_DIR}/mbed/flash_api.c
    # hci_board.c 가 BT MAC 을 efuse 에서 읽는다.
    ${REALTEK_DIR}/mbed/efuse_api.c
  )
endif()


# 특정 파일을 빌드에서 제외한다.
#
# 근거는 docs/05-build-system.md §5.4 에 정리했다. 요약:
#   - 우리 파생본이 대체하는 것
#   - KM0 / 부트로더 몫
#   - 같은 심볼을 중복 정의하는 패키지 변종
#   - SDK KM4 목록에 없는 것
#   - FreeRTOS 를 요구하는 것 (1단계는 bare-metal)
#   - 아직 RTL 용으로 작성하지 않은 우리 드라이버
#
set(EXCLUDE_PATHS
  # 우리 파생본(src/bsp/device/nu87_app_start.c)이 대체한다. 원본은 대조용으로 남긴다.
  fwlib/ram_hp/rtl8721dhp_app_start.c

  # 부트로더 / KM0 몫
  fwlib/usrcfg/rtl8721d_bootcfg.c
  fwlib/usrcfg/rtl8721dhp_boot_trustzonecfg.c
  fwlib/usrcfg/rtl8721dlp_flashcfg.c
  fwlib/usrcfg/rtl8721dlp_sleepcfg.c
  fwlib/usrcfg/rtl8721dlp_pinmapcfg.c
  # QFN88 EVB 변종. 위 pinmapcfg 와 같은 심볼을 정의해 함께 빌드하면 중복 정의가 된다.
  fwlib/usrcfg/rtl8721dlp_pinmapcfg_qfn88_evb_v1.c

  # SDK KM4 빌드 목록에 없다 (ROM 에 있거나 이 칩에 없는 주변장치)
  fwlib/ram_common/rtl8721d_efuse.c
  fwlib/ram_common/rtl8721d_qdec.c

  # FreeRTOS 필요 — 무선/OTA 단계에서 되살린다
  fwlib/ram_hp/rtl8721dhp_sd.c            # pvPortMalloc / vPortFree
  fwlib/ram_hp/rtl8721dhp_simulation.c    # xTaskCreate / vTaskDelay
  fwlib/ram_common/rtl8721d_gdma_ram.c    # osdep_service.h
)

foreach(TMP_PATH ${SRC_FILES_RECURSE} ${SRC_FILES_VENDOR})
  foreach(EXCLUDE_PATH ${EXCLUDE_PATHS})
    string(FIND ${TMP_PATH} ${EXCLUDE_PATH} RESULT)
    if(NOT ${RESULT} EQUAL -1)
      list(REMOVE_ITEM SRC_FILES_RECURSE ${TMP_PATH})
      list(REMOVE_ITEM SRC_FILES_VENDOR  ${TMP_PATH})
    endif()
  endforeach(EXCLUDE_PATH)
endforeach(TMP_PATH)


add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
  ${SRC_FILES_VENDOR}
  ${SRC_FILES_FREERTOS}
  ${SRC_FILES_WIFI}
  ${SRC_FILES_BLE}
)


target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/ap/modules
  src/ap/modules/common
  src/bsp
  src/bsp/device          # 우리 platform_autoconf.h / platform_opts.h / autoconf.h
  src/bsp/rtos            # 우리 FreeRTOSConfig.h (SDK 예제 것이 아니다)
  src/common
  src/common/core
  src/common/hw/include
  src/hw
  src/hw/driver

  # 벤더 SDK.
  # ★ inc_hp/ 는 넣지 않는다. 그 안의 platform_autoconf.h / platform_opts.h 는
  #   SDK 예제 설정(WiFi/lwIP/mbedTLS 전부 on)이라 참조용으로만 둔 것이고,
  #   실제로 쓰는 것은 위 src/bsp/device/ 의 우리 버전이다.
  ${REALTEK_DIR}/fwlib/include
  ${REALTEK_DIR}/cmsis
  ${REALTEK_DIR}/swlib/include
  ${REALTEK_DIR}/swlib/string
  ${REALTEK_DIR}/app/monitor/include
  ${REALTEK_DIR}/app/xmodem          # rtl8721d.h 가 xmodem_update_rom.h 를 include
  ${REALTEK_DIR}/os_dep/include
  ${REALTEK_DIR}/misc
  ${REALTEK_DIR}/imgtool_floader/include
)

if(NU87_RTOS)
  target_include_directories(${EXECUTABLE} PRIVATE
    ${FREERTOS_DIR}
    ${FREERTOS_DIR}/Source/include
    ${FREERTOS_DIR}/Source/portable/GCC/RTL8721D_HP/non_secure
  )
endif()

if(NU87_BLE)
  target_include_directories(${EXECUTABLE} PRIVATE
    ${REALTEK_DIR}/bluetooth
    ${REALTEK_DIR}/bluetooth/inc
    ${REALTEK_DIR}/bluetooth/inc/app
    ${REALTEK_DIR}/bluetooth/inc/os
    ${REALTEK_DIR}/bluetooth/inc/platform
    ${REALTEK_DIR}/bluetooth/inc/stack
    ${REALTEK_DIR}/bluetooth/inc/bluetooth/gap
    ${REALTEK_DIR}/bluetooth/inc/bluetooth/gap/gap_lib
    ${REALTEK_DIR}/bluetooth/inc/bluetooth/profile
    ${REALTEK_DIR}/bluetooth/inc/bluetooth/profile/client
    ${REALTEK_DIR}/bluetooth/inc/bluetooth/profile/server
    ${REALTEK_DIR}/bluetooth/board/common/inc
    ${REALTEK_DIR}/bluetooth/board/amebad/src
    ${REALTEK_DIR}/bluetooth/board/amebad/src/hci
    ${REALTEK_DIR}/bluetooth/board/amebad/src/vendor_cmd
    ${REALTEK_DIR}/bluetooth/board/amebad/lib
    ${REALTEK_DIR}/file_system/ftl
    ${REALTEK_DIR}/api/at_cmd
    ${REALTEK_DIR}/mbed
    ${REALTEK_DIR}/mbed/hal
    ${REALTEK_DIR}/mbed/hal_ext
  )
  target_link_libraries(${EXECUTABLE} PRIVATE
    ${REALTEK_DIR}/bluetooth/board/amebad/lib/btgap.a
  )

  # 벤더 코드다. 우리 소스와 같은 -Wall 기준을 강요하지 않는다.
  set_source_files_properties(
    ${SRC_FILES_BLE}
    PROPERTIES COMPILE_OPTIONS "${NU87_VENDOR_RELAX_FLAGS}"
  )
endif()

if(NU87_WIFI)
  target_include_directories(${EXECUTABLE} PRIVATE
    ${REALTEK_DIR}/os_dep/include
    ${REALTEK_DIR}/wlan/include
    ${REALTEK_DIR}/wlan/osdep
    ${REALTEK_DIR}/api
    ${REALTEK_DIR}/api/wifi
    ${REALTEK_DIR}/api/network/include
    ${REALTEK_DIR}/api/platform
    ${REALTEK_DIR}/network/lwip/src/include
    ${REALTEK_DIR}/network/lwip/src/include/netif
    ${REALTEK_DIR}/network/lwip/port/realtek
    ${REALTEK_DIR}/network/lwip/port/realtek/freertos
    ${REALTEK_DIR}/network            # wifi_conf.c 가 "dhcp/dhcps.h" 로 include
    ${REALTEK_DIR}/network/dhcp
    # mbedTLS 는 구현이 ROM 에 있고 헤더만 필요하다
    ${REALTEK_DIR}/network/ssl/mbedtls/include
    ${REALTEK_DIR}/network/ssl/ssl_ram_map/rom
    ${REALTEK_DIR}/wlan/wlan_ram_map/rom
    ${REALTEK_DIR}/api/wifi/rtw_wpa_supplicant/src
  )

  # 벤더 코드다. 우리 소스와 같은 -Wall 기준을 강요하지 않는다.
  set_source_files_properties(
    ${SRC_FILES_WIFI}
    PROPERTIES COMPILE_OPTIONS "${NU87_VENDOR_RELAX_FLAGS}"
  )

  # src/include/lwip 은 -idirafter 여야 한다. 일반 -I 로 넣으면 lwip/xxx.h 가
  # xxx.h 로도 잡혀 표준 헤더나 다른 헤더를 가린다 (SDK Makefile.include.gen 과 동일).
  target_compile_options(${EXECUTABLE} PRIVATE
    -idirafter ${REALTEK_DIR}/network/lwip/src/include/lwip
  )

  # 소스가 공개되지 않은 무선 아카이브. 서로를 참조하므로 그룹으로 묶는다.
  target_link_libraries(${EXECUTABLE} PRIVATE
    -Wl,--start-group
    ${REALTEK_DIR}/lib/application/lib_wlan.a
    ${REALTEK_DIR}/lib/application/lib_wps.a
    ${REALTEK_DIR}/lib/application/lib_pmc_hp.a
    ${REALTEK_DIR}/lib/application/lib_eap.a
    ${REALTEK_DIR}/lib/application/lib_mdns.a
    -Wl,--end-group
  )
endif()


target_compile_definitions(${EXECUTABLE} PRIVATE
  -DCONFIG_PLATFORM_8721D
  # 명령줄로 정의해야 bsp.h 가 hw_def.h 보다 먼저 처리될 때도 값이 잡힌다.
  $<$<BOOL:${NU87_RTOS}>:_USE_HW_RTOS>
  $<$<BOOL:${NU87_WIFI}>:_USE_HW_WIFI>
  $<$<BOOL:${NU87_BLE}>:_USE_HW_BLE>
)


# 아키텍처 플래그는 툴체인 파일이 NU87_ARCH_FLAGS 로 내보낸다.
target_compile_options(${EXECUTABLE} PRIVATE
  ${NU87_ARCH_FLAGS}

  -fdata-sections
  -ffunction-sections
  -fno-common

  -fstack-usage
  -Wall
  -Wmissing-field-initializers
  -Wuninitialized

  -g3
  -Og
)

# 벤더 SDK 소스에만 완화 플래그를 적용한다.
# 우리 코드(src/ap, src/hw, src/bsp)는 -Wall 을 엄격하게 유지한다.
# GCC 14 가 int-conversion 등을 기본 error 로 승격했고 벤더 코드는 GCC 6.4.1 기준이다.
if(SRC_FILES_VENDOR)
  set_source_files_properties(
    ${SRC_FILES_VENDOR}
    PROPERTIES COMPILE_OPTIONS "${NU87_VENDOR_RELAX_FLAGS}"
  )
endif()

# 아래 벤더 파일들은 FreeRTOS API 를 쓰면서 헤더를 include 하지 않는다.
# 벤더 소스를 고치면 SDK 재동기화 때마다 되살아나므로 강제 include 로 해결한다.
# freertos_backtrace_ext.h 가 tskKERNEL_VERSION_MAJOR 로 자기 선언을 감싸므로
# FreeRTOS.h -> task.h 를 먼저 넣어야 선언이 살아난다.
# ★ 위의 일반 벤더 설정보다 뒤여야 한다. 나중 호출이 앞의 것을 덮어쓴다.
if(NU87_RTOS)
  set_source_files_properties(
    ${REALTEK_DIR}/fwlib/ram_common/rtl8721d_ipc_api.c        # vTaskStackAddr / vTaskStackSize
    ${REALTEK_DIR}/fwlib/ram_common/rtl8721d_flash_ram.c      # xTaskGetSchedulerState / xTaskIncrementTick
    PROPERTIES COMPILE_OPTIONS
      "${NU87_VENDOR_RELAX_FLAGS};-include;FreeRTOS.h;-include;task.h;-include;freertos_backtrace_ext.h"
  )
endif()


# ── 이미지 배치 선택 ────────────────────────────────────────────────────────
#   xip   (기본) 코드·상수를 플래시에서 실행한다. 무선 라이브러리와 OTA 슬롯의
#         전제이고, SRAM 을 버퍼용으로 통째로 남긴다.
#   sram  코드·상수까지 전부 SRAM. GDB load 로 바로 올릴 수 있고 SW 브레이크포인트가
#         되므로 반복 개발이 빠르다. 다만 476KB 를 넘길 수 없다.
#
# ★ sram 배치에서만 되는 것 — 아래를 쓰려면 -DNU87_LAYOUT=sram 으로 재구성한다.
#     load-sram 태스크 / "Load to SRAM & Run KM4" 디버그 구성
#     소프트웨어 브레이크포인트 (xip 는 하드웨어 BP 2개뿐)
#   xip 배치에서 GDB load 는 실패한다. OpenOCD 에 RTL872x 플래시 드라이버가
#   없어서 XIP 영역 쓰기가 D-캐시만 오염시키고 타깃이 불안정해진다.
#   xip 는 flash.py(UART)로 굽는다.
set(NU87_LAYOUT "xip" CACHE STRING "이미지 배치: xip | sram")
set_property(CACHE NU87_LAYOUT PROPERTY STRINGS xip sram)

if(NU87_LAYOUT STREQUAL "xip")
  set(NU87_LDSCRIPT_IMG ${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/nu87_km4_img2_xip.ld)
elseif(NU87_LAYOUT STREQUAL "sram")
  set(NU87_LDSCRIPT_IMG ${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/nu87_km4_img2.ld)
else()
  message(FATAL_ERROR "NU87_LAYOUT 은 sram 또는 xip 여야 합니다: ${NU87_LAYOUT}")
endif()
message(STATUS "이미지 배치: ${NU87_LAYOUT}")


target_link_options(${EXECUTABLE} PRIVATE
  ${NU87_ARCH_FLAGS}

  -T${NU87_LDSCRIPT_IMG}
  # 칩 마스크 ROM 심볼. "symbol = 0xADDR;" 나열일 뿐인 평문이다.
  -T${REALTEK_DIR}/ld/rlx8721d_rom_symbol_acut.ld

  -specs=nano.specs
  -specs=nosys.specs
  -lc
  -lm
  -lstdc++
  -lsupc++
  -fno-rtti

  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Wl,--print-memory-usage
)

# 링커스크립트가 바뀌면 다시 링크한다.
set(NU87_LDSCRIPTS
  ${NU87_LDSCRIPT_IMG}
  ${REALTEK_DIR}/ld/rlx8721d_rom_symbol_acut.ld
  # 이미지 생성기와 KM0 블롭이 바뀌어도 이미지를 다시 만들어야 한다.
  # POST_BUILD 는 링크에 매달려 있어 링크가 안 걸리면 실행되지 않는다.
  ${FIRM_SDK_DIR}/tools/make_image.py
  ${FIRM_SDK_DIR}/prebuilt/km0_image2_all.bin
)
set_target_properties(${EXECUTABLE} PROPERTIES LINK_DEPENDS "${NU87_LDSCRIPTS}")

set(CMAKE_C_FLAGS   "-Wenum-conversion")
set(CMAKE_CXX_FLAGS "-fno-rtti")

if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 11)
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,--no-warn-rwx-segments")
endif()


# 링크 결과를 부팅 가능한 이미지로 만든다.
#   .ram_image2.* 섹션 추출 → 32바이트 헤더 부착 → KM0 스톡 블롭과 결합
# 벤더 스크립트(prepend_header.sh + 플랫폼별 checksum 바이너리)를 쓰지 않고
# Python 으로 처리한다 (Windows/macOS/Linux 공통).
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary ${EXECUTABLE} ${PRJ_NAME}.bin
  COMMAND ${Python3_EXECUTABLE} ${FIRM_SDK_DIR}/tools/make_image.py
          --elf     ${EXECUTABLE}
          --map     ${PRJ_NAME}.map
          --objcopy ${CMAKE_OBJCOPY}
          --prebuilt ${FIRM_SDK_DIR}/prebuilt
          --outdir  .
  COMMENT "이미지 생성 (km0_km4_image2.bin)"
  VERBATIM
)
