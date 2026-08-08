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
  fwlib/ram_common/rtl8721d_flash_ram.c   # xTaskGetSchedulerState — OTA 단계에서 필요
  fwlib/ram_common/rtl8721d_ipc_api.c     # vTaskStackAddr — 무선 단계에서 필수
  fwlib/ram_common/rtl8721d_gdma_ram.c    # osdep_service.h

  # 아직 RTL 용으로 작성하지 않은 우리 드라이버 (hw_def.h 에서 스위치도 꺼 두었다)
  hw/driver/gpio.c
  hw/driver/reset.c
  hw/driver/rtc.c
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
)


target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/ap/modules
  src/ap/modules/common
  src/bsp
  src/bsp/device          # 우리 platform_autoconf.h / platform_opts.h / autoconf.h
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


target_compile_definitions(${EXECUTABLE} PRIVATE
  -DCONFIG_PLATFORM_8721D
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


target_link_options(${EXECUTABLE} PRIVATE
  ${NU87_ARCH_FLAGS}

  # 우리 이미지 배치 (전량 SRAM)
  -T${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/nu87_km4_img2.ld
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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/nu87_km4_img2.ld
  ${REALTEK_DIR}/ld/rlx8721d_rom_symbol_acut.ld
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
