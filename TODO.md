# TODO

- Replace helper-backed ESP32-S3 instruction execution with native Xtensa
  lowering, following the MIPS backend structure.
- Start with hot safe ops: ARM/Thumb ALU, shifts, compares, moves, and flag
  updates.
- Add native load/store paths for byte, halfword, word, signed loads, and GBA
  memory wait-state accounting.
- Implement direct branch emission and Xtensa branch patching before re-enabling
  external-exit pretranslation.
- Keep scheduler exits correct: cycle exhaustion, HALT, IRQ, SWI/BIOS,
  idle-loop target, and translation gates.
- Remove `HOST_MAX_BLOCK_SIZE=16` only after long-block QEMU tests pass.
- Add parity tests that compare dynarec vs interpreter registers, IO writes,
  framebuffer hash, and PNG output.
- Run longer `goodBoyAdv.gba` QEMU captures, then test real gameplay input.
- Clean ESP32-S3 test app `printf` format warnings.
- Delete stray local artifacts `0x10118` and `0x118` if confirmed accidental.
