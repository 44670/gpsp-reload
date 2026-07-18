# gpSP on ESP32-S31-Korvo-1

This ESP-IDF app runs the gpSP ARM interpreter and sends its raw 240x160
RGB565 framebuffer to the Korvo-1 800x480 RGB panel. The direct driver scales
the image exactly 3x, keeps 40-pixel black side bars, and draws measured FPS at
the top-left of the GBA image. It uses no LVGL, board BSP, M5 library, or
managed component.

The default LCD path uses no PSRAM for scanout and has no 800x480 framebuffer.
gpSP and the LCD refill ISR intentionally share one 240x161 internal-SRAM
render buffer; scanout may therefore tear while gpSP writes the next frame.
The RGB DMA driver rotates two 30-line internal-SRAM bounce buffers, and its
refill callback expands ten GBA rows at a time directly into the next LCD DMA
block. This uses 77,280 bytes for the shared render target and 96,000 bytes for
the two bounce buffers. The refill callback and scaler are IRAM-safe. The older
double-framebuffer path remains available for comparison with
`-DESP32S31_LCD_MODE=framebuffer` and can select `cpu`, `sram_gdma`, `ppa`, or
`auto` through `-DESP32S31_SCALER=...`.

`ESP32S31_LCD_BOUNCE_SOURCE_ROWS` controls the bounce strip height and must
divide 160 exactly. Ten rows is the tested balanced default. Sixteen rows was
also stable and was about 0.035 ms/frame faster than ten rows, but its two
76,800-byte buffers leave only about 56 KiB of DMA-capable SRAM free. Ten rows
leaves about 112 KiB free and a 68 KiB largest DMA block.

## SRAM and PSRAM placement

The chip has 524,288 bytes (512 KiB) of physical HP SRAM at
`0x2f000000..0x2f080000`. ESP-IDF exposes `0x7afc0`, or 503,744 bytes (about
492 KiB), as the application's unified executable/data SRAM; the upper 20,544
bytes are reserved for ROM and bootloader use. There is another 32,768-byte LP
SRAM bank, of which this link exposes 32,744 bytes, but it is a separate
retention/low-power region and is not RGB-DMA capable.

This distinction matters in diagnostics: `MALLOC_CAP_INTERNAL` includes the LP
bank, while `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT` measures
the HP SRAM that can actually back LCD bounce buffers. The production layout
occupies 283,824 bytes of static HP SRAM before heap allocations. Hardware
status after full boot and LCD allocation reported 114,319 bytes of DMA-capable
HP SRAM free, with a 69,632-byte largest block. It also reported 4,516 bytes
unused in the 6,144-byte main-task stack after cartridge loading
and sustained emulation.

The default placement is based on repeated hard-reset A/B runs of the same
Goodboy ADV workload. Approximate changes below are per emulated frame; a
negative time is faster.

| Candidate moved to SRAM | SRAM cost | Measured effect | Default |
| --- | ---: | ---: | --- |
| ARM interpreter loop (`execute_arm`) | 63,172 B | -1.4 to -1.9 ms | SRAM |
| Shared 240x161 render buffer | 77,280 B | -0.50 to -0.55 ms | SRAM |
| Common RGB565 tile-renderer paths | 5,376 B | -0.10 to -0.13 ms | SRAM |
| LCD bounce strips, 2 to 8 source rows | +57,600 B | about -0.27 ms | retained baseline |
| LCD bounce strips, 8 to 10 source rows | +19,200 B | about -0.018 ms | 10 rows in SRAM |
| LCD bounce strips, 10 to 16 source rows | +57,600 B | about -0.035 ms | rejected for headroom |
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
free SRAM, so VRAM is not the best place for this SRAM. Ordered by measured
marginal speed per byte, the useful placements are: interpreter loop, hot
RGB565 renderer, shared render buffer, bounce capacity through eight rows,
8 KiB sound ring, IWRAM, and the small eight-to-ten-row bounce increase. The
rest should remain in PSRAM.

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
latency. The final static external BSS is 586,792 bytes: 256 KiB EWRAM, 96 KiB
VRAM, a 32 KiB read map, a 16 KiB writable BIOS, about 25 KiB cold state,
20 KiB compact OBJ links, and 128 KiB backup storage. The interpreter build
omits the unused 256 KiB EWRAM and 32 KiB IWRAM dynarec SMC mirrors. A 1 MiB
cartridge paging window is allocated dynamically, while app text and read-only
data use PSRAM XIP.

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
