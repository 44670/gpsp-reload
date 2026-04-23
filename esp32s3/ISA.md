# ESP32-S3 ISA Notes For A gpSP Native Dynarec

## Purpose

This file records the Xtensa and ESP32-S3 facts that matter for a native
`gpSP` backend. It is not a general Xtensa manual. The goal is to capture the
constraints that drive:

- block emission
- direct and indirect exits
- helper and scheduler call boundaries
- patchable branch templates
- executable cache placement and cache synchronization

The backend contract still comes from `cpu_threaded.c`, `cpu.cc`,
`gba_memory.c`, and the existing native backends. This file only covers the
host ISA side.

## Source Priority

When facts disagree, use this priority order:

1. The live Espressif toolchain selected by `xtensa-esp32s3-elf-gcc`,
   `xtensa-esp32s3-elf-as`, and `xtensa-esp32s3-elf-objdump`.
2. ESP-IDF Xtensa and cache headers.
3. Espressif `crosstool-NG` config and wrapper logic.
4. Older checked-in Xtensa headers in SDK trees.

This matters because the checked-in IDF Xtensa headers are close to the live
toolchain, but not always exact.

## What Espressif crosstool Actually Tells Us

The `crosstool-NG` repo is mostly plumbing for Xtensa dynconfig, not a
hard-coded `esp32s3` ISA description.

- `samples/xtensa-esp-elf/crosstool.config` enables
  `CT_XTENSA_DYNCONFIG=y`, `CT_TARGET_VENDOR="esp"`, `CT_MULTILIB=y`, and
  `CT_TARGET_CFLAGS="-mlongcalls"`.
- `.gitmodules` points at `xtensa-dynconfig`, `xtensa-overlays`, and
  `esp-toolchain-bin-wrappers`.
- The wrapper install step clones one generic toolchain binary into `esp32`,
  `esp32s2`, and `esp32s3` command names.

The practical result is that the real chip-specific truth source is the live
Xtensa dynconfig selected by the toolchain, typically via `xtensa_esp32s3.so`,
not the plain `crosstool-NG` repo by itself.

## Core ISA Summary

The live `xtensa-esp32s3-elf` toolchain reports a 32-bit little-endian Xtensa
core with these relevant features:

- windowed registers enabled
- 64 physical address registers
- density instructions enabled
- zero-overhead loops enabled
- `l32r` available
- `addx`, `sext`, `nsa/nsau`, `min/max` available
- integer `mul16`, `mul32`, `mul32 high`, `div/rem` available
- `s32c1i` and release-sync instructions available
- `threadptr` and boolean registers available

Important missing or constrained features:

- no `CONST16`
- no wide branches
- no predicted branches
- no exclusive load/store pair
- no absolute literals by default

For gpSP, the most important consequences are:

- control-flow reach is limited
- arbitrary constants and far addresses are not cheap
- a runtime JIT must own its own literal, trampoline, and exit-template design

## Instruction Width And Patching Implications

ESP32-S3 Xtensa is variable-length.

- density forms use 16-bit encodings
- many common base instructions use 24-bit encodings
- the core advertises a maximum instruction size of 4 bytes

In practice, local assembly tests produced:

- `mov.n`, `addi.n`, `l32i.n`, `s32i.n`, `retw.n`: 2 bytes
- `call8`, `j`, `extui`, `addx4`, `ssl`, `ssr`, `src`, `sext`: 3 bytes
- `entry`: 3 bytes

This makes Xtensa patching fundamentally different from MIPS and ARM:

- patch sites are byte-oriented
- instruction size is not uniform
- compact forms cannot be safely overwritten with larger forms in place

Backend rule: patchable exits should use fixed-size templates, not arbitrary
compact encodings that may later need to grow.

## ABI And Register Windowing

The default ESP-IDF Xtensa ABI is windowed, not `call0`.

Normal compiled code typically looks like:

- `entry sp, ...`
- body using windowed argument and return registers
- `retw.n`

Normal C calls often use `call8` under that ABI.

However, low-level Xtensa runtime and RTOS code still uses `call0` in carefully
controlled assembly paths, even when the selected ABI is windowed. That is a
useful design clue:

- translated blocks should stay leaf-like whenever possible
- helper, scheduler, IRQ, SWI, and indirect-branch glue should go through a
  small assembly veneer with an explicitly chosen calling convention
- do not mix ad-hoc windowed and `call0` conventions inside emitted code

