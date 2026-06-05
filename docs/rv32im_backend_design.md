# RV32IM Backend Design Notes

This note captures the gpSP native dynarec contract for RV32IM bring-up and
tracks the first-phase proof status. It is based on `cpu_threaded.c`,
`mips/mips_emit.h`, and `mips/mips_stub.S`, with `main.c`/`main.h` used for
scheduler return semantics.

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
  in the same frontend/backend split described above.
- Emit plain RV32I/M 32-bit instructions first. Do not emit compressed `C`.
- Reserve enough bytes for every patch site before the first branch-patching
  implementation. Prefer conservative long-branch sequences over range
  assumptions.
- Keep the first backend state-backed. Do not add a guest register cache before
  helper calls, exits, scheduler return, cache sync, and invalidation are
  proven.
- Use qemu-user proofs before broad opcode lowering. Each new backend boundary
  should have a deterministic `result=PASS` runtime case before it is treated
  as stable.
- Treat a passing raw RISC-V emitter test as necessary but not sufficient. The
  backend is only correct when guest-visible ARM state, memory, scheduler
  return, and frame output match the interpreter.

## Current RV32IM Proof Status

The RV32IM backend now has a standalone qemu-user proof suite in
`tests/rv32im/` and production lowering in `riscv/riscv_runtime.c` /
`riscv/riscv_emit.h`. The current validated surface includes:

- raw RV32I/M emitter encoding checks against clang/LLVM reference output
- qemu-riscv32 ABI entry/return checks
- top-level unix libretro core build with `HAVE_DYNAREC=1` and
  `CPU_ARCH=riscv`, proving the production `cpu_threaded.c` / RV32IM runtime
  integration still compiles and links outside the standalone qemu-user shim
- data-processing, flag-producing data-processing, multiply, long multiply,
  PSR, load/store, PC-relative load/store memory,
  register-offset load/store, PC-register-offset word/byte load/store, shifted-LSL/LSR/ASR/ROR
  PC-register-offset word/byte load/store, writeback
  memory, register-offset writeback memory, immediate, PC-relative,
  register-offset, PC-register-offset load/store, and writeback halfword memory, block memory, SWP, SWI,
  HLE div, and
  PC-source
  runtime cases
- helper-backed memory paths and CPU alert handling for SMC, IRQ, and HALT
- scheduler update and frame-complete exits through `update_gba()` semantics
- cycle checkpoints emitted at frontend internal branch join points
- conditional block headers for ARM conditions `0..13`
- internal, external, conditional, SWI, and absolute RV32IM branch patch sites
- ARM and Thumb lookup/fallback paths, including restored-SPSR Thumb fallback
- SPSR restore for `Rd=PC,S=1` data-processing writes and `LDM ... {pc}^`
- scriptable qemu-user harness commands for `load`, `reset`, `backend`, `run`,
  `cont`, `stepi`, `stepb`, `regs`, `mem`, `watchio`, `counters`,
  `fallbacks`, `sched`, `tracepc`, `bp`, `framehash`, `compare`, `rejects`,
  `png`, and `quit`
- successful qemu-user harness commands use stable
  `result=PASS command=...` summary lines, and failure paths use
  `result=FAIL command=...`
- explicit runtime-snapshot `run runtime` command that executes the selected
  backend's runtime fixture snapshot and reports state, memory, scheduler,
  frame, and backend-counter hashes
- explicit RV32IM `cont runtime [offset]` command that reports a bounded
  scheduler-boundary window from the runtime workload using the same event
  encoding as `sched runtime`
- explicit RV32IM `stepi runtime [count]` command that reports a bounded
  guest-PC prefix expanded from executed native block metadata, tagged with
  the same Thumb low-bit encoding as the lookup trace
- explicit runtime-snapshot `framehash runtime` and `png <path> runtime`
  artifact paths derived from the selected backend's compare snapshot
- explicit runtime-snapshot `regs runtime` dump for the selected backend's
  final compare fixture CPU state
- explicit runtime-snapshot `counters runtime` dump for selected-backend
  block/fallback/native counters, fallback source breakdown, state, memory,
  scheduler, and snapshot hashes
