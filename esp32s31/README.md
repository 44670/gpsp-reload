# gpSP on ESP32-S31-Korvo-1

## Release build and firmware flash

The supported release configuration is selected by the exact CMake options
below, not by `CMAKE_BUILD_TYPE=Release`. It enables the native RV32IM
dynarec, disables profiling and gpSP-only LTO, boots into the TF-card menu,
uses a 12 MiB static ROM container, and allocates 1536 KiB/384 KiB ROM/RAM JIT
caches. `sdkconfig.defaults` supplies the remaining release settings: 320 MHz
CPU, 250 MHz PSRAM, PSRAM XIP for application `.text` and `.rodata`, and the
startup PSRAM memory test.

`ESP32S31_ROM_JIT_CACHE_KB=1536` is converted by the top-level CMake file to
`ROM_TRANSLATION_CACHE_SIZE=1572864`. This sizes gpSP's static, 4 KiB-aligned
PSRAM translation cache. It is unrelated to the `rom_cache_bytes=0` field in
the `gpsp_boot` line, which describes a separate ROM-data cache.

### Clean menu release build

Use only `esp32s31/build/`. Run `fullclean` when changing Kconfig defaults,
boot mode, memory layout, or any cached release option. Compiler temporary
files are also kept under `build/` because the system `/tmp` may be full.

```sh
source /home/john/esp-idf/export.sh
cd /home/john/work/gpsp/esp32s31
idf.py -B build fullclean
mkdir -p build/compiler-tmp
TMPDIR=/home/john/work/gpsp/esp32s31/build/compiler-tmp idf.py -B build \
  -DGPSP_ESP32S31_DYNAREC=1 \
  -DGPSP_ESP32S31_PROFILE=0 \
  -DGPSP_ESP32S31_PC_PROFILE=0 \
  -DGPSP_ESP32S31_LTO=0 \
  -DGPSP_ESP32S31_BOOT_MODE=menu \
  -DGPSP_ESP32S31_FLASH_SPILL=1 \
  -DGPSP_ESP32S31_MENU_AUTOROM_NAME= \
  -DESP32S31_GAMEPAK_STATIC_MB=12 \
  -DESP32S31_ROM_JIT_CACHE_KB=1536 \
  -DESP32S31_RAM_JIT_CACHE_KB=384 \
  build
```

The resulting application image is `build/gpsp_esp32s31.bin`.

### Incremental menu release build

Always repeat all ten release options so a sticky `build/CMakeCache.txt`
cannot silently retain a profiling or hardware-test configuration:

```sh
source /home/john/esp-idf/export.sh
cd /home/john/work/gpsp/esp32s31
mkdir -p build/compiler-tmp
TMPDIR=/home/john/work/gpsp/esp32s31/build/compiler-tmp idf.py -B build \
  -DGPSP_ESP32S31_DYNAREC=1 \
  -DGPSP_ESP32S31_PROFILE=0 \
  -DGPSP_ESP32S31_PC_PROFILE=0 \
  -DGPSP_ESP32S31_LTO=0 \
  -DGPSP_ESP32S31_BOOT_MODE=menu \
  -DGPSP_ESP32S31_FLASH_SPILL=1 \
  -DGPSP_ESP32S31_MENU_AUTOROM_NAME= \
  -DESP32S31_GAMEPAK_STATIC_MB=12 \
  -DESP32S31_ROM_JIT_CACHE_KB=1536 \
  -DESP32S31_RAM_JIT_CACHE_KB=384 \
  build
```

### Flash the release application

Use `app-flash` for a firmware-only update. It writes and verifies only the
application partition and preserves the `gamepak` partition at `0x190000`.
Do not interrupt it after erase has started.

