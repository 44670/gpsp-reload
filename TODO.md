# TODO

- ESP32-S3 semantic instruction support should stay at MIPS parity before each
  native lowering step: helper execution now handles ARM/Thumb SWI 6/7 HLE Div
  and DivArm inline like MIPS, and treats the same reserved ARM/Thumb decode
  buckets as no-ops instead of backend failures.
- Native ARM data-processing lowering now covers simple no-flag
  `AND/EOR/SUB/RSB/ADD/ADC/SBC/RSC/ORR/MOV/BIC/MVN` register/immediate forms.
  It keeps the block PC live in Xtensa `a3`, advances it inline for native ARM
  instructions, and spills it only before helper calls and block exits.
  Flag-setting forms, shifted register operands, conditional forms, and
  PC-writing forms still route through the helper.
- Replace helper-backed ESP32-S3 instruction execution with native Xtensa
  lowering, following the MIPS backend structure.
- Continue reducing hot literal-pool traffic. MIPS does not use a literal pool
  for normal constants; it uses `addiu`/`ori`/`lui`. Xtensa still needs `l32r`
  for helper pointers and hard 32-bit constants, but small immediates should be
  synthesized inline where practical.
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
- Continue the experimental ESP32-S3 PSRAM executable-cache path. Static,
  page-aligned PSRAM JIT storage is now wired in, with emission through the
  DBUS/data view and entry dispatch through the derived IBUS/instruction view.
  The path still needs page validation, hardware self-test, and manual MMU
  setup fallback before it can be considered stable.
- Prototype the PSRAM exec mapping with low-level ESP32-S3 MMU setup instead
  of relying on the public `esp_mmu_map()` write/exec policy. The prototype
  must reproduce the important IDF side effects: freeze/stop external cache
  while changing entries, write the PSRAM MMU table entries, enable the
  instruction cache bus for the exec alias, invalidate affected cache lines,
  and resume cache safely on both cores.
- Gate PSRAM JIT behind a hardware self-test before gpSP uses it: write a tiny
  Xtensa function through the PSRAM data alias, write back D-cache, invalidate
  I-cache for the instruction alias, call the instruction alias, verify the
  return value, then rewrite the function and verify that backpatch visibility
  works.
- Treat the PSRAM JIT path as experimental until the self-test and long
  QEMU/hardware runs pass. Do not expand the generated-code surface until
  instruction fetch, cache sync, and rewrite visibility are proven on hardware.
- Add parity tests that compare dynarec vs interpreter registers, IO writes,
  framebuffer hash, and PNG output.
- Run longer `goodBoyAdv.gba` QEMU captures, then test real gameplay input.
- Clean ESP32-S3 test app `printf` format warnings.
