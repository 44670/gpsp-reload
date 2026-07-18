# gpSP on ESP32-S31-Korvo-1

This ESP-IDF app runs the gpSP ARM interpreter and sends its raw 240x160
RGB565 framebuffer to the Korvo-1 800x480 RGB panel. The direct driver scales
the image exactly 3x, keeps 40-pixel black side bars, and draws measured FPS at
the top-left of the GBA image. It uses no LVGL, board BSP, M5 library, or
managed component.

The default LCD path has no 800x480 framebuffer. gpSP rotates three native
240x161 PSRAM render buffers, while the RGB DMA driver rotates two 24-line
internal-SRAM bounce buffers. Its refill callback expands eight GBA rows at a
time directly into the next LCD DMA block. The older double-framebuffer path
remains available for comparison with
`-DESP32S31_LCD_MODE=framebuffer` and can select `cpu`, `sram_gdma`, `ppa`, or
`auto` through `-DESP32S31_SCALER=...`.

The ROM lives in the `gamepak` SPI-flash partition as raw `.gba` bytes. There
is no wrapper, metadata header, manifest, or sidecar partition. Since the
firmware and ROM share the board's 16 MiB flash, the partition accepts at most
`0xe70000` bytes (14.44 MiB). At boot the firmware infers the used ROM length
from the erased `0xff` tail, rounds it to gpSP's 32 KiB paging unit, and keeps
the cartridge mapped with `esp_partition_mmap()`.

Build and flash the interpreter firmware:

```sh
source /home/john/esp-idf/export.sh
cd /home/john/work/gpsp/esp32s31
idf.py --preview -B build build
idf.py --preview -B build -p /dev/ttyUSB0 flash
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
idf.py --preview -B build -DKORVO1_LCD_COMPAT_18MHZ=1 build
idf.py --preview -B build -DKORVO1_LCD_GPIO38_DISP=1 build
```

Run the host-side scaler, FPS overlay, and GT1151 report tests with:

```sh
make -C tests/esp32s31
```

The raw display/touch and no-framebuffer gpSP paths were hardware-tested on
2026-07-18 with the factory timing, 16 MiB octal PSRAM at 250 MHz, and 16 MiB
QIO flash at 80 MHz. The bounce path completed more than 92,000 measured DMA
refills without a position discontinuity, dropped frame, or LCD timeout.
