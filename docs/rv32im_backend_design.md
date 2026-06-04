# RV32IM Backend Design Notes

This note captures the current gpSP native dynarec contract before RV32IM
lowering starts. It is based on `cpu_threaded.c`, `mips/mips_emit.h`, and
`mips/mips_stub.S`, with `main.c`/`main.h` used for scheduler return semantics.

The first RV32IM backend should copy the MIPS backend's structure, not its
instructions. MIPS delay-slot tricks, jump encodings, and code placement rules
must be replaced with RISC-V psABI-safe calls, RV32I/M encodings, and explicit
long-branch/patch-site planning.

## Source Map

- `cpu_backend.c:63-90` calls `init_dynarec_caches()`, `init_emitter()`, and
  `execute_arm_translate(cycles)` for native dynarec execution.
- `cpu_threaded.c:214-225` selects the backend emitter header at compile time.
  RV32IM will need the same kind of `RISCV_ARCH` include path.
- `cpu_threaded.c:227-282` owns platform cache sync. Every backend that writes
  executable code must provide a correct `platform_cache_sync()` path.
- `cpu_threaded.c:2535-2690` owns block lookup, ROM/RAM cache lookup, PC mode
  normalization, translation retry, and cache sync after successful lookup.
- `cpu_threaded.c:2934-3010` scans a block, records branch/SWI exits, respects
  translation gates, and decides where a block can end.
- `cpu_threaded.c:3029-3390` translates ARM/Thumb blocks, emits per-instruction
  code, records internal branch targets, patches internal/external exits, and
  advances the ROM or RAM translation pointer.
- `cpu_threaded.c:3393-3480` initializes the BIOS SWI entry and flushes ROM/RAM
  translation caches.
- `mips/mips_emit.h:56-109` defines the MIPS backend's fixed host-register
  contract for guest state, flags, PC, cycle counter, arguments, and scratch.
- `mips/mips_emit.h:216-276` defines cycle accounting, branch patching,
  indirect branch emission, and block prologue layout.
- `mips/mips_emit.h:1892-1898` emits translation-gate and PC update behavior.
- `mips/mips_emit.h:1966-2011` saves/restores guest state across helper calls.
- `mips/mips_emit.h:2023-2802` builds memory access stubs, patch handlers,
  SMC handling, I/O side-effect paths, and the ROM cache watermark.
- `mips/mips_emit.h:2804-2807` exposes `execute_arm_translate()` as a wrapper
  around the assembly entry stub.
- `mips/mips_stub.S:47-128` documents the MIPS runtime register/state layout.
- `mips/mips_stub.S:155-213` collapses/extracts CPSR flags and saves/restores
  mapped guest registers.
- `mips/mips_stub.S:242-262` implements the update trampoline that calls
  `update_gba()`, handles frame completion, handles PC changes, and resumes or
  returns.
- `mips/mips_stub.S:297-317` implements ARM/Thumb/dual indirect branch helpers.
- `mips/mips_stub.S:321-376` handles I/O side effects, SMC flush, IRQ/HALT, CPU
  sleep, and lookup after PC-changing events.
- `mips/mips_stub.S:399-506` handles SWI, CPSR/SPSR reads and writes, mode
  changes, and SPSR restore branch behavior.
- `mips/mips_stub.S:512-552` implements the native dynarec entry ABI.
- `main.c:118-297` defines `update_gba()`: bit 31 means frame complete, bit 30
  means PC changed due to interrupt/mode work, and low bits carry the next
  cycle budget. `main.h:83-84` exposes the masks used by the interpreter path.
- `memmap.c:19-24` records existing host jump-distance constraints. RV32IM
  needs its own code placement and long-branch plan.

## Contract Boundaries

### Backend Selection And Entry

Native dynarec selection is compile-time and runtime-gated. `cpu_backend_reset()`
initializes the dynarec caches and backend emitter, while
`cpu_backend_execute()` calls `execute_arm_translate(cycles)`. RV32IM must add a
backend include and build path without changing this public API.

