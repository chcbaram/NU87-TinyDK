# 참조 구현

`upload_image_tool.cpp` (1103줄)

출처: https://github.com/Ameba-AIoT/ameba-arduino-d
      `Ameba_misc/Autoflash_patch/src/upload_image_tool.cpp` (dev 브랜치)

Realtek 공식 저장소에 들어 있는 UART 플래싱 도구의 소스다.
`jojoling/ameba_bw16_autoflash` 의 코드가 그대로 편입된 것이다.

`flash.py` 가 이 파일의 프로토콜을 이식한 것이므로 대조용으로 둔다.
빌드에는 쓰지 않는다.

핵심 함수:
  program_spi_flash   전체 흐름
  write_block         1032 바이트 프레임 전송
  erase_block         0x17 소거
  send_cmd            0x15 대기 → 명령 → 0x06 대기
  set_max_speed       0x05 / 0x07 보레이트 협상
  load_file           파일 적재 + 체크섬 (헤더를 벗기지 않는다)