```sh
source /home/john/esp-idf/export.sh
cd /home/john/work/gpsp/esp32s31
mkdir -p build/compiler-tmp
TMPDIR=/home/john/work/gpsp/esp32s31/build/compiler-tmp idf.py -B build \
  -p /dev/ttyUSB0 \
  -DGPSP_ESP32S31_DYNAREC=1 \
  -DGPSP_ESP32S31_PROFILE=0 \
  -DGPSP_ESP32S31_PC_PROFILE=0 \
  -DGPSP_ESP32S31_LTO=0 \
  -DGPSP_ESP32S31_BOOT_MODE=menu \
  -DGPSP_ESP32S31_FLASH_SPILL=1 \
  -DGPSP_ESP32S31_MENU_AUTOROM_NAME= \
  -DESP32S31_GAMEPAK_STATIC_MB=12 \
  -DESP32S31_ROM_JIT_CACHE_KB=1536 \
  -DESP32S31_RAM_JIT_CACHE_KB=384 \
  app-flash
```

Flash a raw GBA cartridge image independently of the firmware:

```sh
./flash_gba.sh -p /dev/ttyUSB0 path/to/game.gba
```

### Direct-from-flash debug build

Direct mode omits the menu and 12 MiB static ROM container, maps the existing
`gamepak` partition, and uses a 2048 KiB ROM JIT cache. Run `fullclean` before
switching boot modes, then use the same release options except for the two
values shown here:

```sh
source /home/john/esp-idf/export.sh
cd /home/john/work/gpsp/esp32s31
idf.py -B build fullclean
mkdir -p build/compiler-tmp
TMPDIR=/home/john/work/gpsp/esp32s31/build/compiler-tmp idf.py -B build \
  -DGPSP_ESP32S31_DYNAREC=1 \
  -DGPSP_ESP32S31_PROFILE=0 \
  -DGPSP_ESP32S31_PC_PROFILE=0 \
  -DGPSP_ESP32S31_LTO=0 \
  -DGPSP_ESP32S31_BOOT_MODE=flash \
  -DGPSP_ESP32S31_FLASH_SPILL=1 \
  -DGPSP_ESP32S31_MENU_AUTOROM_NAME= \
  -DESP32S31_GAMEPAK_STATIC_MB=12 \
  -DESP32S31_ROM_JIT_CACHE_KB=2048 \
  -DESP32S31_RAM_JIT_CACHE_KB=384 \
  build
```

This ESP-IDF app runs gpSP with the native RV32IM dynarec enabled by default
and sends its raw 240x160
RGB565 framebuffer to the Korvo-1 800x480 RGB panel. The direct driver scales
the image exactly 3x, keeps 40-pixel black side bars, and fuses measured FPS at
the top-left of the scaled GBA image. The default path renders into a 240x161
internal-SRAM target, copies its visible 240x160 area into one 76,800-byte
PSRAM snapshot, and has the LCD bounce callback scale ten source rows at a time
into two 48,000-byte internal-SRAM DMA strips. The emulator-owned GBA
framebuffer is never modified by the OSD. It uses no LVGL, board BSP, M5
library, or managed component.

There is deliberately only one PSRAM snapshot. While its CPU copy is active,
the bounce ISR reads the completed SRAM render target; after the copy, it reads
the stable PSRAM snapshot while gpSP renders the next frame. Thus the ISR never
samples a half-written snapshot. A displayed LCD frame can still change from
the old frame to the new frame at one strip boundary, which is the accepted
single-buffer tearing behavior. The former direct 800x480 PSRAM framebuffer
remains a comparison option with `-DESP32S31_LCD_MODE=framebuffer`; its scaler
experiments can select `cpu`, `sram_gdma`, `ppa`, or `auto` through
`-DESP32S31_SCALER=...`.

The firmware is compile-time locked to one HP core with
`CONFIG_FREERTOS_UNICORE=y`; a build fails if
`CONFIG_FREERTOS_NUMBER_OF_CORES` is not one. The active core runs at the
configured 320 MHz, and dynamic power management is disabled.

## TF-card ROM menu and direct boot

