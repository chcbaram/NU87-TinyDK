/*
 * build_info.h — SDK 빌드가 만들어내는 헤더의 우리 버전
 *
 * 원본은 asdk 빌드 스크립트가 매 빌드마다 타임스탬프를 찍어 생성한다
 * (project/realtek_amebaD_va0_example/inc/inc_lp/build_info.h).
 * 우리는 asdk 를 쓰지 않으므로 컴파일러가 주는 값으로 대신한다.
 *
 * 쓰는 곳은 hci_board.c 의 디버그 출력 한 줄뿐이다.
 */
#ifndef BUILD_INFO_H_
#define BUILD_INFO_H_

#define UTS_VERSION           (__DATE__ " " __TIME__)
#define RTL_FW_COMPILE_TIME   __TIME__
#define RTL_FW_COMPILE_DATE   __DATE__
#define RTL_FW_COMPILE_BY     "nu87-fw"
#define RTL_FW_COMPILE_HOST   ""
#define RTL_FW_COMPILE_DOMAIN
#define RTL_FW_COMPILER       __VERSION__

#endif
