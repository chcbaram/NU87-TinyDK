/*
 * nu87_km4_img2_xip.ld — KM4 애플리케이션 이미지 배치 · XIP (RTL8720DF)
 *
 * 코드와 상수를 플래시(KM4_IMG2, 가상 0x0E000020)에 두고 실행하며,
 * 진입점 / 데이터 / BSS / 힙만 SRAM(BD_RAM_NS)에 둔다.
 *
 * 전량 SRAM 배치인 nu87_km4_img2.ld 와 짝이다. 둘 중 하나를 고른다:
 *   cmake -S . -B build -DNU87_LAYOUT=sram    (기본, 반복 개발용)
 *   cmake -S . -B build -DNU87_LAYOUT=xip     (무선/OTA 단계)
 *
 * SRAM 배치를 남겨 둔 이유
 *   - GDB load 로 바로 올릴 수 있어 플래시를 건드리지 않고 반복할 수 있다
 *   - 소프트웨어 브레이크포인트를 쓸 수 있다 (하드웨어 BP 는 코어당 2 개뿐)
 *   - 플래시 쓰기 중에도 코드 페치가 막히지 않는다
 *
 * XIP 배치가 필요한 이유
 *   - lib_wlan.a / btgap.a 가 SRAM 476KB 에 들어가지 않는다
 *   - OTA 슬롯(OTA1/OTA2)이 XIP 가상주소 매핑을 전제로 한다
 *
 * ★ XIP 에서 지켜야 할 것
 *   플래시에 쓰는 코드와 그 경로에서 도는 인터럽트 핸들러는 SRAM 에 있어야 한다.
 *   쓰기 중에는 플래시 페치가 막히기 때문이다.
 *   SDK 의 IMAGE2_RAM_TEXT_SECTION 이 그 용도이고, 아래에서 .image2.ram.text* 를
 *   SRAM 쪽에 배치한다.
 *
 * 원본: firm-sdk/lib/Realtek/ld/rlx8721d_img2_is.ld + rlx8721d_layout_is.ld
 */

/* 진입은 Img2EntryFun0 구조체를 통해 이뤄진다. KM4 부트로더가
 * .image2.entry.data 의 함수 포인터를 읽어 점프한다.
 * ENTRY() 는 --gc-sections 의 루트를 알려주는 의미만 갖는다. */
ENTRY(Reset_Handler)

/* ──────────────────────────────────────────────────────────────────────────
 *  메모리 리전
 * ────────────────────────────────────────────────────────────────────────── */
MEMORY
{
  /* ROM — 심볼 해석용. 실제 배치는 하지 않는다 */
  IROM_NS (rx)          : ORIGIN = 0x1010A000, LENGTH = 0x10140000 - 0x1010A000  /* 216K */
  DROM_NS (rx)          : ORIGIN = 0x101C0000, LENGTH = 0x101D4000 - 0x101C0000  /*  80K */
  ROMBSS_RAM_COM (rw)   : ORIGIN = 0x10000000, LENGTH = 0x10001000 - 0x10000000  /*   4K */
  ROMBSS_RAM_NS (rw)    : ORIGIN = 0x10001000, LENGTH = 0x10002000 - 0x10001000  /*   4K */

  /* KM4 애플리케이션 SRAM — 진입점 / 데이터 / BSS / 힙 */
  BD_RAM_NS (rwx)       : ORIGIN = 0x10005000, LENGTH = 0x1007C000 - 0x10005000  /* 456K */

  /* KM4 IMG2 XIP — 코드와 상수
   *
   * 0x0E000000 은 플래시 MMU 가 만드는 가상 주소이고 고정값이다.
   * KM0 부트로더의 __hs_flash_text_start__ 에서 오는 컴파일 타임 상수라
   * 우리 이미지 크기나 KM0 블롭 크기와 무관하다.
   *
   * 물리 주소는 KM0 부트로더가 런타임에 계산한다 (boot_flash_lp.c):
   *
   *   BOOT_FLASH_OTA_MMU(0, 0x0C000000, OTA_Region, &LsSize);   // KM0
   *   BOOT_FLASH_OTA_MMU(1, 0x0E000000, OTA_Region + LsSize, NULL);  // KM4
   *
   * LsSize 는 KM0 이미지의 자기 헤더에서 계산해 4KB 로 올림한 값이다.
   * 즉 KM0 블롭이 바뀌어도 부트로더가 알아서 따라온다.
   *
   * 우리 보드 실측:
   *   KM0 파트  96768 + 10128 + 64 = 106960 -> 4KB 정렬 110592 (0x1B000)
   *   KM4 물리  0x08006000 + 0x1B000 = 0x08021000
   *   가상 0x0E000020 == 물리 0x08021020 (바이트 일치 확인)
   *
   * ★ 지켜야 하는 것은 가상주소가 아니라 km0_image2_all.bin 의 길이다.
   *   파일 길이가 위 계산값과 다르면 KM4 파트가 부트로더가 찾는 자리에 놓이지
   *   않아 "AmebaD Flash Memory Layout is modified!" 를 출력하며 멈춘다.
   *   make_image.py 가 이 조건을 검사한다.
   *
   * LENGTH 는 넘침 검출용 상한이다. 실제 한계는 OTA 슬롯 크기가 정한다. */
  KM4_IMG2 (rx)         : ORIGIN = 0x0E000000 + 0x20, LENGTH = 0x00200000 - 0x20
}