The default release boot mode is `menu`. It mounts the Korvo-1 TF slot at
`/sdcard` through 4-bit SDMMC (D0..D3 GPIO20..23, CLK GPIO24, CMD GPIO25, and
active-low power enable GPIO39), then draws the file browser directly into
gpSP's existing 240x160 RGB565 render buffer. There is no second menu
framebuffer. The menu build does not initialize or poll touch.

Menu input comes only from the USB XInput path. The D-pad moves by one item,
Left/Right moves by a page, physical Xbox A opens a directory or selects a
`.gba`, and physical Xbox B returns to the parent directory. Gameplay keeps
the existing Retropad/Nintendo geometry mapping independently.

After selection, the loader scans the exact ROM length in 32 KiB pages and
computes a 32-bit FNV-1a hash for each page. A hash match is only a candidate:
the full 32 KiB is read again and compared byte-for-byte before the pages share
one mapping. There is no special handling for `0xff`, zero, or another fill
byte. Each unique page is assigned to the static PSRAM container first. The
default container is 12 MiB and includes two scan/compare work pages, leaving
382 pages, or 11.94 MiB, for ROM data. Flash compare/write/verify uses a static
4 KiB internal staging buffer so the SPI1 driver never has to read PSRAM while
cache is disabled. Menu mode uses a 1.5 MiB ROM JIT cache and a 384 KiB RAM JIT
cache so the 12 MiB pool fits safely. The linked release menu image ends
external BSS at `0x50fc90a0`, leaving 225,120 contiguous bytes below the end of
the 16 MiB PSRAM aperture; hardware exposes 219 KiB of that region to the heap.

If unique pages exceed the PSRAM pool and
`GPSP_ESP32S31_FLASH_SPILL=1`, overflow pages use the `gamepak` partition. The
loader reads and compares each 32 KiB logical block first. Equal blocks are
left untouched; a different block is erased and rewritten as eight physical
4 KiB SPI-flash sectors, then read back and verified. A duplicate logical page
never consumes another flash slot. While processing flash, the progress screen
shows the cumulative 32 KiB `SKIP` and `WRITE` block counts. Set
`GPSP_ESP32S31_FLASH_SPILL=0` to prohibit all runtime flash writes; the menu
reports a capacity error instead.

The `gamepak` partition holds 462 unique-page slots. Together with the 382
PSRAM slots, a ROM may contain at most 844 distinct page contents (26.375 MiB);
duplicate logical pages do not count toward that limit. A denser image fails
with a capacity message before any page is loaded or erased.

Flash-overflow slots start at offset zero of the `gamepak` partition. The
partition remains untouched when a ROM fits PSRAM, but the first overflow
write repurposes it and invalidates any previously packed direct-boot image.
Run `flash_gba.sh` again before switching to direct boot after a menu load that
reported nonzero `flash_write_blocks`.

For debugging, `GPSP_ESP32S31_BOOT_MODE=flash` completely omits the menu,
SD-card loader, and static ROM pool, then boots the raw or packed ROM already
in the `gamepak` partition through `esp_partition_mmap()`.

In the default bounce mode,
`ESP32S31_LCD_BOUNCE_SOURCE_ROWS` controls its strip height and must divide 160
exactly. Ten rows is the tested balanced setting. Sixteen rows was also stable
and was about 0.035 ms/frame faster, but its two 76,800-byte buffers leave only
about 56 KiB of DMA-capable SRAM free.

## SRAM and PSRAM placement

The chip has 524,288 bytes (512 KiB) of physical HP SRAM at
`0x2f000000..0x2f080000`. ESP-IDF exposes `0x7afc0`, or 503,744 bytes (about
492 KiB), as the application's unified executable/data SRAM; the upper 20,544
bytes are reserved for ROM and bootloader use. There is another 32,768-byte LP
SRAM bank, of which this link exposes 32,744 bytes, but it is a separate
retention/low-power region and is not RGB-DMA capable.