- explicit RV32IM `rejects runtime` emitter-contract audit for unsafe opcodes
  that must stay rejected by native lowering, currently `MRS r15,CPSR`,
  `MSR CPSR,r15`, multiply/long-multiply with `rm == pc`, HLE-reserved SWI,
  and `SWP` with `rm == pc`; it also pins 13 non-AL conditional opcode
  rejections across the direct native emitter families, preserving the rule
  that ARM condition handling enters through conditional block headers instead
  of per-opcode lowering, and four byte/halfword load-to-PC rejections that
  stay helper-routed until those PC-write forms have interpreter-parity proof;
  six PC-base writeback forms and one register-offset shifted-register form
  are pinned the same way
- explicit RV32IM `fallbacks runtime [offset]` dump for observed runtime
  fallback events, including initial lookup, relookup, and unsupported-block
  categories plus PC, ARM/Thumb mode, lookup result, and cycle budget; the
  smoke gate pins tail windows that prove ARM and Thumb initial lookup
  miss/invalid paths plus ARM and Thumb unsupported-block fallback PCs
- runtime snapshot frame/hash artifacts include the fallback source breakdown,
  not only the aggregate fallback count
- explicit RV32IM `sched runtime [offset]` dump for scheduler-boundary events
  observed during the runtime workload, including lookup, update, interpreter
  remainder, flush, IRQ-check, and HALT-state counters
- explicit RV32IM `stepb runtime [count]` block-step prefix from the runtime
  lookup trace, with Thumb lookups tagged in bit 0
- explicit RV32IM `tracepc runtime [count] [offset]` lookup trace window from
  the runtime workload, with Thumb lookups tagged in bit 0
- explicit RV32IM `bp <pc> runtime` lookup-breakpoint query against the
  runtime workload trace, using the same Thumb low-bit encoding
- explicit RV32IM `mem <addr> <len> runtime [offset]` helper-event dump for
  observed runtime memory reads/writes in the selected address range
- explicit RV32IM `mem <addr> <len> runtime-bytes` shadow byte dump for the
  `0x02000000..0x0200ffff` fixture window, populated by actual RV32IM helper
  writes during the runtime workload
- explicit RV32IM `watchio <addr> <len> runtime [offset]` helper-event view
  for observed runtime IO-window reads/writes, currently exercised by a real
  store helper event at `0x04000028`
