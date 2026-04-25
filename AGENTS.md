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

## ESP32-S3 Current Architecture And Status

- The active ESP32-S3 backend is a native Xtensa dynarec with a simple state-backed execution model.
- Guest ARM registers remain in the compact CPU/JIT state block. Do not add a guest register cache yet.
- Current generated-code storage is owned by `esp32s3/psram_static.c` and declared in `esp32s3/psram_static.h`.
- ESP32-S3-owned runtime buffers should be static PSRAM, not dynamic heap allocations:
  - ROM and RAM JIT caches
  - main video buffer
  - post-process video buffer
  - previous-frame mix buffer
  - audio sample buffer
  - QEMU/test frame-capture buffer
  - current 1 MB gamepak paging fallback window
- JIT cache writes use the PSRAM DBUS/data alias. JIT execution dispatch converts block entry data pointers to the derived IBUS/instruction alias.
- `platform_cache_sync()` on ESP32-S3 must sync both aliases: write back the data range, then invalidate the aligned instruction range for the exec alias.
- JIT cache sizes are intentionally capped for the 8 MB CoreS3 SE PSRAM target:
  - ROM JIT cache `<= 2 MB`
  - RAM JIT cache `<= 384 KB`
- Current native lowering includes simple no-flag ARM data-processing forms. Unsupported forms still route through helper-backed execution.
- The backend has passed host Xtensa codegen tests and ESP-IDF QEMU dhrystone dynarec smoke tests, but PSRAM executable-cache trust still requires real hardware validation.
- Still pending before treating PSRAM JIT as stable:
  - page-by-page validation that static JIT cache pages map to PSRAM
  - manual ESP32-S3 MMU register fallback if the derived IBUS alias is not enough on hardware
  - startup PSRAM executable self-test that verifies first execution and rewrite/backpatch visibility
  - fuller frontend split where lookup returns exec aliases and all writes/patches use data aliases
  - ROM flash partition / SPI mmap path so the 1 MB PSRAM fallback window is not the final ROM data story

## ESP32-S3 Xtensa ABI Direction

- ESP32-S3 uses Xtensa with a windowed ABI: instructions only address `a0..a15` at once, even though the core has more physical window backing registers.
- Generated JIT code must not assume it can freely allocate all physical Xtensa registers. Treat the visible register budget as:
  - `a0`: call/return linkage, reserved
  - `a1`: stack pointer, reserved
  - `a2..a15`: usable inside generated code
- Keep one Xtensa register permanently pointing at compact CPU/JIT state. This is the Xtensa equivalent of MIPS `reg_base`.
- Use this fixed CPU/JIT state layout for the first state-backed backend:
  - `OFF_R0 = 0x00`: guest `r0`
  - `OFF_R1 = 0x04`: guest `r1`
  - `OFF_R2 = 0x08`: guest `r2`
  - `OFF_R3 = 0x0C`: guest `r3`
  - `OFF_R4 = 0x10`: guest `r4`
  - `OFF_R5 = 0x14`: guest `r5`
  - `OFF_R6 = 0x18`: guest `r6`
  - `OFF_R7 = 0x1C`: guest `r7`
  - `OFF_R8 = 0x20`: guest `r8`
  - `OFF_R9 = 0x24`: guest `r9`
  - `OFF_R10 = 0x28`: guest `r10`
  - `OFF_R11 = 0x2C`: guest `r11`
  - `OFF_R12 = 0x30`: guest `r12`
  - `OFF_R13 = 0x34`: guest `r13` / SP
  - `OFF_R14 = 0x38`: guest `r14` / LR
  - `OFF_R15 = OFF_PC = 0x3C`: guest `r15` / PC
  - `OFF_CPSR = 0x40`: guest CPSR
  - `OFF_CPU_MODE = 0x44`: guest CPU mode
  - `OFF_CPU_HALT_STATE = 0x48`: CPU halt state
  - `OFF_BUS_VALUE = 0x4C`: bus value
  - `OFF_N_FLAG = 0x50`: dynarec N flag scratch
  - `OFF_Z_FLAG = 0x54`: dynarec Z flag scratch
  - `OFF_C_FLAG = 0x58`: dynarec C flag scratch
  - `OFF_V_FLAG = 0x5C`: dynarec V flag scratch
  - `OFF_SLEEP_CYCLES = 0x60`: sleep cycles
  - `OFF_OAM_UPDATED = 0x64`: OAM updated flag
  - `OFF_SAVE0 = 0x68`: mirrored `REG_SAVE`
  - `OFF_SAVE1 = 0x6C`: mirrored `REG_SAVE2`
  - `OFF_SAVE2 = 0x70`: mirrored `REG_SAVE3`
  - `OFF_SAVE3 = 0x74`: mirrored `REG_SAVE4`
  - `OFF_SAVE4 = 0x78`: mirrored `REG_SAVE5`
  - `OFF_SAVE5 = 0x7C`: mirrored `REG_SAVE6`
  - `OFF_SPSR = 0x80`: `spsr[0]`
  - `OFF_SPSR_END = 0x98`: one-past `spsr[5]`
  - `OFF_REG_MODE = 0x98`: `reg_mode[0][0]`
  - `OFF_REG_MODE_END = 0x15C`: one-past `reg_mode[6][6]`
  - `OFF_JIT_CYCLES = 0x15C`: signed JIT cycle counter
  - `OFF_JIT_ALERT = 0x160`: JIT CPU alert bits
  - `OFF_EXIT_REASON = 0x164`: block exit reason or backend status
  - `OFF_STATE_SIZE = 0x168`: end of fixed state header
- The first `0x80` bytes intentionally mirror `reg[0..31]` from `cpu.h`. Do not insert JIT-only fields before `0x80`; append them after the mirrored CPU/dynarec state.
- Use the state pointer for all guest register access. Keep the first Xtensa backend simple: guest registers stay in CPU/JIT state, and generated code uses `l32i`/`s32i` with fixed offsets for each guest register.
- Keep ordinary backend scratch in Xtensa registers, not in the CPU/JIT state block. Use memory scratch only for values that must survive helper calls, scheduler/update paths, exits, or debug inspection.
- Do not add a guest register cache yet. Correct block execution, exits, helper calls, scheduler boundaries, cache sync, and invalidation matter more than reducing state loads/stores in the first native backend milestone.
- Because helper calls can rotate or clobber the visible register window, do not keep guest register values live across C helper calls. Reload guest values from CPU/JIT state after helper/update paths as needed.
- A practical first mapping is:
  - `a2`: CPU/JIT state base pointer
  - `a3`: cycle counter, if kept live
  - `a4..a15`: scratch temps for emitted operations, address calculations, PC/CPSR work, and helper setup
- A later optimization pass may add a register cache, but it must be a separate step after the simple state-backed JIT is correct and covered by tests.
- Cache sync after emitting executable Xtensa code remains mandatory.

## Practical Rule For Future Agents

- When in doubt, read `cpu_threaded.c` first. It defines the dynarec contract.
- Use `mips/mips_emit.h` and `mips/mips_stub.S` as the primary native backend reference.
- Do not treat a backend as complete unless it handles control-flow exits, update/scheduler boundaries, cache sync, and invalidation, not just instruction translation.


## References

Use rg (ripgrep) if needed.

Use `build/` for ESP-IDF builds and QEMU runs. Do not introduce custom build directories such as `build-v6.0-esp32s3`.

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