The MIPS entry stub receives the cycle count and the shared CPU state pointer,
saves host callee-saved state, installs its state base register, extracts flags
from CPSR, handles sleeping CPUs without waking them, looks up the current ARM
or Thumb block, restores mapped guest registers, and jumps into generated code.
RV32IM needs the same entry shape, with psABI-preserved registers saved before
generated code reuses them.

### Translation Cache Ownership

The frontend owns two generated-code arenas:

- ROM blocks use `rom_translation_cache`, a hash table keyed by `pc | thumb`,
  and a `rom_cache_watermark` that preserves backend stubs and the BIOS SWI
  entry across ROM flushes.
- RAM blocks use `ram_translation_cache` plus per-RAM code tags stored in the
  mirrored SMC/tag areas. RAM invalidation is coarse: flushing RAM cache and
  clearing tag ranges is accepted behavior today.

RV32IM must preserve the ROM/RAM split. The first backend may use simple cache
flushes, but it cannot merge ROM/RAM ownership or lose the BIOS SWI watermark.

### Block Lookup

`block_lookup_address_arm()` and `block_lookup_address_thumb()` normalize PC
alignment, retry translation up to four times, call `translate_icache_sync()` on
success, and return the executable block entry after `block_prologue_size`.
`block_lookup_address_dual()` uses bit 0 of the target address to choose ARM or
Thumb and updates CPSR T-bit state.

RV32IM must keep lookup returning an executable entry pointer compatible with
the generated block prologue size. If RV32IM uses separate data/exec aliases on
some target, lookup must return the executable address while writers and
patchers use the writable address.

### Block Scanning And Emission

`scan_block()` identifies exits before emission. It records direct branch
targets, SWI-to-BIOS exits, conditional-exit information, translation gates,
RAM SMC tags, and block size caps. `translate_block_arm()` and
`translate_block_thumb()` then emit code, insert cycle updates at internal join
points, generate a final translation gate, patch internal branches, record
external exits, and translate/patch external targets after the block body has
been committed.

RV32IM lowering must honor the frontend's recorded exits. Unsupported
instructions can route through helper-backed execution in early bring-up, but
control-flow exits, scheduler boundaries, and patch behavior must still be
native-backend correct.

### Cycle Counter And Scheduler Update

Generated branches subtract accumulated `cycle_count` from the backend cycle
register before testing whether an update is needed. MIPS uses the block
prologue's update trampoline and `mips_update_gba()` to call `update_gba()`.

`update_gba()` returns:

- bit 31 set: a frame completed; return to the outer frontend/scheduler.
- bit 30 set: PC changed, usually due to interrupt handling; perform block
  lookup from the updated PC.
- bits 0..14: next cycle budget, matching `cycles_to_run()`.

RV32IM must preserve those return paths exactly. The update stub must save
guest-visible state before calling C, collapse backend flag state into CPSR,
restore or relookup as required, and return to the host caller on frame
completion.

The `idle_loop_target_pc` path is not optional. MIPS forces the cycle counter to
zero when the branch source PC equals the idle-loop target, then routes through
the update trampoline. RV32IM needs the same semantic effect.

### Translation Gates

The scanner stops at `translation_gate_target_pc[]`, and each translated block
emits a final translation gate that indirect-branches through the backend's
ARM/Thumb lookup helper. This protects frontend control-flow constraints that
are independent of any single opcode lowering.

RV32IM must keep translation gates as real exits, not optimize them away during
early bring-up.

### Branch Patching

The frontend expects two patch forms:

- conditional patching for conditional-block headers
- unconditional patching for recorded direct branch exits

