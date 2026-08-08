# 공장 출하 플래시 백업

`nu87_factory_flash_4MB.bin` — NU87-TinyDK 보드의 **최초 상태 플래시 전체 덤프** (4MB).

```
sha256  6ce97922a6ab02324b7940d6c4a30ec22c4cd6d403d7245858d7eccd4145cc1d
채취    2026-08-08, SWD (OpenOCD 0.12 + ST-LINK/V2-1)
범위    0x08000000 .. 0x08400000
```

## 왜 보관하는가

1. **복구용.** 플래시를 잘못 쓰면 이걸로 되돌린다. RTL8720DF 는 부트로더/OTA 레이아웃이
   ROM 에 고정되어 있어 `0x08000000` 근처를 잘못 건드리면 부팅이 막힌다.
2. **부팅 블롭의 출처.** `prebuilt/` 의 3개 파일이 이 덤프에서 추출된 것이다.
   `tools/extract_blobs.py` 로 언제든 재생성할 수 있다.
3. **이미지 포맷의 실측 근거.** `docs/06-boot-image.md` 의 레이아웃 표가 여기서 나왔다.

## 재생성

```bash
# 백업 (덮어쓰기 주의)
openocd -f tools/openocd/nu87.cfg -c "init" \
        -c "nu87_backup backup/nu87_factory_flash_4MB.bin" -c "shutdown"

# 블롭 추출
python3 tools/extract_blobs.py backup/nu87_factory_flash_4MB.bin -o prebuilt/
```

## 주의 — 이것은 특정 보드의 덤프다

`0x08002000`(BACKUP) 과 `0x08003000`(System Data) 는 현재 전부 `0xFF` 라서
이 덤프에 보드 고유 데이터가 들어 있지 않다. 그래서 다른 NU87 보드에 써도 대체로 문제없다.

다만 다음은 보장하지 않는다:
- eFuse 는 플래시가 아니므로 이 덤프에 포함되지 않는다 (MAC 주소, RF 캘리브레이션, SWD 핀 선택 등)
- 다른 보드의 출하 펌웨어 버전이 다를 수 있다

**다른 보드에 작업할 때는 그 보드의 백업을 따로 떠 둘 것.**

## 절대 하지 말 것

- `chip erase` — 되돌릴 수 없다
- `0x08000000` 오프셋을 근거 없이 덮어쓰기 — 벤더 프리부트로더/보안부팅 키가 있을 수 있다