/* ──────────────────────────────────────────────────────────────────────────
 *  섹션
 * ────────────────────────────────────────────────────────────────────────── */
SECTIONS
{
  /* ROM 코드/데이터가 이 주소에 있다는 것만 알려준다 (배치 없음) */
  .rom.text        : { } > IROM_NS
  .rom.rodata      : { } > DROM_NS
  .hal.rom.bss     : { } > ROMBSS_RAM_COM
  .hal.ns_rom.bss  : { } > ROMBSS_RAM_NS

  /* ── 이미지 진입점 (SRAM 파트 시작) ────────────────────────────────────
   * __ram_image2_text_start__ 가 RAM 파트 헤더의 "로드 주소"가 된다.
   * make_image.py 가 .map 에서 이 심볼을 읽어 32바이트 헤더에 채운다.
   *
   * .image2.entry.data      = Img2EntryFun0 { app_start, NULL, NewVectorTable }
   * .image2.validate.rodata = RAM_IMG2_VALID_PATTEN[20] = "RTKWin\0\xff..."
   *                           부트로더가 이걸 검증한다. 빠지면 부팅하지 않는다.
   */
  .ram_image2.entry :
  {
    __ram_image2_text_start__ = .;

    /* +0   12B : Img2EntryFun0 { RamStartFun, RamWakeupFun, VectorNS } */
    __image2_entry_func__ = .;
    KEEP(*(SORT(.image2.entry.data*)))

    /* +12  20B : RAM_IMG2_VALID_PATTEN[20] = "RTKWin\0\xff..." */
    __image2_validate_code__ = .;
    KEEP(*(.image2.validate.rodata*))

    /* +32  72B : firm_ver_t — 펌웨어 식별 정보
     *
     * 부트로더나 호스트 툴이 펌웨어를 실행하지 않고 버전을 읽을 수 있도록
     * RAM 파트 시작에서 정확히 +32 바이트에 고정 배치한다.
     * SRAM 배치와 XIP 배치가 같은 규칙을 쓰므로 툴을 나눌 필요가 없다. */
    . = __ram_image2_text_start__ + 32;
    __image2_version__ = .;
    KEEP(*(.version))
    . = ALIGN(4);
  } > BD_RAM_NS

  /* ── SRAM 에 남겨야 하는 코드 ──────────────────────────────────────────
   * 플래시 쓰기 루틴처럼 XIP 페치가 막힌 동안 돌아야 하는 코드다.
   * SDK 는 IMAGE2_RAM_TEXT_SECTION 으로 표시한다.
   */
  .ram_image2.text :
  {
    __ram_text_start__ = .;

    *(.image2.ram.text*)
    *(.image2.net.ram.text*)
    *(.image2.ram.rodata*)
    *(.image2.net.ram.rodata*)

    __ram_text_end__ = .;
    . = ALIGN(16);
  } > BD_RAM_NS

  /* ── 초기화된 데이터 ───────────────────────────────────────────────────
   * __ram_image2_text_end__ 까지가 부트로더가 SRAM 으로 복사하는 범위다.
   */
  .ram_image2.data :
  {
    __data_start__ = .;
    *(.data*)
    *(.image2.ram.data*)
    *(.image2.net.ram.data*)

    /* ★ .module 섹션 — ap 계층의 모듈 레지스트리
     *
     * MODULE_DEF(x) 가 volatile module_t 를 이 섹션에 넣고,
     * moduleInit() 이 _smodule / _emodule 로 개수를 계산해 순회한다.
     * 이 섹션이 빠지면 모듈이 하나도 등록되지 않는다. */
    . = ALIGN(8);
    _smodule = .;
    KEEP(*(.module))
    KEEP(*(.module*))
    _emodule = .;

    __data_end__ = .;
    __ram_image2_text_end__ = .;
    . = ALIGN(16);
  } > BD_RAM_NS

  /* ── BSS — 로드되지 않는다. app_start() 가 0 으로 채운다 ─────────────── */
  .ram_image2.bss :
  {
    __bss_start__ = .;
    *(.bss*)
    *(.image2.ram.bss*)
    *(.image2.net.ram.bss*)
    *(COMMON)
    __bss_end__ = .;
  } > BD_RAM_NS

  /* ── 논캐시 데이터 ─────────────────────────────────────────────────────
   * app_mpu_nocache_init() 이 이 범위를 MPU 논캐시 리전으로 설정한다.
   */
  .ram_image2.nocache.data :
  {
    . = ALIGN(32);
    __ram_nocache_start__ = .;
    *(.bdsram.data*)
    *(.image2.ram.bdsram.data*)
    *(.image2.net.ram.bdsram.data*)
    . = ALIGN(32);
    __ram_nocache_end__ = .;
  } > BD_RAM_NS

  /* ── 힙 ────────────────────────────────────────────────────────────────
   * newlib 의 _sbrk 가 'end' 심볼부터 힙으로 쓴다.
   */
  .ram_heap.data :
  {
    . = ALIGN(32);
    *(.bfsram.data*)
    *(.image2.ram.bfsram.data*)
    end = .;
    _end = .;
    __end__ = .;
    *(.heap.stdlib*)
    . = . + 4096;              /* 최소 힙 4KB 확보 */
    __bfsram_end__ = .;
    _heap_end = .;
  } > BD_RAM_NS

  /* ── 코드와 상수 (XIP 파트) ────────────────────────────────────────────
   * __flash_text_start__ 가 XIP 파트 헤더의 "로드 주소"가 된다.
   * KM4 부트로더는 이 파트를 복사하지 않고 플래시에 둔 채 실행시킨다.
   */
  .xip_image2.text :
  {
    __flash_text_start__ = .;

    *(.img2_custom_signature*)

    *(.text*)

    /* crti.o / crtn.o 가 _init / _fini 를 여기에 넣는다.
     * __libc_init_array() 가 _init 을 호출하므로 반드시 배치해야 한다.
     * 빠뜨리면 두 파일의 조각이 orphan 섹션으로 흩어져 _init 이 깨진 코드가 되고,
     * __libc_init_array() 에서 UNDEFINSTR HardFault 가 난다. */
    . = ALIGN(4);
    KEEP(*(.init))
    KEEP(*(.fini))

    *(.rodata*)

    /* C++ / __libc_init_array 지원 — app_start() 가 호출한다 */
    . = ALIGN(4);
    __preinit_array_start = .;
    KEEP(*(.preinit_array))
    __preinit_array_end = .;
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = .;
    . = ALIGN(4);
    __fini_array_start = .;
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array))
    __fini_array_end = .;

    /* SDK shell 커맨드 테이블 (미사용이지만 심볼은 유지) */
    . = ALIGN(4);
    __cmd_table_start__ = .;
    KEEP(*(.cmd.table.data*))
    __cmd_table_end__ = .;

    __flash_text_end__ = .;
    . = ALIGN(16);
  } > KM4_IMG2

  /* C++ 예외 테이블 */
  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > KM4_IMG2

  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > KM4_IMG2

  /* PSRAM 과 BT trace 는 쓰지 않는다 */
  /DISCARD/ :
  {
    *(.psram.text*)
    *(.psram.data*)
    *(.psram.rodata*)
    *(.psram.bss*)
    *(.psram.heap*)
    *(.BTTRACE)
  }
}

/* SRAM 상단 (0x1007C000) */
__sram_end__ = ORIGIN(BD_RAM_NS) + LENGTH(BD_RAM_NS);

/* 메인 스택(MSP)은 MSP_RAM_NS(0x10004000~0x10005000, 4K)에 있고
 * KM4 부트로더가 설정해 둔다. BD_RAM_NS 와 겹치지 않는다. */
_estack          = __sram_end__;
_Min_Stack_Size  = 0;
