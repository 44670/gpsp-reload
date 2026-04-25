# TODO

## Current Phase: CoreS3 SE Playable Firmware And Experimental JIT

- `tests/esp32s3/idf-app` now defaults to `GPSP_TEST_MODE=play` and
  `GPSP_TEST_BACKEND=dynarec`. This is the real firmware path: it runs
  `retro_run()` continuously instead of stopping after the old test-frame
  budget.
- `GPSP_TEST_MODE` is still available for harness workflows:
  - `play`: endless CoreS3 SE firmware loop.
  - `dhrystone`: finite dhrystone ROM regression.
  - `frames`: finite QEMU framebuffer capture/regression.
  - `debug`: UART command debugger.
- Play mode uses fixed interval frameskip `1`: every other frame is skipped.
  On skipped frames gpSP bypasses scanline rendering through `skip_next_frame`,
  and the ESP32-S3 frontend does not update the LCD.
- Play mode has a static FreeRTOS status task with a static stack/TCB. It prints
  progress independently of delivered video frames and independently of
  `retro_run()` returning, so JIT stalls can be distinguished from video silence.
- CoreS3 SE console output/input should use the ESP32-S3 USB Serial/JTAG
  controller, not UART0. The IDF app defaults set
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.
- Experimental ESP32-S3 JIT is being turned back on first. Treat it as a
  hardware bring-up target, not stable emulation yet. The build now compiles
  `cpu_threaded.c`, `esp32s3/xtensa_runtime.c`, and defines `HAVE_DYNAREC`
  when `GPSP_TEST_BACKEND=dynarec`.
- Use only the ESP-IDF `build/` directory for ESP32-S3 build, QEMU, capture,
  and debugger workflows.
- Preserve static PSRAM storage for ESP32-S3-owned emulator buffers:
  framebuffer, post-process framebuffer, previous-frame mix buffer, audio
  sample buffer, QEMU frame capture, and the 1 MB gamepak fallback window.
- ROM data lives in the raw SPI flash `gamepak` data partition and is mapped
  with `esp_partition_mmap()`. The partition is just the `.gba` byte stream:
  no app embedding, no metadata/header wrapper, and no sidecar metadata
  partition. Use `tests/esp32s3/idf-app/flash_gba.sh` to write it.
- Bring up direct CoreS3 SE board drivers without M5 library dependencies,
  following `/home/john/work/CardPuterADV/esp-walkie-talkie`.
- CoreS3 SE display output is now ported as `esp32s3/cores3se_lcd.c`:
  320x240 RGB565 over SPI3 with GPIO36 SCLK, GPIO37 MOSI, GPIO35 DC, and
  GPIO3 CS. Hardware verified: color order is correct and the strip-transfer
  path waits after each `esp_lcd_panel_io_tx_color()` before reusing the static
  DMA strip buffer.
- Add CoreS3 SE input next: FT6336 touch on I2C address `0x38` with INT GPIO21,
  mapped to GBA controls through a small on-screen or touch-region scheme.
- Add CoreS3 SE audio after video/input: ES7210 mic ADC, AW88298 speaker amp,
  and I2S pins MCLK GPIO0, BCLK GPIO34, WS GPIO33, DOUT GPIO13, DIN GPIO14.
- Run `idf.py -B build/ build` after each firmware slice. For QEMU, patch a
  flash image with `flash_gba.sh --image ...` or use the helper scripts, which
  do that before launching QEMU. QEMU must use `--qemu-extra-args="-m 8M"` so
  PSRAM matches CoreS3 SE and leaves data-mmap space for the raw `gamepak`
  flash partition.

## Experimental Dynarec Work

- ESP32-S3 semantic instruction support should stay at MIPS parity before each
  native lowering step. Helper execution handled ARM/Thumb SWI 6/7 HLE Div and
  DivArm inline like MIPS before this work resumed.
- Native ARM data-processing lowering covered simple no-flag
  `AND/EOR/SUB/RSB/ADD/ADC/SBC/RSC/ORR/MOV/BIC/MVN` register/immediate forms.
  Flag-setting forms, shifted register operands, conditional forms, and
  PC-writing forms still routed through the helper.
- Replace helper-backed ESP32-S3 instruction execution with native Xtensa
  lowering incrementally, following the MIPS backend structure.
- Continue reducing hot literal-pool traffic when native lowering resumes.
- Pass or preserve the CPU/JIT state pointer through the entry ABI so generated
  blocks do not reload it except at prologue/veneer boundaries.
- Add native load/store paths for byte, halfword, word, signed loads, and GBA
  memory wait-state accounting.
- Implement direct branch emission and Xtensa branch patching before re-enabling
  external-exit pretranslation.
- Keep scheduler exits correct: cycle exhaustion, HALT, IRQ, SWI/BIOS,
  idle-loop target, and translation gates.
- Keep the PSRAM executable-cache path experimental until CoreS3 SE hardware
  proves instruction fetch, cache sync, rewrite visibility, long-run cache
  flushing, and side-table exhaustion behavior.
- Before treating JIT as stable, make helper side-table exhaustion trigger a
  clean ROM/RAM cache flush and retry, matching translation-buffer-full
  behavior.
- Prototype manual low-level ESP32-S3 MMU setup only after the ESP-IDF static
  PSRAM executable alias path is proven or clearly insufficient.
