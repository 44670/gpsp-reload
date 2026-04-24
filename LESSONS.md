# Lessons

- Keep the MIPS backend as the structural reference: block lookup, exits,
  scheduler return, invalidation, and cache sync matter as much as opcode
  lowering.
- Do not call a backend complete because a smoke ROM or Dhrystone passes; games
  expose control-flow, timing, IO, and Thumb edge cases much faster.
- Preserve gpSP's run-until-event contract. `update_gba()`, frame completion,
  HALT/IRQ behavior, idle-loop exits, and translation gates are backend
  requirements.
- Treat interpreter behavior as the truth source. Compare registers, IO writes,
  memory, frame hashes, and PNG output against interpreter runs.
- Debug CPU divergence with narrow traces first: PC ranges, IO ranges, and
  breakpoint stops beat huge logs.
- A serial debugger is worth the small hook cost. It needs `regs`, `mem`,
  `op`, `jit`, `tracepc`, `watchio`, `bp`, `breakio`, `stepi`, `cont`, and
  frame/PNG commands.
- `run` and `cont` should be different: `run` drives libretro video/audio frame
  callbacks; `cont` should stop at CPU scheduler/debug boundaries.
- QEMU automation should be scriptable and self-terminating. Batch commands must
  exit QEMU after `result=PASS/FAIL` so tests do not hang.
- Xtensa/ESP32-S3 details must be verified with Espressif tools and QEMU, not
  guessed from generic Xtensa knowledge.
- Host-only emit checks are useful, but real target validation should use
  ESP-IDF QEMU and `xtensa-esp32s3-elf-objdump`.
- Thumb instruction decoding is easy to get subtly wrong; verify full opcode
  fields, not just nearby cases that worked in one test.
- Keep debugger hooks weak and inert by default so normal interpreter and
  dynarec builds are unaffected.
- Avoid debugging rendering, CPU semantics, and backend codegen at the same
  time. Prove which layer diverges first.
- Zero generic fallback is necessary but not sufficient; a backend can execute
  every block and still produce wrong display state.
- Make every test output parseable summary lines with backend, counters, frame
  count, framebuffer hash, and failure reason.