Windowed Xtensa is workable for a dynarec, but only if emitted blocks avoid
deep call chains. If emitted code starts calling many helpers directly, window
spill and ABI complexity become backend problems instead of stub problems.

## Calls, Long Calls, And Why Static Toolchain Behavior Does Not Save A JIT

Espressif's crosstool sample and ESP-IDF builds add `-mlongcalls` for Xtensa
code. IDF injects it at project level.

For normal compiled objects this is convenient:

- a near call may stay `call8 target`
- a far call may expand into `l32r aX, literal ; callx8 aX`
- the linker may relax it back if the target is close enough

Local object tests confirmed exactly that behavior.

A runtime JIT does not get any of those conveniences:

- no assembler relaxation after emission
- no linker pass
- no auto-generated literal placement

Backend rule: emitted code must choose and write the final call form itself.
Do not design a gpSP backend around "the toolchain will turn this into a long
call for us later", because there is no later stage.

## Branch Reach Constraints

`XCHAL_HAVE_WIDE_BRANCHES` is false, and local assembly tests matched that.

Observed forward reach in local tests:

- plain `j` worked up to about 131070 bytes and then failed immediately after
  that
- direct `beqz` stayed direct to about 2 KiB
- beyond that, the assembler rewrote conditional branches into an inverse short
  branch plus a `j`

Two consequences matter for gpSP:

1. A runtime JIT cannot assume that a directly chained block target is in range.
2. Conditional exit forms need explicit design, not optimistic short-branch
   patching.

Backend rule:

- use direct short branches only for block-local control flow or very near
  internal chains
- represent general exits with fixed templates, local trampolines, or
  register-indirect jumps
- store enough metadata at each patch site to know which template was emitted

## Literals, Constants, And Address Materialization

The core does not have `CONST16`, and the default model uses PC-relative
`l32r`, not absolute literals.

Local compile tests showed:

- small immediates stay inline
- arbitrary 32-bit constants naturally become `.literal` entries plus `l32r`

This is good enough for static code, but it has real backend design impact:

- arbitrary helper addresses are not cheap immediates
- arbitrary block targets are not cheap immediates
- a single global literal area is risky because `l32r` reach is finite

Backend rule:

- do not rely on "free" 32-bit immediate synthesis
- keep literal/trampoline areas local to the translation cache region that uses
  them
- for the first milestone, an explicit side-table load plus indirect jump is
  often simpler than aggressive inline literal-pool packing

## Integer Operations Relevant To ARM7TDMI Translation

Xtensa has enough integer support to keep the first gpSP backend mostly native
for common ARM data-processing and address-generation instructions.

Useful instructions validated in local code generation tests:

- `ssl` + `src` for rotate-left style sequences
- `ssr` + `srl` for variable logical right shifts
- `sext` for sign extension
- `addx2`, `addx4`, `addx8` for scaled address arithmetic
- `extui` for bitfield extraction

This means:

- ARM barrel-shifter style data-processing ops do not need to be helper-heavy
- sign-extension paths can stay inline
- common address math for loads, stores, and block stepping can stay inline

That said, guest-visible CPU semantics still come from gpSP, not the host ISA.
Use these instructions to implement the correct guest behavior, not to invent a
host-driven model.

## Alignment And Memory Access Rules

The toolchain defaults include `-mstrict-align`.

That does not change guest semantics, but it is a useful warning for backend
design:

- only use direct host loads and stores on proven naturally aligned paths
- keep unaligned guest cases on helper paths
- do not assume host unaligned accesses are a free substitute for GBA memory
  semantics

For the first milestone, the safest split is:

- inline aligned fast paths for proven-safe regions and widths
- helper exits for everything else

## Executable Memory On ESP32-S3

ESP-IDF exposes executable memory, but there are two different stories:

### Internal executable heap

`MALLOC_CAP_EXEC` exists and heap code treats it as an IRAM allocation request.
For a first native backend, this is the safest starting point.

Pros:

- simple
- low latency
- predictable
- matches the existing native dynarec assumption of executable cache memory

Cons:

- limited capacity

### Executable MMU mappings for flash or PSRAM

ESP-IDF also exposes executable MMU-mapped regions with exec/read/write
capabilities. That makes executable external memory technically possible.

Pros:

- much larger potential cache area

Cons:

- different latency profile
- heavier cache and mapping considerations
- not a good baseline for the first backend milestone

Backend rule: first milestone should use internal executable memory. Treat
PSRAM-backed executable caches as a later optimization tier, not the baseline.