- qemu-user harness `compare` execution of generated RV32IM
  `ADD r2, r0, r1`, `ADDS`, `SUBS`, `RSBS`, `CMP`, `MUL`, `MLA`,
  `UMULL`, `SMULL`,
  `UMULLS`, `SMULLS`, `UMLAL`, `SMLAL`, `UMLALS`, `SMLALS`,
  `ADC`, `SBC`, `RSC`, `ADCS`, `SBCS`, `RSCS`, `ANDS`, `EORS`,
  `ORRS`, `MOVS`, `BICS`, `MVNS`, `BIC`, `MVN`, `RSB`,
  shifted and register-shifted `EOR`/`ADD`/`ORR`/`MOV`,
  register-shifted `ANDS`/`EORS`/`MOVS`/`ORRS`/`TST`,
  `TEQ`, `CMN`, `MRS CPSR`,
  `MRS SPSR`, `MSR CPSR_flg`, `MSR CPSR_ctl`, `MSR SPSR`,
  `LDR`, `LDRB`, `LDR pc`, PC-relative `LDR`/`LDRB`/`STR`, `STR`, `STRB`,
  `STR pc`,
  register-offset `STRB`, PC-register-offset `STR`, PC-register-offset `STRB`,
  register-offset writeback `STR`, post-index register-offset `LDRB`,
  `LDRH`, PC-relative `LDRH`, register-offset `LDRH`,
  PC-register-offset `LDRH`,
  `LDRSB`, PC-relative `LDRSB`, register-offset `LDRSB`,
  PC-register-offset `LDRSB`,
  `LDRSH`, PC-relative `LDRSH`, register-offset `LDRSH`,
  PC-register-offset `LDRSH`,
  post-index writeback `LDRSH`,
  `STRH`, PC-relative positive/negative `STRH`, register-offset `STRH`,
  PC-register-offset `STRH`,
  writeback `STRH`,
  `STMIA`, `LDMIA`, `STMDB sp!`, `LDMIA ... {pc}`,
  `LDMIA ... {pc}^`,
  HLE `Div`, HLE `DivArm`, PC-source data-processing/test ops,
  register-offset/shifted-LSL/shifted-LSL-with-PC/shifted-LSR/shifted-LSR-with-PC/shifted-ASR/shifted-ASR-with-PC/shifted-ROR/shifted-ROR-with-PC/RRX load ops, including shifted-LSL/LSR/ASR/ROR PC-register-offset word loads,
  shifted-LSL/shifted-LSR/shifted-ASR/shifted-ROR/RRX register-offset stores and shifted-LSL/shifted-LSR/shifted-ASR/shifted-ROR/RRX-store
  remaining-cycle handoffs, shifted-LSL/shifted-LSR/shifted-ASR/shifted-ROR PC-register-offset byte/word stores,
  pre/post-index writeback memory ops,
  register-offset writeback/post-index memory ops,
  `SWP`, `SWPB`,
  direct-branch-to-native-target, far external patched-branch target chaining,
  patched-branch target native fallthrough chaining,
  internal patched-branch target chaining, and
  `BL` link-register branch-to-native-target, and `BX r7`
  indirect-branch-to-native-target runtime fixtures, SWI-to-BIOS target
  mode/bus/banked-state effects, patchable SWI-to-native-target chaining,
  patchable SWI target native fallthrough chaining,
  conditional-header truth-table taken/skipped behavior for ARM conditions `0..13`,
  conditional-header target native fallthrough chaining,
  `MOV pc, r14` PC-write behavior, `MOVS pc, r14` SPSR restore behavior,
  store-triggered SMC/IRQ/HALT alert handling, store-triggered SMC/IRQ native
  chaining after alert handling, byte-store SMC/IRQ/HALT alert handling,
  halfword SMC/IRQ alert handling, store/halfword/block/SWP HALT-alert handling,
  block-memory SMC/IRQ alert handling, SWP-triggered SMC/IRQ alert handling, idle-loop gate, unsupported-block,
  partial-unsupported native discard
  fallback, ARM lookup-miss/invalid fallback, Thumb lookup-miss/invalid fallback, and Thumb unsupported-block fallback fixtures against a local ARM
  reference model, with
  two hundred forty two runtime blocks executed, seventy one total runtime
  fallbacks split into four initial lookup fallbacks, sixty four relookup
  fallbacks, and three unsupported-block fallbacks, basic data-processing native fallthrough chaining, remaining-cycle and invalid re-lookup fallback handoffs,
  ADDS/SUBS/RSBS/CMP/logical/test-op CPSR flag results and
  low-bit preservation checked, MRS CPSR/SPSR read results and remaining-cycle handoff, MSR CPSR flag remaining-cycle handoff,
  MSR CPSR control mode/banked-LR effects and remaining-cycle handoff, SPSR helper-write effects and remaining-cycle handoff, and native PSR
  accounting checked,
  multiply/accumulate, unsigned/signed long multiply, long multiply flag,
  long multiply accumulate, and long multiply accumulate flag results
  checked, carry-input data-processing, carry-input flag, logical flag, and
  extended shifted and register-shifted data-processing results checked,
  register-shifted flag/test and TEQ/CMN CPSR results checked,
  helper memory, helper load, load-to-PC, load-to-PC native target chaining, PC-write native target chaining, PC-write target native fallthrough chaining, PC-write Thumb fallback, PC-relative load, and register-offset load plus writeback store/load remaining-cycle handoffs, PC-relative store memory and remaining-cycle handoff,
  source-PC store value and remaining-cycle handoff, word-store,
  IO-window word-store helper observation, byte-store,
  register-offset byte-store, shifted-LSL/shifted-LSL-with-PC/shifted-LSR/shifted-LSR-with-PC/shifted-ASR/shifted-ASR-with-PC/shifted-ROR/shifted-ROR-with-PC register-offset byte-store, and RRX
  register-offset byte-store plus remaining-cycle handoffs, byte-store SMC/IRQ
  remaining-cycle handoff, and SMC/IRQ/HALT alert observations hashed,
  PC-register-offset word/byte store and shifted-PC word store remaining-cycle handoffs,
  register-offset, shifted register-offset load/store including shifted-LSL,
  shifted-LSR, shifted-ASR, and shifted-ROR with `Rm == PC` byte/word load/store,
  ordinary shifted-LSR, shifted-ASR, and shifted-ROR loads, subtract-offset,
  RRX load/store, and register-offset writeback
  load/store address/value observations checked,
  immediate, PC-relative, register-offset, and PC-register-offset halfword
  load/store remaining-cycle handoffs, PC-relative signed-byte load helper
  result/address/PC observation, PC-relative positive/negative halfword store
  helper address/value/PC observation, register-offset halfword store remaining-cycle handoff, writeback halfword
  signed/unsigned helper load results and store observations checked,
  halfword writeback store source/base ordering, store/load remaining-cycle handoff, and post-index load ordering
  checked,
  immediate memory writeback address/source ordering and store/load remaining-cycle handoff checked,
  block-memory writeback, decrement-before push, PC-loaded native target
  chaining, LDM-PC native target chaining, LDM-PC target native fallthrough
  chaining, LDM-PC SPSR restore/update behavior, ordered multi-word helper
  transfers, block-memory LDM, decrement-before PUSH, and SMC/IRQ remaining-cycle handoffs, and
  block-memory HALT update behavior checked,
  HLE division quotient/remainder/absolute-quotient helper results and DivArm remaining-cycle handoff checked,
  PC-source `pc+8`/`pc+12` operand, shifted-register flag behavior, and remaining-cycle handoff checked,
  SWP/SWPB helper old-value/read-PC/write-PC behavior checked, including
  SWP SMC/IRQ flush/interrupt observations, SWP HALT update behavior,
  byte read/write observations, and remaining-cycle handoff for `SWP` and
  `SWPB`,
  direct, indirect, conditional, SWI, PC-write, SPSR-restore, external and internal patched branch,
  production patch-site repatching with explicit icache flush,
  direct-branch target native-chain remaining-cycle execution, branch-target
  native fallthrough chaining, BX ARM-target native fallthrough chaining,
  patched-branch target native fallthrough chaining, and
  conditional-header target native fallthrough chaining, and
  BL target native fallthrough chaining, SWI target native fallthrough
  chaining, patchable SWI target native fallthrough chaining,
  PC-write target native fallthrough chaining, LDM-PC target native
  fallthrough chaining, and direct-branch, BL, BX ARM, BX Thumb, SWI, and
  PC-write ADD remaining-cycle execution exercised,
  scheduler/update cycle-refill, PC-change chaining, and frame-complete PC-change exit, HALT/idle-loop/Thumb-lookup observations hashed, with
  unsupported-block, Thumb unsupported-block, Thumb lookup-miss/invalid, helper load, load-to-PC, PC-relative load/store, PSR read/write, word store, register-offset load/store, PC-register-offset word/byte store, immediate/PC-relative/register-offset halfword load, PC-register-offset halfword load/store, register-offset halfword store, direct-branch, BL, BX ARM, BX Thumb, SWI, and PC-write ADD
  remaining-cycle lookup-misses, byte-store normal/alert, block-memory, and SWP alert remaining-cycle
  lookup-misses, and SWPB remaining-cycle lookup-miss fallbacks observed

