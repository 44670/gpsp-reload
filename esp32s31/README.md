# gpSP on ESP32-S31-Korvo-1

This ESP-IDF app runs the gpSP ARM interpreter and sends its raw 240x160
RGB565 framebuffer to the Korvo-1 800x480 RGB panel. The direct driver scales
the image exactly 3x, keeps 40-pixel black side bars, and draws measured FPS at
the top-left of the GBA image. It uses no LVGL, board BSP, M5 library, or
managed component.

The default LCD path uses no PSRAM for scanout and has no 800x480 framebuffer.
gpSP and the LCD refill ISR intentionally share one 240x161 internal-SRAM
render buffer; scanout may therefore tear while gpSP writes the next frame.
The RGB DMA driver rotates two 24-line internal-SRAM bounce buffers, and its
refill callback expands eight GBA rows at a time directly into the next LCD DMA
block. This uses 77,280 bytes for the shared render target and 76,800 bytes for
the two bounce buffers. The refill callback and scaler are IRAM-safe. The older
double-framebuffer path remains available for comparison with
`-DESP32S31_LCD_MODE=framebuffer` and can select `cpu`, `sram_gdma`, `ppa`, or
`auto` through `-DESP32S31_SCALER=...`.

`ESP32S31_LCD_BOUNCE_SOURCE_ROWS` controls the bounce strip height and must
divide 160 exactly. Eight rows is the tested default. Ten rows needs two 48,000
byte contiguous DMA allocations and failed on the selected SRAM layout even
though the aggregate free heap was large enough; the largest free blocks are
what matter here.

## SRAM and PSRAM placement

The advertised 500 KB main SRAM is not 500 KiB of application heap. The ESP-IDF
linker exposes 503,744 bytes (about 492 KiB) of shared instruction/data RAM for
this configuration. The chip also has 32,768 bytes of LP SRAM, but that is a
separate retention/low-power region and is not the normal RGB-DMA or main-heap
pool. In the production build, static DIRAM use is 348,458 bytes before the LCD
bounce buffers are allocated. Hardware status after LCD startup reported
91,383 bytes free, with a 31,744-byte largest block.

The default placement is based on repeated hard-reset A/B runs of the same
Goodboy ADV workload. Approximate changes below are per emulated frame; a
negative time is faster.

| Candidate moved to SRAM | SRAM cost | Measured effect | Default |
| --- | ---: | ---: | --- |
| ARM interpreter loop (`execute_arm`) | 63,172 B | -1.4 to -1.9 ms | SRAM |
| Shared 240x161 render buffer | 77,280 B | -0.50 to -0.55 ms | SRAM |
| Common RGB565 tile-renderer paths | 5,376 B | -0.10 to -0.13 ms | SRAM |
| LCD bounce strips, 2 to 8 source rows | +57,600 B | about -0.27 ms | 8 rows in SRAM |
| GBA IWRAM data | 32,768 B | -0.03 to -0.08 ms | SRAM |
| GBA VRAM | 98,304 B | about -0.10 ms | PSRAM |
| Interpreter read map | 32,768 B | no measurable gain | PSRAM |
| 16 KiB sound ring | 16,384 B | -0.02 to -0.03 ms | PSRAM |
| Compact OBJ links | 20,480 B | about -0.003 ms | PSRAM |
| Profile-selected small helpers | 4,560 B | about +0.35 ms regression | PSRAM |

VRAM-in-SRAM with four-row bounce strips and VRAM-in-PSRAM with eight-row
strips had effectively the same frame time. The latter leaves about 59 KB more
free SRAM, so VRAM is not the best place for this SRAM. The current priority is:
interpreter loop, render buffer, hot RGB565 renderer, DMA bounce capacity,
IWRAM, then everything else.

The board's PSRAM is 16 MiB octal x8 at 250 MHz with fixed 18-cycle read
latency. The final static external BSS is 561,152 bytes: 256 KiB EWRAM, 96 KiB
VRAM, a 32 KiB read map, a 16 KiB sound ring, 20 KiB compact OBJ links, and
128 KiB backup storage. The interpreter build omits the unused 256 KiB EWRAM
and 32 KiB IWRAM dynarec SMC mirrors. A 1 MiB cartridge paging window is
allocated dynamically, while app text and read-only data use PSRAM XIP.

A zero-PSRAM build is not feasible without a larger architectural change. GBA
EWRAM + IWRAM + VRAM already total 384 KiB; adding only the 77,280-byte render
target leaves roughly 42 KiB for the entire app, IDF, stacks, and LCD DMA.

The ROM lives in the `gamepak` SPI-flash partition as raw `.gba` bytes. There
is no wrapper, metadata header, manifest, or sidecar partition. Since the
firmware and ROM share the board's 16 MiB flash, the partition accepts at most
`0xe70000` bytes (14.44 MiB). At boot the firmware infers the used ROM length
from the erased `0xff` tail, rounds it to gpSP's 32 KiB paging unit, and keeps
the cartridge mapped with `esp_partition_mmap()`.

Build the interpreter firmware:

```sh
source /home/john/esp-idf/export.sh
cd /home/john/work/gpsp/esp32s31
idf.py -B build build
```

For a routine firmware-only update, fully write and verify the app partition;
this does not touch the `gamepak` partition at `0x190000`:

```sh
/home/john/.espressif/python_env/idf6.2_py3.12_env/bin/python -m esptool \
  --chip esp32s31 -p /dev/ttyUSB0 -b 460800 \
  --before default-reset --after hard-reset --no-stub \
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x10000 build/gpsp_esp32s31.bin
```

Flash a raw GBA cartridge image independently of the firmware:

```sh
./flash_gba.sh -p /dev/ttyUSB0 path/to/game.gba
```

The current frontend discards audio and reports touch samples over UART; touch
is not yet mapped to GBA controls. LCD and touch initialization remain
independent, so a missing peripheral does not prevent UART diagnostics.

The default display profile is the hardware-tested factory 26 MHz timing.
The older 18 MHz profile and ambiguous GPIO38 DISP route remain explicit build
experiments:

```sh
idf.py -B build -DKORVO1_LCD_COMPAT_18MHZ=1 build
idf.py -B build -DKORVO1_LCD_GPIO38_DISP=1 build
```

Run the host-side scaler, FPS overlay, and GT1151 report tests with:

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

The raw display/touch and no-framebuffer gpSP paths were hardware-tested on
2026-07-18 with the factory timing, 16 MiB octal PSRAM at 250 MHz, and 16 MiB
QIO flash at 80 MHz. The bounce path completed hundreds of thousands of
measured DMA refills without a position discontinuity, dropped frame, or LCD
timeout.
