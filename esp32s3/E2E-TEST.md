# ESP32-S3 End-To-End Test Plan

## Current Status

Verified on this repo state with ESP-IDF `v6.0` on 2026-04-24:

- `tests/dhrystone/Makefile` builds both `dhrystone_arm.gba` and
  `dhrystone_thumb.gba`
- `tests/esp32s3/idf-app` boots in Espressif QEMU for `esp32s3`
- `GPSP_TEST_BACKEND=dynarec` passes the embedded Dhrystone ROM in QEMU
- the dynarec run now executes through the ESP32-S3 backend's own ARM block
  interpreter with zero generic `execute_arm()` fallback
- `GPSP_TEST_BACKEND=interp` also passes, which is the control reference

Required one-time host setup:

- install Espressif's QEMU fork, not distro `qemu-system-xtensa`
- command:
  `python3 $IDF_PATH/tools/idf_tools.py install qemu-xtensa`
- then export the tool path before running:
  `source ~/esp-idf/export.sh`

Verified commands:

- dynarec build:
  `idf.py -D GPSP_TEST_BACKEND=dynarec build`
- dynarec run:
  `idf.py qemu monitor`
- interp build:
  `idf.py -D GPSP_TEST_BACKEND=interp build`
- interp run:
  `idf.py qemu monitor`

Host-side emission verifier:

- build and disassemble ARM block stub on a 32-bit host build:
  `make -C tests/esp32s3 disasm-arm`
- build and disassemble Thumb block stub on a 32-bit host build:
  `make -C tests/esp32s3 disasm-thumb`
- the local verifier now uses `gcc -m32`, so pointer-sized metadata issues show
  up in a 32-bit host environment before running QEMU
- this uses Espressif's `xtensa-esp32s3-elf-objdump` on a raw emitted binary blob,
  so it checks the backend's actual emitted instruction bytes without needing to
  boot QEMU first

Verified disassembly body for both ARM and Thumb stubs:

```text
entry   a1, 32
l32r    a2, <helper literal>
l32r    a3, <meta literal>
mov.n   a10, a3
callx8  a2
mov.n   a2, a10
retw.n
```

Verified UART pass lines:

```text
backend=dynarec frames=30 video_frames=30 magic=0x44524859 status=0 iterations=2000 int_glob=5 bool_glob=1 ch1=A ch2=B arr1_8=7 arr2_8_7=2010 ptr=17 next=18
jit blocks_emitted=318 blocks_executed=80861 generic_fallbacks=0 unsupported=0 thumb_blocks=0
result=PASS backend=dynarec
backend=interp frames=30 video_frames=30 magic=0x44524859 status=0 iterations=2000 int_glob=5 bool_glob=1 ch1=A ch2=B arr1_8=7 arr2_8_7=2010 ptr=17 next=18
jit blocks_emitted=1 blocks_executed=0 generic_fallbacks=0 unsupported=0 thumb_blocks=0
result=PASS backend=interp
```

Important current limitation:

- the generic interpreter fallback is no longer on the Dhrystone execution path
- block bodies currently execute through a backend-local ARM block interpreter
  entered from emitted Xtensa stubs, not yet through full per-instruction native
  Xtensa lowering
- that means Dhrystone is now covered end to end by the ESP32-S3 backend
  contract, but the next optimization step is still real native ARM-to-Xtensa
  lowering in `xtensa_emit.h`

## Purpose

This file defines how the future ESP32-S3 native dynarec backend is validated
end to end.

The target is not just "Xtensa code executes". The target is:

- gpSP still obeys the existing `cpu_threaded.c` contract
- translated blocks preserve guest-visible CPU behavior
- scheduler and alert exits still work
- RAM invalidation still works
- the backend can be tested headlessly in QEMU before hardware bring-up

This plan is intentionally QEMU-first and hardware-second.

## Test Philosophy

### 1. Use full-system ESP32-S3 QEMU for E2E

