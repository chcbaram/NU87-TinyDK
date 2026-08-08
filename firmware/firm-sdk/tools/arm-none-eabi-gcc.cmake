# CMake 툴체인 — Arm GNU Toolchain (arm-none-eabi-gcc) / RTL8720DF KM4
#
# 아키텍처 플래그를 여기에 격리해 둔다.
# 무선 단계에서 lib_wlan.a / btgap.a (벤더 asdk GCC 6.4.1 로 빌드된 아카이브) 를
# 링크하다가 ABI·newlib 심볼 충돌이 나면 이 파일만 asdk-gcc.cmake 로 교체하면 되고,
# 소스와 프로젝트 구조는 손대지 않는다.
#
# 사용:
#   cmake -S . -B build                                   # PATH 에서 찾는다
#   cmake -S . -B build -DARM_TOOLCHAIN_DIR=/opt/.../bin/arm-none-eabi-gcc
#   ARM_TOOLCHAIN_DIR 환경변수로도 지정 가능

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  ARM)

# ── 검증된 최소 버전 ────────────────────────────────────────────────────────
# 13.3.1 과 14.2.1 로 fwlib 30개 컴파일 검증 완료 (docs/05-build-system.md).
#
# 13.3 을 하한으로 잡은 이유:
#   - cortex-m33 / armv8-m.main / fpv5-sp-d16 / -mcmse 는 GCC 7 부터 있으므로
#     이론상 더 낮은 버전도 될 수 있으나, 실제로 검증한 것은 13.3 부터다.
#   - 벤더 SDK 코드는 GCC 6.4.1 기준이라 최신 GCC 의 엄격해진 진단에 걸린다.
#     어느 진단이 error 인지가 버전마다 다르므로 하한을 고정해야 재현성이 생긴다.
set(NU87_GCC_MIN_VERSION "13.3")

# ── 컴파일러 탐색 ───────────────────────────────────────────────────────────
set(ARM_POSSIBLE_PATHS
    ENV ARM_TOOLCHAIN_DIR
    # Windows
    "C:/work/tools/baram-fw-tools/arm_toolchain/arm_gcc/test"
    "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1"
    "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.3 rel1"
    # macOS (Homebrew cask gcc-arm-embedded)
    "/opt/homebrew/bin"
    "/usr/local/bin"
    # Linux (developer.arm.com tarball 을 /opt 에 푼 경우)
    "/opt/arm-gnu-toolchain/bin"
)

find_program(ARM_TOOLCHAIN_DIR
    NAMES arm-none-eabi-gcc arm-none-eabi-gcc.exe
    HINTS ${ARM_POSSIBLE_PATHS}
    PATH_SUFFIXES bin
    DOC "arm-none-eabi-gcc 실행 파일 경로"
)

if(NOT ARM_TOOLCHAIN_DIR)
  message(FATAL_ERROR
    "arm-none-eabi-gcc 를 찾을 수 없습니다.\n"
    "  PATH 에 추가하거나 ARM_TOOLCHAIN_DIR 환경변수로 경로를 지정하세요.\n"
    "  설치 안내: firmware/docs/05-build-system.md")
endif()

# ── 버전 검사 ───────────────────────────────────────────────────────────────
# CMAKE_C_COMPILER_FORCED 를 켜면 CMake 가 컴파일러 검사를 건너뛰어
# CMAKE_C_COMPILER_VERSION 이 채워지지 않는다. 직접 물어본다.
execute_process(
  COMMAND "${ARM_TOOLCHAIN_DIR}" -dumpversion
  OUTPUT_VARIABLE NU87_GCC_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE NU87_GCC_VERSION_RESULT
)

if(NOT NU87_GCC_VERSION_RESULT EQUAL 0)
  message(FATAL_ERROR "컴파일러 버전을 확인할 수 없습니다: ${ARM_TOOLCHAIN_DIR}")
endif()

if(NU87_GCC_VERSION VERSION_LESS NU87_GCC_MIN_VERSION)
  message(FATAL_ERROR
    "arm-none-eabi-gcc ${NU87_GCC_VERSION} 은 너무 낮습니다. "
    "최소 ${NU87_GCC_MIN_VERSION} 이 필요합니다.\n"
    "  찾은 경로: ${ARM_TOOLCHAIN_DIR}\n"
    "  설치 안내: firmware/docs/05-build-system.md")
endif()

message(STATUS "arm-none-eabi-gcc ${NU87_GCC_VERSION} (>= ${NU87_GCC_MIN_VERSION}) : ${ARM_TOOLCHAIN_DIR}")

