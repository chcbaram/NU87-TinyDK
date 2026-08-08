/*
 * nu87_km4_img2.ld — KM4 애플리케이션 이미지 배치 (RTL8720DF)
 *
 * 코드·상수·데이터를 전부 KM4 SRAM(BD_RAM_NS, 476KB)에 배치한다.
 * 플래시 XIP(0x0E000000)를 쓰지 않으므로:
 *   - 이미지가 1 파트라 32바이트 헤더 하나만 붙이면 된다
 *   - 플래시 쓰기 중에도 코드 페치가 막히지 않는다
 *     (XIP 이면 쓰기 루틴과 그 경로의 인터럽트 핸들러를 SRAM 에 따로 둬야 한다)
 *   - 소프트웨어 브레이크포인트를 쓸 수 있다 (하드웨어 BP 는 코어당 2 개뿐)
 *   - GDB load 로 바로 올릴 수 있다
 *
 * WiFi + lwIP + mbedTLS 가 들어오면 SRAM 에 담기지 않으므로
 * .xip_image2.text 를 되살려 xip / ram / psram 3 파트 구성으로 바꿔야 한다.
 *
 * 원본: firm-sdk/lib/Realtek/ld/rlx8721d_img2_is.ld + rlx8721d_layout_is.ld
 */

/* 진입은 Img2EntryFun0 구조체를 통해 이뤄진다. KM4 부트로더가
 * .image2.entry.data 의 함수 포인터를 읽어 점프한다.
 * ENTRY() 는 --gc-sections 의 루트를 알려주는 의미만 갖는다. */
ENTRY(Reset_Handler)

/* ──────────────────────────────────────────────────────────────────────────
 *  메모리 리전 (원본 rlx8721d_layout_is.ld 에서 KM4 에 필요한 것만)
 * ────────────────────────────────────────────────────────────────────────── */
MEMORY
{
  /* ROM — 심볼 해석용. 실제 배치는 하지 않는다 */
  IROM_NS (rx)          : ORIGIN = 0x1010A000, LENGTH = 0x10140000 - 0x1010A000  /* 216K */
  DROM_NS (rx)          : ORIGIN = 0x101C0000, LENGTH = 0x101D4000 - 0x101C0000  /*  80K */
  ROMBSS_RAM_COM (rw)   : ORIGIN = 0x10000000, LENGTH = 0x10001000 - 0x10000000  /*   4K */
  ROMBSS_RAM_NS (rw)    : ORIGIN = 0x10001000, LENGTH = 0x10002000 - 0x10001000  /*   4K */

  /* KM4 애플리케이션 SRAM — 우리가 쓰는 유일한 리전 */
  BD_RAM_NS (rwx)       : ORIGIN = 0x10005000, LENGTH = 0x1007C000 - 0x10005000  /* 456K */
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

  /* ── 이미지 진입점 ──────────────────────────────────────────────────────
   * __ram_image2_text_start__ 가 이미지 헤더의 "로드 주소"가 된다.
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
     * 이미지 시작에서 정확히 +32 바이트에 고정 배치한다.
     * OTA 슬롯 검증, 다운그레이드 방지, 업데이트 전 버전 비교에 쓴다.
     *
     * '. =' 로 오프셋을 명시하므로 앞쪽 구조체 크기가 바뀌어도 위치가 유지된다.
     *
     * 위치:
     *   플래시   OTA 슬롯 시작 + km0_image2 크기 + 32(이미지 헤더) + 32
     *   실행 중  __ram_image2_text_start__ + 32
     *
     * OTA1/OTA2 는 Flash MMU 가 같은 가상주소로 매핑하므로 절대번지가 아니라
     * 이미지 상대 오프셋으로 두어야 한다. */
    . = __ram_image2_text_start__ + 32;
    __image2_version__ = .;
    KEEP(*(.version))
    . = ALIGN(4);
  } > BD_RAM_NS

  /* ── 코드 ──────────────────────────────────────────────────────────────
   * 원본은 *(.text*) 를 KM4_IMG2(XIP) 로 보내지만 여기서는 SRAM 에 둔다.
   */
  .ram_image2.text :
  {
    __ram_text_start__ = .;
    __flash_text_start__ = .;        /* 원본 심볼 호환용 (XIP 미사용) */

    *(.image2.ram.text*)
    *(.image2.net.ram.text*)
    *(.text*)

    /* crti.o / crtn.o 가 _init / _fini 를 여기에 넣는다.
     * __libc_init_array() 가 _init 을 호출하므로 반드시 배치해야 한다.
     * 빠뜨리면 두 파일의 조각이 orphan 섹션으로 흩어져 _init 이 깨진 코드가 되고,
     * __libc_init_array() 에서 UNDEFINSTR HardFault 가 난다. */
    . = ALIGN(4);
    KEEP(*(.init))
    KEEP(*(.fini))

    *(.rodata*)
    *(.image2.ram.rodata*)
    *(.image2.net.ram.rodata*)

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
    __ram_text_end__ = .;
    . = ALIGN(16);
  } > BD_RAM_NS

  /* C++ 예외 테이블 */
  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > BD_RAM_NS

  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > BD_RAM_NS

  /* ── 초기화된 데이터 ───────────────────────────────────────────────────
   * __ram_image2_text_end__ 까지가 부트로더가 로드하는 범위다.
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
     * moduleInit() 이 _smodule / _emodule 로 개수를 계산해 순회한다:
     *
     *   info.count    = ((int)&_emodule - (int)&_smodule) / sizeof(module_t);
     *   info.p_module = (module_t *)&_smodule;
     *
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
   * SRAM_NOCACHE_DATA_SECTION 으로 표시된 변수가 들어온다 (DMA 버퍼 등).
   * 비어 있어도 심볼은 있어야 한다.
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
 * KM4 부트로더가 설정해 둔다. BD_RAM_NS 와 겹치지 않는다.
 *
 * newlib 의 _sbrk 가 힙 상한을 (_estack - _Min_Stack_Size) 로 계산한다.
 * 스택이 별도 리전에 있으므로 예약분 없이 SRAM 끝까지 힙으로 쓸 수 있다. */
_estack          = __sram_end__;
_Min_Stack_Size  = 0;