This distinction matters in diagnostics: `MALLOC_CAP_INTERNAL` includes the LP
bank, while `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT` measures
the HP SRAM usable by DMA clients. The snapshot-bounce build links with 281,054
bytes of DIRAM occupied and 222,690 bytes available before runtime allocations.
Hardware status after full boot, USB gamepad initialization, cartridge mapping,
and the two LCD bounce allocations reported 125,143 bytes of internal memory
free, 93,135 bytes of DMA-capable HP SRAM free, and a 57,344-byte largest DMA
block. It also reported 4,324 bytes unused in the 6,144-byte main-task stack
during sustained emulation.

The memory placement is based on repeated hard-reset A/B runs of the same
Goodboy ADV workload. Approximate changes below are per emulated frame; a
negative time is faster. Bounce-strip rows in this table are retained as
historical measurements and are not allocated by the default framebuffer
path.

| Candidate placement | Memory cost | Measured effect | Default |
| --- | ---: | ---: | --- |
| ARM interpreter loop (`execute_arm`) | 63,172 B | -1.4 to -1.9 ms | SRAM |
| Shared 240x161 render buffer | 77,280 B | -0.50 to -0.55 ms | SRAM |
| Common RGB565 tile-renderer paths | 5,376 B | -0.10 to -0.13 ms | SRAM |
| LCD bounce strips, 2 to 8 source rows | +57,600 B | about -0.27 ms | SRAM |
| LCD bounce strips, 8 to 10 source rows | +19,200 B | about -0.018 ms | SRAM |
| LCD bounce strips, 10 to 16 source rows | +57,600 B | about -0.035 ms | rejected for headroom |
| One 240x160 stable snapshot | 76,800 B PSRAM | about +0.46 ms/frame copy | PSRAM |
| GBA IWRAM data | 32,768 B | -0.03 to -0.08 ms | SRAM |
| Sound ring, PSRAM to SRAM | 8,192 B final | -0.02 to -0.03 ms at 16 KiB; 8 KiB matched it | SRAM |
| One promoted ROM instruction page | 32,768 B | +0.16 to +0.30 ms regression | rejected |
| GBA VRAM | 98,304 B | about -0.10 ms | PSRAM |
| Interpreter read map | 32,768 B | no measurable gain | PSRAM |
| Writable BIOS copy | 16,384 B | no measurable gain | PSRAM |
| Paging/cheat/disabled-link state | 25,696 B | no measurable gain | PSRAM |
| Compact OBJ links | 20,480 B | about -0.003 ms | PSRAM |
| Profile-selected small helpers | 4,560 B | about +0.35 ms regression | PSRAM |

VRAM-in-SRAM with four-row bounce strips and VRAM-in-PSRAM with eight-row
strips had effectively the same frame time. The latter leaves about 59 KB more
free SRAM, so VRAM is not the best place for this SRAM. For the active
snapshot-bounce path, the useful placements are the interpreter loop, hot
RGB565 renderer, shared render buffer, 10-row LCD strips, 8 KiB sound ring, and
IWRAM. The compact stable snapshot and the rest remain in PSRAM.

The render buffer therefore remains a good SRAM use, but it is not treated as
mandatory by the build. Moving it to PSRAM loses about 0.5 ms/frame; spending
the released 77,280 bytes on every lower-ranked candidate that fits recovers
far less than that. `-DESP32S31_LCD_RENDER_MEMORY=psram` remains available for
future workloads to remeasure this tradeoff.

A separate 32 KiB SRAM ROM instruction page was also tested by swapping IWRAM
back to PSRAM, keeping total SRAM use nearly constant. The workload stayed on
one promoted page, but interpreter time still regressed by 0.16--0.30 ms per
frame. Keeping ROM pages in the 1 MiB PSRAM paging window is faster: its cache
and separate memory path avoid adding more contention to the SRAM used by the
CPU, renderer, and LCD refill ISR.

The immutable 16 KiB built-in BIOS source now stays in external read-only
storage and is copied once to the writable BIOS array. Fixed board options no
longer instantiate the 13.5 KiB generic libretro option table. Large paging,
cheat, RFU, and serial state moves 25,696 bytes to PSRAM, and the main-task
stack was reduced from 16 KiB to 6 KiB. These changes recover HP SRAM without
measurable Goodboy frame-time regressions.