## Cache Synchronization Is Mandatory

Like the other native backends, an ESP32-S3 backend must explicitly handle
instruction-cache visibility after emission and after backpatching.

Available mechanisms include:

- Xtensa HAL functions such as `xthal_icache_region_invalidate()` and
  `xthal_icache_sync()`
- ESP-IDF cache helpers such as `cache_ll_invalidate_addr()`
- `esp_cache_msync()` for mapped-memory cases

Backend rule:

- sync after block emission
- sync again after any in-place patching
- do not hide cache sync behind vague "finalize later" logic

One practical warning: the checked-in IDF Xtensa headers and the live toolchain
did not agree on every cache detail in local inspection. Avoid baking cache
line assumptions into backend code when an API can do the right thing for the
actual runtime configuration.

## Memory Protection Caveat

With `CONFIG_ESP_SYSTEM_MEMPROT`, some normal executable-heap capabilities are
reduced or removed from the standard heap layout.

Backend rule:

- probe executable allocation at init time
- fail loudly or fall back to the interpreter if exec memory is unavailable
- do not silently continue with a non-executable cache buffer

## gpSP Backend Consequences

These are the host-side rules that fall out of the ISA facts above.

### 1. Keep the existing dynarec contract

The ESP32-S3 backend should preserve the gpSP model already defined in
`cpu_threaded.c`:

- run-until-event, not call-per-guest-function
- separate ROM and RAM translation caches
- explicit scheduler and alert exits
- preserved `idle_loop_target_pc` and `translation_gate_target_pc[]`
- coarse RAM invalidation is acceptable for the first milestone

### 2. Follow the MIPS structural split

Use the MIPS backend as the reference shape:

- `cpu_threaded.c`: scan, classify exits, own cache policy, own patch tables
- `esp32s3/xtensa_emit.h`: encode instructions and register mapping
- `esp32s3/xtensa_stub.S`: entry loop, block lookup, helper ABI, scheduler
  exits, indirect-branch helpers

Do not try to turn `cpu_threaded.c` into a full Xtensa assembler by itself.

### 3. Prefer leaf translated blocks

Translated blocks should usually:

- load their fixed state
- execute guest instructions inline
- branch to nearby internal labels when possible
- exit to a stub for helper work instead of calling C directly

This keeps windowing under control and makes register ownership easier.

### 4. Use fixed exit templates

Because Xtensa is variable-length and short-range, patchable exits should use
preplanned templates such as:

- unconditional indirect-exit template
- conditional-exit template
- helper-call template
- direct-chain template for explicitly near targets only

Avoid patching arbitrary compact encodings in place.

### 5. Do not depend on assembler trampolines

Assembler-generated trampolines and linker relaxations help static objects.
They do not exist for a runtime translation cache.

If a block exit needs a far target, the emitter must already know how that
far-target form works.

### 6. Separate "near chain" and "general exit" paths

Near internal chains can use direct forms when the target is provably in range.
Everything else should use:

- a local trampoline
- a per-cache literal-loaded indirect jump
- or a return to the central stub and lookup path

The first backend milestone should bias toward correctness and simpler patching,
not maximal direct chaining.

### 7. Keep RAM invalidation simple at first

Xtensa branch and literal constraints are already enough complexity for the
first backend. RAM self-modifying code should keep gpSP's current coarse flush
story initially. Better RAM invalidation granularity can come later.

## Non-Goals And Bad Assumptions

Do not assume any of the following:

- checked-in Xtensa headers are always the same as the live toolchain
- `-mlongcalls` helps runtime-emitted code
- assembler trampolines will exist for JIT blocks
- executable PSRAM is a drop-in replacement for internal exec heap
- direct `call8` or direct `j` can reach arbitrary helpers or blocks
- uniform instruction width makes patching simple

## Practical Validation Commands

These are useful when re-validating the backend assumptions on a live system:

```sh
xtensa-esp32s3-elf-gcc -Q --help=target
xtensa-esp32s3-elf-gcc -dM -E -
xtensa-esp32s3-elf-as --target-help
xtensa-esp32s3-elf-objdump -dr <object.o>
```

Small compile and disassembly tests are worth keeping around for:

- branch reach
- long-call expansion
- literal-pool materialization
- shift and rotate lowering
- sign-extension lowering

For this backend, those concrete emitter checks are more trustworthy than
memory of generic Xtensa documentation.