The supported and useful emulation target is Espressif's full-system QEMU for
ESP32-S3, driven through ESP-IDF.

Use:

- `idf.py qemu`
- `idf.py qemu monitor`
- `idf.py qemu gdb`
- `idf.py qemu --qemu-extra-args="-d in_asm,cpu" monitor`

Do not treat `qemu-user` as the primary E2E path. It may still be useful for
isolated Xtensa assembly experiments, but it does not validate:

- ESP32-S3 executable memory setup
- cache invalidation and icache visibility
- MMU-backed executable regions
- RTOS and UART integration
- full backend entry and scheduler return flow

### 2. Compare against known-good references

The ESP32-S3 backend should not invent its own truth source.

Reference behavior should come from:

- host interpreter runs
- existing repo test assets
- gpSP's own state and scheduler model

When possible, compare final outputs instead of intermediate implementation
details.

### 3. Start from narrow deterministic tests

The backend is not ready for arbitrary commercial ROM bring-up on day one.
Initial validation should use:

- tiny self-checking Xtensa exec-cache tests
- tiny guest ROMs with deterministic pass/fail output
- `tests/dhrystone/dhrystone.gba`
- long-run regression ROMs only after smoke tests are stable

### 4. Prefer headless pass/fail artifacts

Every stage should produce machine-checkable output over UART or a file:

- `PASS` or `FAIL`
- register hash
- memory hash
- framebuffer hash when relevant
- executed block count
- fallback count
- SMC flush count

If a test only "looks okay in the monitor", it is not finished.

## What QEMU Can And Cannot Validate

Per Espressif's published ESP32-S3 QEMU support, the useful pieces for this
backend include:

- CPU
- UART
- flash and flash MMU
- PSRAM and PSRAM MMU
- eFuse
- GDMA
- SysTimer
- timer groups

The important unsupported or not-real-hardware areas include:

- Wi-Fi
- Bluetooth
- USB
- RMT
- general-purpose SPI
- I2C
- I2S
- ULP
- GPIO matrix and IOMUX

That is acceptable for this backend. The ESP32-S3 dynarec is primarily a CPU,
memory, cache, and scheduler problem. QEMU is therefore good enough for most
early and mid-stage backend validation.

QEMU is not enough for:

- final board-level display timing confidence
- real peripheral wiring
- real flash and PSRAM bandwidth behavior
- final performance claims

## Existing Repo Assets To Reuse

Use what already exists before creating new test infrastructure.

### Current useful assets

- `tests/dhrystone/dhrystone.gba`
- `tests/dhrystone/dhrystone_runner.c`
- `tests/headless/verify_capture.js`
- `tests/headless/gpsp_headless.js`

### Why these matter

`tests/dhrystone` already gives us:

- a deterministic GBA ROM
- a known pass/fail contract
- a runner on the host side

`tests/headless` already gives us:

- screenshot capture logic
- image verification flow
- a pattern for automated output checking

The ESP32-S3 E2E path should reuse the same ROMs and the same notion of
"deterministic output", even if the execution harness is different.

## Required Build Modes

The backend should be tested in at least these modes.

### Host reference modes

- host interpreter
- host existing JSJIT where useful

These are used only as correctness references.

### ESP32-S3 modes

- ESP32-S3 interpreter build in QEMU
- ESP32-S3 dynarec build in QEMU
- ESP32-S3 dynarec build on hardware

The interpreter build on ESP32-S3 matters because it separates:

- generic porting bugs
- platform integration bugs
- dynarec backend bugs

Do not debug all three at once.

## Required Test Stages

The backend should advance through these gates in order.

### Stage 0: Toolchain And Emulator Sanity

Goal:

- prove the ESP-IDF app builds
- prove ESP32-S3 QEMU boots the app
- prove UART output is captured in CI or scripts

Required checks:

- `idf.py qemu monitor` boots the image
- a trivial test app prints a fixed banner
- `idf.py qemu gdb` can attach
- `--qemu-extra-args="-d in_asm,cpu"` produces usable traces

