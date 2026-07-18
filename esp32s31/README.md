# ESP32-S31-Korvo-1 board-driver smoke firmware

This is the first ESP32-S31 port slice. It owns the Korvo-1 RGB panel and
GT1151 touch controller directly and depends only on ESP-IDF. It deliberately
does not use the Korvo BSP, LVGL, or managed components.

The firmware displays a moving 3x-scaled 240x160 RGB565 test image and prints
touch coordinates plus periodic machine-readable driver statistics. LCD and
touch startup are independent: either driver may soft-fail while UART remains
available.

Build and flash with the ESP-IDF preview target:

```sh
source /home/john/esp-idf/export.sh
cd /home/john/work/gpsp/esp32s31
idf.py --preview -B build build
idf.py --preview -B build -p /dev/ttyUSB0 flash monitor
```

The default display profile is the factory-demo 26 MHz timing. The older BSP
profile and the ambiguous GPIO38 DISP routing are explicit experiments:

```sh
idf.py --preview -B build -DKORVO1_LCD_COMPAT_18MHZ=1 build
idf.py --preview -B build -DKORVO1_LCD_GPIO38_DISP=1 build
```

Run the host-side scaler and GT1151 report tests with:

```sh
make -C tests/esp32s31
```

## Initial hardware smoke result

On 2026-07-18 the default profile was flashed to an ESP32-S31 revision v0.0
Korvo-1 with 16 MiB flash and 16 MiB octal PSRAM. One cold boot produced the
centered color-stripe framebuffer, moving white tear-test line, white active
area border, and black side bars. The GT1151 identified as `GT1158` and touch
samples covered the four panel corners and center without coordinate rotation.

At 3,040 submitted frames the driver reported zero dropped frames and zero
frame-complete timeouts. The touch path reported zero I2C and checksum errors.
This is an initial smoke result, not the plan's longer cold-boot or soak gate.
