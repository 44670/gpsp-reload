# TODO

## Current Phase: CoreS3 SE Interpreter Port

- Keep the ESP32-S3 IDF app interpreter-only until CoreS3 SE board bring-up is
  stable. `tests/esp32s3/idf-app` should use `GPSP_TEST_BACKEND=interp`, not
  define `HAVE_DYNAREC`, and not compile `cpu_threaded.c` or
  `esp32s3/xtensa_runtime.c`.
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
  GPIO3 CS. It builds and fails soft in QEMU when AW9523 is absent, but still
  needs a CoreS3 SE hardware run.
- Add CoreS3 SE input next: FT6336 touch on I2C address `0x38` with INT GPIO21,
  mapped to GBA controls through a small on-screen or touch-region scheme.
- Add CoreS3 SE audio after video/input: ES7210 mic ADC, AW88298 speaker amp,
  and I2S pins MCLK GPIO0, BCLK GPIO34, WS GPIO33, DOUT GPIO13, DIN GPIO14.
- Run `idf.py -B build/ build` after each firmware slice. For QEMU, patch a
  flash image with `flash_gba.sh --image ...` or use the helper scripts, which
  do that before launching QEMU. QEMU must use `--qemu-extra-args="-m 8M"` so
  PSRAM matches CoreS3 SE and leaves data-mmap space for the raw `gamepak`
  flash partition.

## Parked Dynarec Work

- ESP32-S3 semantic instruction support should stay at MIPS parity before each
  native lowering step. Helper execution handled ARM/Thumb SWI 6/7 HLE Div and
  DivArm inline like MIPS before this work was parked.
- Native ARM data-processing lowering covered simple no-flag
  `AND/EOR/SUB/RSB/ADD/ADC/SBC/RSC/ORR/MOV/BIC/MVN` register/immediate forms.
  Flag-setting forms, shifted register operands, conditional forms, and
  PC-writing forms still routed through the helper.
- When dynarec resumes, replace helper-backed ESP32-S3 instruction execution
  with native Xtensa lowering, following the MIPS backend structure.
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
  proves instruction fetch, cache sync, and rewrite visibility.
- Prototype manual low-level ESP32-S3 MMU setup only after the interpreter port
  and board drivers are stable.
