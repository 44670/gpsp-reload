# TODO

- ESP32-S3 semantic instruction support should stay at MIPS parity before each
  native lowering step: helper execution now handles ARM/Thumb SWI 6/7 HLE Div
  and DivArm inline like MIPS, and treats the same reserved ARM/Thumb decode
  buckets as no-ops instead of backend failures.
- Native ARM data-processing lowering now covers simple no-flag
  `AND/EOR/SUB/RSB/ADD/ADC/SBC/RSC/ORR/MOV/BIC/MVN` register/immediate forms.
  Flag-setting forms, shifted register operands, conditional forms, and
  PC-writing forms still route through the helper.
- Replace helper-backed ESP32-S3 instruction execution with native Xtensa
  lowering, following the MIPS backend structure.
- Keep the first Xtensa backend state-backed, but stop spilling `OFF_PC` after
  every native instruction. Keep a live block PC scratch register and spill PC
  only before helper calls, exits, scheduler/update paths, and debug-visible
  boundaries.
- Reduce hot literal-pool traffic. MIPS does not use a literal pool for normal
  constants; it uses `addiu`/`ori`/`lui` and keeps PC live. Xtensa still needs
  `l32r` for helper pointers and hard 32-bit constants, but PC increments and
  small immediates should be synthesized inline where practical.
- Pass or preserve the CPU/JIT state pointer through the entry ABI so generated
  blocks do not reload it except at prologue/veneer boundaries.
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