MIPS can patch a 16-bit branch offset or a 32-bit absolute jump instruction in
place. RV32IM must design patch sites before broad lowering starts. Plain
RISC-V direct branch immediates and `jal` immediates have limited range, so the
backend either needs guaranteed close code placement or fixed-size patch sites
large enough for long-branch veneers/sequences. Do not assume a one-word patch
is always enough.

### Helper Calls And Guest State

The MIPS backend keeps many guest registers resident in host registers, then
saves/restores them around C helpers and special memory paths. It also
collapses/extracts CPSR flags at helper boundaries that observe or mutate CPSR.

For RV32IM first phase:

- keep helper calls psABI-correct
- do not keep guest values live across C calls unless the stub saves them
- preserve any guest-visible register, CPSR, SPSR, mode, bus-value, OAM, HALT,
  and sleep-cycle state that a helper can observe
- avoid adding a dynamic guest register cache during the first milestone

A fixed register mapping may be introduced only if it is explicit and covered
by save/restore logic comparable to the MIPS stubs. A state-backed model is
acceptable for the earliest correctness path.

### Memory Helpers, SMC, And I/O Side Effects

MIPS initializes executable memory helper stubs before ordinary block emission.
Those stubs handle fast plain memory paths, unmapped/open loads, backup/RTC
handlers, palette/OAM special stores, ROM page loading, memory-op patching, and
SMC checks. I/O writes can return CPU alert bits that force SMC flush, IRQ
handling, HALT/sleep, or PC lookup.

RV32IM can start with helper-backed memory paths, but the backend must still
honor:

- RAM-code writes triggering `flush_translation_cache_ram()`
- I/O write alerts routing through the scheduler/lookup logic
- OAM update side effects
- ROM page loading when `memory_map_read` has no page
- open-bus/protected BIOS read behavior when helpers require it

### Cache Sync And Executable Memory

`translate_icache_sync()` syncs only newly emitted ROM/RAM ranges. Flushes reset
the last-synced pointers so future lookups sync from the new cache start or ROM
watermark. MIPS uses `__builtin___clear_cache()` in the generic platform path
and performs local instruction sync after self-patching memory handlers where
available.

RV32IM must provide cache synchronization for emitted code and patched code
under qemu-user and any future target. The qemu-user harness may run in a
simple RWX mmap environment, but the backend still needs explicit sync calls so
the code path matches real targets.

### BIOS/SWI And CPU Mode

SWI is not just a branch. MIPS stores supervisor LR/SPSR, changes CPSR to
supervisor mode with IRQ disabled, calls `set_cpu_mode()`, updates open bus,
and then routes execution to the pre-generated BIOS SWI entry. CPSR/SPSR writes
can change mode or PC and must relookup when necessary.

RV32IM must preserve SWI/BIOS entry behavior and CPSR/SPSR helper boundaries
before being treated as a real backend.

## RV32IM First-Phase Implications

- Add `riscv/riscv_emit.h`, `riscv/riscv_codegen.h`, and a runtime stub file
  only after the current MIPS flow remains explainable from this note.
- Emit plain RV32I/M 32-bit instructions first. Do not emit compressed `C`.
- Reserve enough bytes for every patch site before the first branch-patching
  implementation. Prefer conservative long-branch sequences over range
  assumptions.
- Keep a fixed CPU-state base pointer and a cycle counter in known registers.
  Use psABI callee-saved registers only after the entry stub saves them.
- Build the `qemu-riscv32` harness before broad opcode lowering. It should
  prove entry/exit, update paths, helper calls, frame hash/PNG output, and
  interpreter-vs-RV32IM state equivalence.
- Treat a passing raw RISC-V emitter test as necessary but not sufficient. The
  backend is only correct when guest-visible ARM state, memory, scheduler
  return, and frame output match the interpreter.

## Semi-Milestone Gate

This Objective 1 note is a commit boundary only after the source references are
checked against the current worktree. The next semi-milestone should be a
minimal RV32I/M emitter and qemu-user harness scaffold, not broad ARM lowering.