The qemu-user harness now proves that partial native bytes emitted before a
block is marked unsupported are discarded, then the block routes through the
interpreter fallback without applying the partial native register write. The
lower-level standalone runtime test also emits the PC-relative signed-byte
load and positive/negative halfword store, PC-register-offset word/byte store, shifted-LSL/LSR/ASR/ROR
PC-register-offset word/byte load/store, and halfword load/store blocks plus
LSL/LSR/ASR/ROR/RRX register-offset load blocks and LSL/LSR/ASR/ROR/RRX
register-offset store blocks, then checks their helper address, PC, value,
and leftover-cycle handoff observations directly. It also proves that
unsupported byte and halfword load-to-PC forms (`LDRB`, `LDRH`, `LDRSB`,
and `LDRSH` with `Rd=PC`) stay rejected by the native emitter until a
separate interpreter-parity proof exists. PC-base writeback and post-index
load/store forms are likewise proven rejected for word/byte and halfword
memory classes. A standalone partial-unsupported block remains as the focused
`riscv_emit_block_finalize()` proof for the same discard-and-fallback contract.
A standalone patch-site
case rewrites the same `riscv_patch_unconditional_branch()` slot from one
native target block to another for the same guest branch target PC, flushes the
patched instruction range each time, and proves execution follows the updated
host target. The qemu-user harness and lower-level standalone runtime test both
prove that a store helper-returned `CPU_ALERT_SMC | CPU_ALERT_IRQ` is consumed
exactly once, runs the RAM cache flush and IRQ hooks, then continues to a
native block at the store fallthrough PC without falling back or leaking the
alert into the chained block.