Pass condition:

- fixed boot banner appears
- exit code and UART parsing are stable

### Stage 1: Executable Cache Self-Test

Goal:

- prove executable memory can be allocated
- prove emitted Xtensa code is callable
- prove icache synchronization is correct

Required checks:

- allocate exec memory with the chosen backend allocation path
- emit a minimal leaf function
- call it from C
- patch the function in place
- sync caches
- call it again and observe the new result

Minimum function set:

- return constant
- add two arguments
- indirect jump through register

Pass condition:

- all emitted functions return expected values
- patched code runs only after explicit cache sync

If this stage is not solid, the backend is not ready for guest blocks.

### Stage 2: Stub ABI Self-Test

Goal:

- prove the assembly stub contract is correct before guest translation starts

Required checks:

- save and restore mapped guest registers
- preserve the chosen cycle counter register
- call helper code and return safely
- exercise direct return to scheduler path
- exercise indirect branch helper path

Pass condition:

- register file after return matches expected values
- no corrupted window state
- no unexpected stack growth or window spill bugs

### Stage 3: Translation Cache And Lookup Smoke Test

Goal:

- prove the backend can create, cache, find, and re-enter translated blocks

Required checks:

- ROM cache allocation and lookup
- RAM cache allocation and lookup
- repeated execution of the same block hits the cache
- basic direct chaining for explicitly near targets
- general exit path for non-near targets

Required instrumentation:

- block compile count
- block lookup hit count
- block lookup miss count
- direct chain count
- indirect exit count

Pass condition:

- repeated execution stops recompiling the same block
- lookup metadata remains consistent across re-entry

### Stage 4: Guest CPU Smoke ROMs

Goal:

- verify a minimal subset of guest instruction translation against fixed ROMs

The first smoke ROM set should focus on:

- ARM data-processing ops
- Thumb data-processing ops
- simple branches
- loads and stores
- basic flag behavior
- BX and ARM/Thumb interworking

Each ROM should:

- run headlessly
- write a pass signature to RAM
- optionally emit a UART line through the test harness

Pass condition:

- every ROM reports exact expected signature

This stage should happen before trying to boot a full game.

### Stage 5: Interpreter Parity On Deterministic ROMs

Goal:

- compare ESP32-S3 dynarec output against interpreter output on fixed assets

Required initial ROM:

- `tests/dhrystone/dhrystone.gba`

Preferred future additions:

- small homebrew smoke ROMs for memory ops, IRQs, BIOS SWIs, and SMC

What to compare:

- final pass/fail status
- final RAM signature
- selected register signature
- frame count to completion

Pass condition:

- dynarec and interpreter agree on all fixed signatures

Small performance differences are acceptable here. Semantic differences are not.

### Stage 6: RAM Invalidation And SMC Tests

Goal:

- prove that translated RAM code is invalidated when guest code changes

Required cases:

- CPU write modifies translated IWRAM code
- CPU write modifies translated EWRAM code
- DMA write modifies translated IWRAM code
- DMA write modifies translated EWRAM code
- self-modifying Thumb code
- self-modifying ARM code

What to observe:

- SMC alert count
- RAM cache flush count
- correct post-modification behavior

Pass condition:

- stale translated code is never executed after modification

For the first milestone, coarse RAM flushes are acceptable. Incorrect reuse is
not acceptable.

### Stage 7: Scheduler, Idle Loop, And Alert Boundary Tests

Goal:

- verify that the backend still obeys gpSP's run-until-event model

Required cases:

- cycle exhaustion return
- IRQ-pending return
- halt or sleep loop return
- `idle_loop_target_pc` handling
- `translation_gate_target_pc[]` block termination
- SWI and BIOS entry boundary handling

What to observe:

- correct return to scheduler
- correct cycle accounting
- no infinite translated loop when an event boundary should force exit

