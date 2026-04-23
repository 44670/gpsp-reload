# AGENTS

## Project Snapshot

- This repository is `gpSP`, a Game Boy Advance emulator.
- The interpreter truth source is mainly in `cpu.cc` and `gba_memory.c`.
- The native dynarec frontend is in `cpu_threaded.c`.
- Host-specific code emission lives in backend headers such as `mips/mips_emit.h`, `arm/arm_emit.h`, and `arm/arm64_emit.h`.
- Host-specific entry/exit stubs live in files such as `mips/mips_stub.S` and `arm/arm_stub.S`.
- There is also an experimental JS backend (`jsjit_*`), but the next task is a native ESP32-S3 backend, not more JS work.

## How The Existing Dynarec Works

- Execution model is run-until-event, not call-per-guest-function.
- A translated block consumes guest cycles, exits when needed, and returns control to the main scheduler.
- `cpu_threaded.c` scans a guest block, records exits, emits host code, then backpatches internal and external branches.
- ROM and RAM translation caches are separate.
- RAM code invalidation is coarse today: cache flushes are still an accepted mechanism.
- `idle_loop_target_pc` and `translation_gate_target_pc[]` are real control-flow constraints and must be preserved by any backend.
- Cache sync after code emission is platform-specific and mandatory.

## What Matters About The MIPS Backend

- MIPS is a good reference because it is a full native dynarec backend with a clear split:
  - `cpu_threaded.c`: block scanning, exit bookkeeping, branch patching, cache ownership
  - `mips/mips_emit.h`: machine code emission macros and register mapping
  - `mips/mips_stub.S`: runtime ABI, entry loop, indirect branch helpers, cycle/update flow
- The MIPS backend keeps a fixed mapping from guest ARM state to host registers, with the remaining state spilled through the shared CPU state block.
- It uses a dedicated cycle counter register and exits back to the host scheduler when cycles/events require it.
- It treats indirect branch helpers, IRQ/update paths, and SWI/BIOS entrypoints as part of the backend contract, not optional extras.
- It relies on executable translation cache memory and explicit icache sync.
- MIPS has an address placement constraint for generated code: blocks must stay within the same 256 MB region. This is handled in `memmap.c`. ESP32-S3 will have different constraints, but the lesson is the same: backend-specific code placement rules must be designed early, not patched in later.

## ESP32-S3 Backend Direction

- Treat the ESP32-S3 backend as a new native dynarec backend, structurally similar to MIPS/ARM, not as a port of the JS backend.
- First priority is preserving the gpSP backend contract:
  - same guest-visible CPU semantics
  - same scheduler/update boundaries
  - same idle-loop and translation-gate behavior
  - same ROM/RAM cache split and invalidation story
- Use the MIPS backend as the main structural reference for:
  - stub shape
  - register allocation strategy
  - block emission workflow
  - exit/patch model
- Keep the first milestone narrow:
  - block lookup
  - prologue/epilogue
  - direct arithmetic/data-processing ops
  - basic loads/stores
  - direct and indirect branch exits
  - scheduler return on cycle exhaustion or alerts

## Practical Rule For Future Agents

- When in doubt, read `cpu_threaded.c` first. It defines the dynarec contract.
- Use `mips/mips_emit.h` and `mips/mips_stub.S` as the primary native backend reference.
- Do not treat a backend as complete unless it handles control-flow exits, update/scheduler boundaries, cache sync, and invalidation, not just instruction translation.


## References

Use rg (ripgrep) if needed.

~/esp-idf
~/work/CardPuterADV/esp-walkie-talkie
~/CardPuterADV/esp-walkie-talkie/refs -> CoreS3SE, not CoreS3

## ESP32-S3 JIT backend

Target: M5Stack CoreS3 SE
PSRAM: 8MB
Flash as partition, spi mmap, 8MB
No M5 lib deps, like ~/work/CardPuterADV/esp-walkie-talkie, write drivers by yourself.

Also use qemu to test jit headlessly:

https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/tools/qemu.html