Remaining first-phase gaps should stay narrow and evidence-driven:

- The qemu-user harness still has synthetic/fixture-backed state, memory, and default trace,
  and default frame paths outside the runtime-backed fixture commands. Synthetic
  paths stay labeled with `harness_mode=synthetic`; the `run runtime`,
  `compare`,
  `cont runtime <offset>`,
  `stepi runtime <count>`,
  `regs runtime`, `mem <addr> <len> runtime <offset>`,
  `mem <addr> <len> runtime-bytes`,
  `watchio <addr> <len> runtime <offset>`, `counters runtime`,
  `fallbacks runtime <offset>`,
  `sched runtime <offset>`,
  `stepb runtime <count>`, `tracepc runtime <count> <offset>`,
  `bp <pc> runtime`,
  `framehash runtime`, and `png <path> runtime` fixture paths are labeled
  `harness_mode=runtime_fixture`; the snapshot commands derive output from the
  runtime state/memory/scheduler/native-counter snapshot, `mem ... runtime`
  and `watchio ... runtime` record bounded windows of actual RV32IM helper
  memory/IO events, `mem ... runtime-bytes` records a bounded shadow-memory
  byte view of actual RV32IM helper writes, `stepi ... runtime` records a
  bounded guest-PC prefix from executed native block metadata,
  `stepb ... runtime` records the lookup prefix,
  `sched runtime ...` records a bounded window of actual
  scheduler/update boundary observations,
  `fallbacks runtime ...` records the runtime fallback kind/PC/result window,
  `tracepc runtime ...` records a bounded window of actual RV32IM lookup PCs,
  `sched runtime ...` includes the observed `update_gba()` return flags and
  PC-change target, flush count, IRQ-check count, and HALT state for each
  scheduler window entry, and `bp ... runtime` reports hit/miss against those
  lookup PCs. Full
  addressable emulator RAM/IO dumps and real emulator frame output are still
  not wired in.
- The `fallbacks runtime` command makes the current fallback bucket auditable;
  it does not by itself reduce fallback count or claim broader parity.
- Thumb instruction lowering remains deliberately unsupported; the harness
  compare path now proves Thumb lookup-miss/invalid fallback and unsupported
  Thumb block fallback only, and Thumb blocks must keep routing through
  fallback until a separate Thumb lowering milestone exists.
- Conditional ARM opcodes are still expected to enter through the frontend's
  conditional block header rewrite, not by accepting non-AL conditions in each
  low-level emitter API.
- Keep unsupported or architecturally risky PC/register combinations rejected
  until there is a matching interpreter-parity proof.
- Broader performance work, guest register caching, compressed RISC-V emission,
  and ESP32-specific tuning remain out of scope for the first correctness
  phase.

## Semi-Milestone Gate

Every validated RV32IM semi-milestone should be committed separately. A typical
gate is `git diff --check`, `make -C tests/rv32im compare`,
`make -C tests/rv32im test`, which includes a `cpu_threaded.c` compile guard
for `HAVE_DYNAREC` plus `RISCV_ARCH`, the `core-build` target for a top-level
unix RV32IM dynarec libretro build, and `make -C tests/rv32im clean` before
committing.