# ── 아키텍처 / 멀티립 ───────────────────────────────────────────────────────
# ROM 코드와 ABI 가 맞아야 한다. Realtek 은
#   -march=armv8-m.main+dsp + arm-none-eabi/lib/v8-m.main/fpu/fpv5-sp-d16
# 으로 링크한다. 표준 툴체인에서 대응하는 멀티립은 thumb/v8-m.main+fp/hard 다.
#
# 멀티립 판정에는 아래 4 개만 쓴다. -mcmse 는 멀티립을 바꾸지 않지만
# 판정 기준을 단순하게 유지하려고 분리한다.
set(NU87_ARCH_BASE -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard)

# 실제 컴파일/링크에 쓰는 플래그.
#
# -mcmse 가 필수다. TrustZone 을 쓰지 않아도 SDK 헤더가 CMSE 인트린식에 의존한다:
#   basic_types.h:520        __attribute__((cmse_nonsecure_call))
#   rtl8721d_trustzone.h:88  cmse_address_info.flags.secure
# 이 옵션이 없으면 arm_cmse.h 의 구조체에 secure 멤버가 없어 컴파일이 깨진다.
#   error: 'struct cmse_address_info' has no member named 'secure'
set(NU87_ARCH_FLAGS ${NU87_ARCH_BASE} -mcmse)

execute_process(
  COMMAND "${ARM_TOOLCHAIN_DIR}" -print-multi-directory ${NU87_ARCH_BASE}
  OUTPUT_VARIABLE NU87_MULTILIB
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if(NOT NU87_MULTILIB STREQUAL "thumb/v8-m.main+fp/hard")
  message(FATAL_ERROR
    "멀티립이 맞지 않습니다: '${NU87_MULTILIB}' (기대값 'thumb/v8-m.main+fp/hard')\n"
    "  이 툴체인은 Cortex-M33 하드 부동소수점 라이브러리를 갖고 있지 않습니다.\n"
    "  developer.arm.com 의 공식 Arm GNU Toolchain 을 사용하세요.\n"
    "  배포판 패키지(gcc-arm-none-eabi)는 멀티립이 빠진 경우가 있습니다.")
endif()

# ── 벤더 SDK 소스용 완화 플래그 ─────────────────────────────────────────────
# 벤더 코드는 GCC 6.4.1 기준으로 작성됐는데 GCC 14 는 아래 진단을
# 기본 error 로 승격했다. 경고로 되돌린다.
# (GCC 13 은 이 셋을 이미 warning 으로 처리하지만, 플래그를 줘도 무해하다)
set(NU87_VENDOR_RELAX_FLAGS
  -Wno-error=int-conversion
  -Wno-error=implicit-function-declaration
  -Wno-error=incompatible-pointer-types
)

# -Wreturn-mismatch 는 GCC 14 에서 신설된 이름이다.
# GCC 13 에 주면 하드 에러가 난다:
#   cc1: error: '-Wno-error=return-mismatch': no option '-Wreturn-mismatch'
# 그래서 버전으로 가른다.
if(NU87_GCC_VERSION VERSION_GREATER_EQUAL 14)
  list(APPEND NU87_VENDOR_RELAX_FLAGS -Wno-error=return-mismatch)
endif()

# ── make 탐색 (Windows/MinGW) ───────────────────────────────────────────────
if(WIN32)
  find_program(CMAKE_MAKE_PROGRAM
    NAMES make make.exe mingw32-make.exe
    HINTS ENV MAKE_DIR "c:/MinGW-32/bin" "c:/msys64/mingw64/bin"
    DOC "MinGW/MSYS make")
  if(NOT CMAKE_MAKE_PROGRAM)
    message(WARNING "make 를 찾을 수 없습니다. Ninja 생성기를 쓰거나 MAKE_DIR 을 지정하세요.")
  else()
    message(STATUS "make: ${CMAKE_MAKE_PROGRAM}")
  endif()
endif()

# ── 툴 경로 ─────────────────────────────────────────────────────────────────
get_filename_component(TOOLCHAIN_PATH "${ARM_TOOLCHAIN_DIR}" DIRECTORY)
set(TOOLCHAIN_PREFIX "${TOOLCHAIN_PATH}/arm-none-eabi-")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(WIN32)
  set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc.exe" CACHE FILEPATH "C compiler")
  set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++.exe" CACHE FILEPATH "C++ compiler")
else()
  set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc" CACHE FILEPATH "C compiler")
  set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++" CACHE FILEPATH "C++ compiler")
endif()
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})

set(CMAKE_OBJCOPY   ${TOOLCHAIN_PREFIX}objcopy CACHE INTERNAL "objcopy")
set(CMAKE_OBJDUMP   ${TOOLCHAIN_PREFIX}objdump CACHE INTERNAL "objdump")
set(CMAKE_SIZE_UTIL ${TOOLCHAIN_PREFIX}size    CACHE INTERNAL "size")
set(CMAKE_NM_UTIL   ${TOOLCHAIN_PREFIX}nm      CACHE INTERNAL "nm")

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

# 크로스 컴파일러라 링크 테스트가 불가능하므로 검사를 끈다.
set(CMAKE_C_COMPILER_FORCED   TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