Pass condition:

- event boundaries match interpreter behavior

This is a required completion gate, not a later optimization pass.

### Stage 8: Framebuffer And Long-Run Regression Tests

Goal:

- catch bugs that only show up over time or in mixed CPU and video workloads

Required checks:

- fixed screenshot CRC or hash after N frames for selected ROMs
- long-run execution without crashes
- stable backend counters over long runs

Use:

- host screenshot verification patterns from `tests/headless`
- equivalent UART- or file-reported hashes on ESP32-S3

Pass condition:

- deterministic image hash matches expected output
- no runaway recompilation
- no memory corruption over long runs

### Stage 9: Hardware Confirmation On CoreS3 SE

Goal:

- confirm the QEMU-proven backend also works on the real target board

Target board assumptions from project guidance:

- M5Stack CoreS3 SE
- 8 MB PSRAM
- flash partition and SPI mmap use
- no M5 library dependency

Required checks:

- boots on hardware
- same deterministic smoke ROMs pass
- `dhrystone.gba` passes
- real display path works if present
- no cache-sync-only-on-QEMU bug

Pass condition:

- QEMU-passing smoke tests also pass on hardware

## Recommended Test Artifact Layout

Future ESP32-S3 tests should be kept under a dedicated tree, for example:

```text
tests/esp32s3/
  idf-app/
  smoke_roms/
  host_tools/
  expected/
```

Suggested contents:

- `idf-app/`: ESP-IDF harness app that links gpSP or a reduced backend test
  harness
- `smoke_roms/`: deterministic GBA ROMs
- `host_tools/`: UART parsers, flash image helpers, and QEMU launch wrappers
- `expected/`: known hashes and signatures

## Required Runtime Instrumentation

The ESP32-S3 backend should expose enough counters to make failures obvious.

At minimum, report:

- translated block count
- ROM cache hit and miss counts
- RAM cache hit and miss counts
- direct chain count
- indirect exit count
- helper-call count
- fallback-to-interpreter count, if any fallback exists
- SMC flush count
- scheduler return count by reason

Emit them:

- on explicit test completion
- on failure
- optionally every N frames in long-run tests

## Suggested Output Format

Every automated test should end with one summary line that is easy to parse.

Example:

```text
result=PASS backend=esp32s3_drc rom=dhrystone frames=17 regs=6f1b4c2a ram=9b12c8de fb=1d3e5a77 blocks=482 rom_hits=10943 ram_hits=0 smc_flushes=0 exits_indirect=91 exits_scheduler=17
```

This makes it possible to compare:

- interpreter vs dynarec
- QEMU vs hardware
- debug vs optimized builds

## Debug Workflow

When a test fails, debug in this order:

1. Reproduce in ESP32-S3 interpreter mode.
2. Reproduce in dynarec mode with the smallest possible ROM.
3. Enable block and exit counters.
4. Run under `idf.py qemu --qemu-extra-args="-d in_asm,cpu" monitor`.
5. Use `idf.py qemu gdb` to inspect stub state, register mapping, and emitted
   code.
6. Reduce to an exec-cache or stub-level test if the failure appears before
   guest semantics matter.

Do not start with full games if a smoke ROM can reproduce the bug.

## What Counts As "Backend Complete"

The ESP32-S3 backend is not complete because it can translate arithmetic
instructions in isolation. It is only complete when all of the following are
true:

- translated blocks execute correctly
- control-flow exits are correct
- scheduler and update boundaries are preserved
- cache synchronization is correct after emission and patching
- RAM invalidation works
- deterministic ROM tests pass in QEMU
- the same smoke tests pass on hardware

That is the completion bar for this backend.

## References

- ESP-IDF QEMU guide:
  <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/tools/qemu.html>
- Espressif QEMU feature matrix:
  <https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md>
- ESP32-S3 ISA/backend notes in this repo:
  [esp32s3/ISA.md](./esp32s3/ISA.md)