The board's PSRAM is 16 MiB octal x8 at 250 MHz with fixed 18-cycle read
latency. In menu release mode, ROM storage is the 12 MiB static container;
there is no dynamic cartridge paging window. Other external BSS includes the
1.5 MiB ROM and 384 KiB RAM JIT caches, guest RAM and lookup tables, backup
storage, and the LCD snapshot. Direct-flash mode omits the 12 MiB container and
normally uses a 2 MiB ROM JIT cache instead.

ESP32-S31 maps its ordinary external-RAM aperture as RWX. The ROM and RAM JIT
caches are therefore static, 4 KiB-aligned PSRAM arrays and generated RV32IM
code executes directly from those addresses after data writeback, instruction
cache invalidation, and `fence.i`. The ESP32-S31 dynarec release configuration
requires `CONFIG_SPIRAM_XIP_FROM_PSRAM`; clean builds enable it through
`sdkconfig.defaults`. IDF copies and remaps flash-backed application `.text`
and `.rodata` into 250 MHz PSRAM at startup, while explicitly internal code
remains in SRAM. The mutable JIT caches independently execute through the
native RWX PSRAM aperture. Release startup also runs IDF's PSRAM memory test
and aborts before entering gpSP if that test fails.

A zero-PSRAM build is not feasible without a larger architectural change. GBA
EWRAM + IWRAM + VRAM already total 384 KiB; adding only the 77,280-byte render
target leaves roughly 42 KiB for the entire app, IDF, stacks, and LCD DMA.

The current frontend discards audio. Both boot modes use USB XInput only;
touch is neither linked into the firmware, initialized, nor polled.

The default display profile is the hardware-tested factory 26 MHz timing.
The older 18 MHz profile and ambiguous GPIO38 DISP route remain explicit build
experiments:

```sh
idf.py -B build -DKORVO1_LCD_COMPAT_18MHZ=1 build
idf.py -B build -DKORVO1_LCD_GPIO38_DISP=1 build
```

Run the host-side scaler, menu renderer, 32 KiB page planner, packed-ROM, FPS
overlay, and GT1151 report tests with:

```sh
make -C tests/esp32s31
```

For repeatable placement measurements, enable phase profiling, rebuild, flash,
and capture three bounded five-second windows after a hardware reset:

```sh
idf.py -B build -D GPSP_ESP32S31_PROFILE=1 build
./benchmark_serial.py --reset --windows 3 --timeout 25
```

Set `GPSP_ESP32S31_PC_PROFILE=1` only for interrupted-PC sampling; its 16 KiB
sample array is placed in LP/RTC SRAM so it does not consume the HP SRAM being
measured.

`GPSP_ESP32S31_LTO=1` applies LTO only to gpSP emulator sources. Board drivers,
the LCD refill path, USB, libretro-common support code, and ESP-IDF remain
ordinary objects. It is intentionally off by default: two non-LTO reset runs
reproduced within 0.03%, while one intervening LTO run increased `retro_run`
from 15.294 to 15.426 ms/frame (+0.87%) and `cpu_backend` from 14.379 to
14.507 ms/frame (+0.89%). LTO also added 1,404 image bytes and grew the
SRAM-resident `execute_arm` loop by 712 bytes.

The active one-snapshot display path was hardware-tested on 2026-07-18
with the factory timing, 16 MiB octal PSRAM at 250 MHz, and 16 MiB QIO flash at
80 MHz. Three reset windows reported 62.5--66.1 FPS. Copying 76,800 bytes from
SRAM to PSRAM took about 0.455 ms/frame including ISR preemption; scaling a full
LCD scan into bounce strips took about 2.15--2.20 ms. The run completed 15,334
bounce callbacks with zero position discontinuities, dropped frames, or LCD
timeouts. The ISR selected the SRAM source 419 times during 902 snapshot copies,
confirming that it avoids reading PSRAM during the partial-copy interval.
