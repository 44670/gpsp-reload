# Expert Consultation Report: gpSP ESP32-S31 RV32IM JIT Freezes After Start

> **Resolution update (2026-07-19): the reported Start freeze was a false
> diagnosis.** The game had entered a dialogue screen and was waiting for a
> subsequent A-button edge. The PC at `0x080008c6` is the game's ordinary
> per-frame VBlank wait and is not, by itself, evidence of a stuck CPU. The
> original investigation is retained below as a record of the evidence and of
> the observer effects that led to the incorrect conclusion.

## 0. Resolution and decisive evidence

The UART debugger and qemu-user runner now support deterministic synthetic
joypad input. Replaying the same input sequence through interpreter, RV32IM
JIT, and physical ESP32-S31 hardware produced the same final raw 240x160
RGB565 framebuffer hash:

```text
interpreter/qemu-riscv32: fnv1a=0x1c19e29d
RV32IM JIT/qemu-riscv32: fnv1a=0x1c19e29d
RV32IM JIT/ESP32-S31:    fnv1a=0x1c19e29d
```

The deterministic input schedule was:

```text
frame 600:  Start for 2 frames
frame 682:  Start for 2 frames
frame 1800: Start for 5 frames
frame 2250: A for 2 frames
frame 2350: A for 2 frames
frame 2450: B for 2 frames
stop at frame 2500
```

Before the later A presses, both interpreter and JIT displayed the same
dialogue box with a red continuation marker. After the A presses, both moved
to the next dialogue text. The final JIT qemu-user run also reported:

```text
native_blocks=843265
blocks_emitted=2658
generated_code_bytes=1678832
fallbacks=0
frame_hash=0x1c19e29d
```

The hardware replay ended at `PC=0x080008c6`, just like both host backends,
while still producing the exact expected framebuffer hash. This demonstrates
why the PC was misleading: the game spends most of each frame in that VBlank
poll and returns there again after processing the frame.

Likewise, observing `0x0300237c == 0x0004` at an arbitrary pause does not show
that bit 0 was never observed. The wait routine consumes/clears bit 0, the IRQ
sets it at VBlank, the main thread leaves the loop, processes a frame, and then
calls the same wait routine again. Frame-boundary sampling in qemu-user
observed repeated bit-0 transitions. The JIT runner's helper wrapper reported
zero writes because native fast IWRAM stores bypass that wrapper, not because
the callback store was absent.

Therefore the current evidence does not support a PSRAM cache-coherence bug,
a direct-chain bug, a missing VBlank store, or an IRQ-return bug for this
scenario. The original sections below describe the earlier hypothesis, but
their claim that the game never leaves `0x080008c6` is superseded by this
addendum.

**Date:** 2026-07-19  
**Project:** gpSP Game Boy Advance emulator  
**Target:** ESP32-S31-Korvo-1, native RV32IM JIT  
**Problem ROM:** referred to as `p.gba` in this report

## 1. Purpose of this report

I am seeking an expert review of a reproducible correctness failure in gpSP's native RV32IM dynamic recompiler on ESP32-S31 hardware.

The ROM boots, displays its copyright screen, and plays its title animation. USB controller input is recognized. After Start is pressed, however, the game becomes stuck in a Thumb loop waiting for a game-owned VBlank flag. The emulator, JIT, scheduler, and LCD transport all continue running. A hardware breakpoint proves that the ROM's VBlank callback is entered, but the main thread never observes the callback's completion flag.

The most useful expert help would be to identify the likely violation of the gpSP dynarec control-flow/state contract and propose a minimally perturbing way to locate it. The present breakpoint mechanism forces dispatcher mode and flushes the JIT caches, so it cannot directly observe the normal fast direct-chaining mode that is currently the leading suspect.

## 2. Executive summary

- The failure occurs only after the title screen transitions into the game.
- The stable guest PC is `0x080008c6`, in a Thumb loop that polls bit 0 of a halfword at `0x0300237c`.
- The observed halfword is `0x0004`: bit 2 is set, but the required bit 0 remains clear.
- GBA interrupt state is plausible and enabled: `IE=0x0005`, `IF=0`, `IME=1`, and the CPSR IRQ-disable bit is clear.
- A hardware breakpoint at `0x08000738` proves that the ROM's VBlank callback is entered.
- The callback tail at `0x080007cc..0x080007d4` is expected to set bit 0 at `0x0300237c`.
- A relevant callback subroutine call and return have correct link-register, stack, and return-PC values under forced-dispatch breakpoint instrumentation.
- Native JIT execution continues rapidly with zero fallback events. Typical frozen-state counters show tens of millions of native block executions.
- The video callback and LCD DMA continue submitting and completing frames without drops or timeouts. The framebuffer hash is constant because the guest display state is no longer changing.
- The best current hypothesis is a JIT fast-path error involving direct chaining, an internal translation gate, mapped-state validity, or callback/IRQ return state. A faulty instruction or IWRAM store in the callback remains possible.
- Display, USB input, ROM flash mapping, total CPU performance, and failure to generate a VBlank IRQ are not credible primary causes.

## 3. Hardware and firmware configuration

| Item | Configuration |
|---|---|
| Board | ESP32-S31-Korvo-1 |
| CPU ISA | 32-bit RISC-V with RV32IM used by the JIT |
| CPU clock | 320 MHz |
| Core configuration | Single core |
| Flash | 16 MiB, QIO |
| PSRAM | 16 MiB observed on this board |
| PSRAM mode | Octal, 250 MHz, XIP enabled |
| LCD | 800x480 parallel RGB panel |
| Input | USB XInput controller, VID/PID `3537:1028` |
| ESP-IDF target | `esp32s31` |
| Compiler optimization | Performance optimization enabled |
| Link-time optimization | Linker LTO enabled; global compile-time LTO is not enabled |
| Emulator mode | Native RV32IM dynarec; no JS JIT is involved |

Relevant flash partition layout:

```text
nvs,      data, nvs,     0x009000, 0x006000
phy_init, data, phy,     0x00f000, 0x001000
factory,  app,  factory, 0x010000, 0x180000
gamepak,  data, 0x40,    0x190000, 0xe70000
```

The firmware image tested during this investigation was built on 2026-07-19 and its application binary was 772,096 bytes.

## 4. ROM and storage path

The reconstructed raw ROM used for host-side analysis is:

```text
/tmp/gpsp-gamepak-read-mfLquF/p-reconstructed.gba
```

Properties:

```text
Raw size: 16,777,216 bytes
SHA-256: 31ed0497ac4562d0a71c81e41c7da8ef85706990eb672496f4351a23f2289839
```

The hardware ROM-container status reports:

```text
raw_image_bytes=12484608
stored_pages=380
page_size=32768
```

ROM reads use an O(1) block lookup into directly memory-mapped SPI flash. There is no PSRAM ROM cache in the active design. The game reaches its copyright and animated title screens, so gross ROM loading or mapping failure is unlikely.

## 5. Reproduction procedure

1. Flash the current ESP32-S31 firmware and `p.gba` game partition.
2. Boot the board.
3. Wait for the copyright screen and title animation.
4. Press Start on the connected USB controller.
5. The transition begins, but gameplay never starts. The visible image eventually remains unchanged.
6. Query CPU, JIT, interrupt, memory, and LCD status through the serial debug interface.

Expected result: the game leaves the title flow and starts gameplay.

Actual result: the main guest thread repeatedly executes the wait loop at `0x080008c6` while VBlank callbacks continue to be entered.

## 6. Frozen guest state

A fresh hardware capture in the frozen state produced:

```text
PC       = 0x080008c6
CPSR     = 0x6000003f
CPU_MODE = 0x10
halt     = 0

r0  = 0x00000000
r1  = 0x00000004
r2  = 0x03002360
r3  = 0x00000001
r13 = 0x03007e24
r14 = 0x080004bf
```

Interpretation:

- CPSR bit 5 is set, so the guest is in Thumb state.
- CPSR low mode bits are `0x1f`, which is System mode.
- The internal gpSP `CPU_MODE=0x10` is its `MODE_SYSTEM` enumeration, so this is not a mode mismatch.
- CPSR bit 7 is clear, so IRQs are not masked at the ARM CPU level.
- The CPU is not halted.

The relevant IWRAM object starts at `0x03002360`:

```text
03002360: 0000000015f30208000000002df30208
03002370: 0000000000000000f5b4000804000000
03002380: d1950000d19500000000000000000000
03002390: 0000280000000000a0003001000c0001
```

Therefore:

```text
*(uint16_t *)0x0300237c = 0x0004
```

Bit 2 is set and bit 0 is clear. The main thread is waiting for bit 0.

The corresponding GBA I/O snapshot was:

```text
04000000: 4031000028965800081e000000000396
04000200: 050000000f4000000100000000000000
```

Decoded relevant values:

```text
DISPSTAT = 0x9628
VCOUNT   = 0x0058
IE       = 0x0005
IF       = 0x0000
IME      = 0x0001
```

`IE=0x0005` enables VBlank and Timer 0 interrupts. `VCOUNT=0x58` is only the scanline at the instant of capture; the capture was not expected to occur during VBlank.

## 7. The exact Thumb wait loop

The guest code around the stable PC is:

```text
080008ac: push  {lr}
080008ae: ldr   r2, [pc, #36]    ; r2 = 0x03002360
080008b0: ldrh  r1, [r2, #28]
080008b2: ldr   r0, [pc, #36]    ; r0 = 0x0000fffe
080008b4: ands  r0, r1
080008b6: ldrh  r1, [r2, #28]
080008b8: strh  r0, [r2, #28]    ; clear bit 0
080008ba: ldrh  r1, [r2, #28]
080008bc: movs  r0, #1
080008be: ands  r0, r1
080008c0: cmp   r0, #0
080008c2: bne   0x080008d0
080008c4: movs  r3, #1
080008c6: ldrh  r1, [r2, #28]
080008c8: adds  r0, r3, #0
080008ca: ands  r0, r1
080008cc: cmp   r0, #0
080008ce: beq   0x080008c6
080008d0: pop   {r0}
080008d2: bx    r0
```

This function clears bit 0 and then waits until an interrupt-side callback sets it again. In the frozen state, the loop repeatedly reads `0x0004`, so it never exits.

## 8. The ROM VBlank callback

The VBlank callback begins at `0x08000738`. Its tail is expected to signal both the BIOS IRQ acknowledgement area and the game-owned completion flag:

```text
080007c2: ldr   r2, [pc, #56]    ; r2 = 0x03007ff8
080007c4: ldrh  r0, [r2]
080007c6: movs  r1, #1
080007c8: orrs  r0, r1
080007ca: strh  r0, [r2]
080007cc: ldr   r0, [pc, #16]    ; r0 = 0x03002360
080007ce: ldrh  r2, [r0, #28]
080007d0: ldrh  r3, [r0, #28]
080007d2: orrs  r1, r2
080007d4: strh  r1, [r0, #28]    ; expected to set bit 0 at 0x0300237c
080007d6: pop   {r4}
080007d8: pop   {r0}
080007da: bx    r0
```

If `0x080007d4` executes with correct operands and its store remains visible, the polling loop should observe bit 0 immediately after the IRQ returns. It does not.

This narrows the failure to one or more of the following:

1. normal fast JIT execution fails to reach the callback tail;
2. an instruction before or at the tail is miscompiled;
3. the halfword store targets the wrong address or writes the wrong value;
4. the store occurs but is overwritten before the interrupted wait loop resumes;
5. IRQ return or direct-chain state restoration resumes with invalid/stale state and prevents the expected memory observation.

## 9. Proof that VBlank IRQ dispatch reaches the callback

A hardware breakpoint at callback entry `0x08000738` hit on the next VBlank:

```text
breakpoint_pc = 0x08000738
next_pc       = 0x0800074c
cpsr          = 0x6000003f
cpu_mode      = 0x10
sp            = 0x03007e18
lr            = 0x0300291c
irq_sp        = 0x03007f74
irq_lr        = 0x00000030
irq_spsr      = 0x6000003f
```

This is strong evidence that:

- LCD timing advances far enough to generate GBA VBlank events;
- gpSP's interrupt scheduler recognizes an enabled VBlank interrupt;
- interrupt dispatch reaches the ROM callback at `0x08000738`.

Therefore, the primary problem is downstream of IRQ assertion and callback entry.

## 10. Callback trace evidence

A range trace over `0x08000738..0x080007db`, using forced dispatcher mode, observed callback execution and calls to several ROM routines. Representative block transitions were:

```text
0x08000738 -> 0x0800074c
0x0800074c -> 0x0800b474
0x08000758 -> 0x08295a08
0x08000782 -> 0x08001110    (conditional path)
0x0800078c -> 0x08000c80
0x08000790 -> 0x0828e6ac
0x0800079c -> 0x080334e0
0x080007a0 -> 0x0806f050
0x080007be -> 0x0800dc40    (conditional path)
```

Trace metadata identifies the containing translated block, not necessarily the exact internal guest entry point. For example, an internal entry at `0x080007c2` may still be reported as a block whose metadata start is `0x080007a0`. Consequently, failure to see a literal trace record beginning at `0x080007c2` is not proof that the instruction was skipped.

### 10.1 Relevant BL/return check

The Thumb BL at `0x080007be` calls `0x0800dc40`. A breakpoint at the callee captured:

```text
pc   = 0x0800dc40
next = 0x0800de12
lr   = 0x080007c3
sp   = 0x03007e00
```

A breakpoint at the callee epilogue captured:

```text
pc   = 0x0800de12
next = 0x080007c2
r0   = 0x080007c3
lr   = 0x080007c3
sp   = 0x03007e18
```

For this observed call, the Thumb return address, stack restoration, and `BX` destination are correct under breakpoint instrumentation. This evidence does **not** support claiming that generic Thumb BL/BLH lowering is the root cause.

## 11. Important observer effect in the debug mechanism

The current breakpoint and exact-trace implementation changes the execution mode before collecting evidence. In simplified form, it does this:

```c
riscv_set_runtime_debug_force_dispatch(true);
flush_dynarec_caches();

/* collect breakpoint or trace evidence */

riscv_set_runtime_debug_force_dispatch(false);
flush_dynarec_caches();
```

This has two consequences:

1. direct block chaining and its normal patched paths are not being exercised;
2. all translated code is regenerated around the observation window.

Thus, correct call/return behavior under a breakpoint is useful but conditional evidence. It cannot rule out a bug that exists only in normal fast dispatch, a patched direct branch, an internal translation gate, or state validity propagated across chained blocks.

This observer effect is currently the largest limitation of the investigation.

## 12. Initial post-Start exact block trace

The first 256 translated-block records after pressing Start showed:

- title-loop blocks alternating around `0x08006a8a` and `0x08006ab6`;
- the Start transition beginning at approximately record 79;
- transition and initialization calls through records 95-118;
- entry to the wait function at record 118;
- first arrival at the polling loop at record 119;
- repeated `0x080008c6 -> 0x080008c6` blocks for the rest of the trace.

The key transition was:

```text
record 118: 0x080004ba..0x080004be -> 0x080008ac
record 119: 0x080008ac..0x080008d4 -> 0x080008c6
record 120+: 0x080008c6..0x080008d4 -> 0x080008c6
```

This trace window is too short to prove that no interrupt occurs: 256 very small polling blocks consume only a fraction of one GBA frame. The later hardware breakpoint at `0x08000738` conclusively shows that VBlank callback entry does occur.

## 13. JIT and display status while visually frozen

A representative later JIT snapshot was:

```text
blocks_emitted   = 3322
native_blocks    = 28827204
native_ops       = 50351
rom_code_bytes   = 386400
ram_code_bytes   = 22088
fallbacks        = 0
fallback_events  = 0
initial_fallback = 0
relookup         = 0
unsupported      = 0
thumb_helpers    = 0
```

The native-block counter continues increasing. Reported emulation throughput while frozen is approximately 150-163 emulated frames per second, well above real-time GBA speed. This is a correctness failure, not CPU saturation.

The LCD path simultaneously reports:

- emulated/video frame counters continue increasing;
- submitted LCD frames equal completed LCD frames;
- no frame drops;
- no LCD transfer timeouts;
- stable framebuffer hash `0x7576cdc5`.

The stable hash reflects an unchanged guest framebuffer. It is not evidence that the physical LCD or DMA engine is stalled.

## 14. Why the main peripheral hypotheses are unlikely

### 14.1 USB input

The USB XInput controller enumerates correctly, and serial input-event logging observed the Start press. The program leaves the title loop and enters post-Start initialization before reaching `0x080008c6`. Therefore, the failure is not an unrecognized Start button.

### 14.2 Display transport

LCD submissions and completions continue without drops or timeouts. The emulator's video callback continues to run. The display is showing a stable guest-generated frame, so the display path is not the primary freeze.

### 14.3 Raw performance

The JIT executes at roughly 150-163 emulated FPS while stuck. More CPU, PSRAM bandwidth, or display acceleration would not make a missing game flag appear.

### 14.4 ROM mapping

The ROM executes through the copyright screen, title animation, Start transition, interrupt callback, and many distant subroutines. Direct flash mapping may still deserve general validation, but it does not fit the specific repeated VBlank-flag failure well.

### 14.5 Failure to raise VBlank IRQ

This has been ruled out by the hardware breakpoint at `0x08000738`.

## 15. QEMU-user differential harness evidence and limitations

The repository contains a statically linked RV32 qemu-user runner:

```text
tests/rv32im/out/rv32im_full_runner
```

Example command:

```sh
make -C tests/rv32im out/rv32im_full_runner

qemu-riscv32 -L tests/rv32im/full_sysroot \
  tests/rv32im/out/rv32im_full_runner \
  --rom /tmp/gpsp-gamepak-read-mfLquF/p-reconstructed.gba \
  --frames 900 \
  --backend rv32im \
  --start-frame 600 \
  --start-duration 2
```

Earlier investigation runs appeared to reproduce the hardware distinction:

- RV32IM JIT remained at `0x080008c6` after Start;
- the interpreter reached gameplay PCs such as `0x080a9dac` and `0x0800684a`;
- changing from JIT to interpreter soon after Start recovered progress;
- changing back to JIT later caused another freeze.

However, the latest rerun is not a trustworthy acceptance result. It reported:

```text
RV32IM:
frames=900
pc=0x080008c6
cpsr=0x6000003f
frame_hash=0x9aa58648
stable_frames=0
fallbacks=0

Interpreter:
frames=900
pc=0x0800684a
cpsr=0x0000003f
frame_hash=0xe888423f
stable_frames=18
```

The runner nevertheless emitted `PASS`, because its current pass condition checks only generic emulator health: valid video geometry, a non-BIOS PC, native execution, emitted blocks, and zero fallbacks. It does not prove that this particular game passed its Start transition.

The QEMU result should therefore be described as a historical reproduction signal, not a current deterministic gate. Input timing, title-state detection, and a game-specific progress oracle must be fixed before using it to accept or reject a JIT change.

## 16. Experiments already performed

| Experiment | Result | Interpretation |
|---|---|---|
| Verify USB events on serial | Start observed | Input path works |
| Inspect stable PC and registers | Stable at `0x080008c6` | Guest is polling a game flag |
| Inspect IWRAM flag | `0x0300237c = 0x0004` | Required bit 0 is absent |
| Inspect interrupt registers | `IE=5`, `IME=1`, IRQ enabled in CPSR | Interrupt configuration is plausible |
| Break at VBlank callback entry | Hit at `0x08000738` | VBlank IRQ reaches ROM callback |
| Trace callback range | Multiple callback blocks and subcalls observed | Callback is not wholly absent |
| Validate relevant BL call/return under breakpoint | LR, SP, and return PC correct | No evidence for this BL being the root cause |
| Monitor JIT fallbacks | Always zero | Failure is inside native execution/control flow, not hidden interpreter fallback |
| Monitor LCD DMA | Frames complete, no timeouts | Not a display-transfer stall |
| Disable individual RV32 fast paths in earlier QEMU runs | No single option fixed the failure | No isolated optimization has yet been proven responsible |
| Toggle broad JIT debug-category masks | Conflicting results | Likely affected timing/control mode; not a reliable localization |
| Isolate the Thumb BLH debug bit | Same output as baseline in a later run | Earlier BLH correlation is unconfirmed |

The individually disabled paths in earlier host experiments included mapped ALU lowering, fast reads, fast stores, entry setup, state helpers, validated entry, and indirect lookup caching. These experiments predated a reliable game-progress oracle, so they should be repeated only after the harness can deterministically identify success.

## 17. Current ranked hypotheses

### 17.1 Fast direct chaining or translation-gate state contract violation

This is currently the leading hypothesis.

Reasons:

- native execution continues with zero fallbacks;
- IRQ entry is correct;
- a relevant nested call/return is correct when the debugger forces dispatcher mode;
- existing instrumentation disables the exact direct-chain mode that remains unobserved;
- the callback contains internal entry/return points, including the return to `0x080007c2`, that may use a translation gate inside a larger translated block;
- mapped guest-register validity or dirty state may differ between normal direct chaining, helper returns, IRQ entry, and dispatcher lookup.

Specific possibilities include:

- an external patched branch enters a block with an invalid mapped-state assumption;
- an internal translation gate points at the wrong generated offset;
- a chain target omits a required mapped-register reload;
- cycle-exit handling branches to a continuation with stale state;
- IRQ return resumes a directly chained polling block without restoring the frontend/backend state contract.

### 17.2 Miscompiled instruction or halfword memory access in the callback tail

The callback may reach the tail but compute or store an incorrect value. The highest-value instruction to observe is:

```text
0x080007d4: strh r1, [r0, #28]
```

Required facts are the exact `r0`, `r1`, address, old halfword, and new halfword at this instruction during normal fast dispatch.

### 17.3 Control-flow failure in one of the callback's nested calls

The callback makes several calls before setting the flag. One observed call at `0x080007be` returns correctly under forced dispatch, but another call or its normal direct-patched return could fail only in fast mode.

### 17.4 Scheduler/cycle-exit or IRQ-return boundary error

`riscv_jit_run_block()` returns to scheduling logic when the signed cycle count is exhausted. `update_gba()` advances hardware and raises events. A mismatch in cycle-exit patching or state synchronization could allow IRQ entry but break callback completion or resume.

### 17.5 A later legitimate write clears the flag before the main loop observes it

This is possible but less likely. In ordinary sequential guest execution, the interrupted wait loop should resume immediately after IRQ handling and observe the flag. A write watchpoint is needed to establish the complete write history of `0x0300237c`.

## 18. Recommended next experiments

The next instrumentation should be passive: it must not force dispatcher mode, flush translation caches, or alter direct-chain patching.

### 18.1 Add fixed-PC passive probes

Add very small native probes or counters at these exact guest PCs:

| Guest PC | Observation |
|---|---|
| `0x08000738` | Count VBlank callback entries |
| `0x080007be` | Record call target, LR, and SP |
| `0x080007c2` | Count returns from `0x0800dc40` |
| `0x080007d4` | Record store address, value, and previous halfword |
| `0x080007da` | Record callback return target and SP |
| `0x080008c6` | Record the first few loaded halfword values after each IRQ |

The probe should write to a preallocated ring buffer and defer serial output until execution is paused. Serial logging in the hot path would perturb timing too much.

### 18.2 Add a passive write history for `0x0300237c`

For every native or helper write that overlaps `0x0300237c`, record:

```text
guest_pc
write_address
write_width
old_value
new_value
current_cpsr
current_cycle_count
source_block_start
```

This will distinguish “the callback store never executes” from “the store executes and is later cleared.”

### 18.3 Log chain metadata without disabling chaining

For a bounded number of events around the callback, record:

```text
source guest block
source exit guest PC
chain target guest PC
generated-code target address
direct vs dispatcher transition
mapped-register valid mask
mapped-register dirty mask
cycle counter before and after exit
```

This is particularly important for the return/internal entry at `0x080007c2` and the eventual IRQ return to `0x080008c6`.

### 18.4 Validate the internal translation map for `0x080007c2`

Dump the translated block containing `0x080007a0..0x080007da` and verify:

- the frontend's guest-PC-to-host-offset table;
- the generated address selected for guest PC `0x080007c2`;
- any prologue or state-assumption differences between block start and internal gate;
- whether a return to `0x080007c2` bypasses required setup;
- all patched relative branch ranges and targets.

### 18.5 Selectively deoptimize only the callback

Without switching the whole emulator to the interpreter, test one change at a time:

1. force dispatcher lookup for chains entering `0x08000738..0x080007da`;
2. prohibit internal-entry chaining specifically to `0x080007c2`;
3. force a full mapped-state spill/reload on entry to and exit from the callback;
4. lower only the callback's halfword load/store instructions through known-correct helpers;
5. disable direct patching only for IRQ callback returns.

If one narrowly scoped deoptimization restores gameplay, it will localize the broken contract far better than a global debug mask.

### 18.6 Build a deterministic QEMU comparison gate

The host runner should stop using generic `PASS` for this ROM. A useful gate would:

1. reach a deterministic title-state marker;
2. inject Start based on guest state, not an assumed frame number;
3. run interpreter and RV32IM from the same serialized state immediately before Start or before one VBlank;
4. stop after the callback returns or after a bounded timeout;
5. compare CPU registers, `0x03002360..0x0300239f`, IRQ state, frame hash, and trace PCs;
6. fail if PC remains at `0x080008c6` or bit 0 at `0x0300237c` never appears;
7. require native-block and generated-byte counters to be nonzero in JIT mode;
8. require zero unexplained fallbacks.

An even tighter differential test would restore one shared state immediately before VBlank, execute exactly through the callback and IRQ return in both modes, and compare after every translated block.

## 19. Questions for the expert reviewer

1. In gpSP's `cpu_threaded.c` contract, what state must an internal translation gate restore that a block-start entry normally establishes?
2. Can a direct patched chain legally target an internal Thumb address such as `0x080007c2`, or must it re-enter through a canonical block prologue?
3. Which guest/mapped state must be spilled before `update_gba()`, IRQ dispatch, helper calls, and indirect branch lookup, and reloaded afterward?
4. Is it possible for a cycle-exit side path to retain the correct PC while losing a dirty mapped register or condition flag required by the callback?
5. What is the safest design for passive PC/store probes in generated RV32IM code without changing register allocation or chain topology?
6. Would you first inspect the generated block containing `0x080007c2`, the native `STRH` lowering at `0x080007d4`, or the IRQ-return chain to `0x080008c6`?
7. Which invariants from the mature MIPS backend should be asserted at every RV32IM chain boundary?
8. Is there a known gpSP frontend assumption that translated blocks entered at internal branch targets must have independently materialized cycle or flag state?

## 20. Relevant source files

| File | Role |
|---|---|
| `cpu_threaded.c` | Dynarec frontend contract, block scanning, exits, translation gates, and patching |
| `riscv/riscv_emit.h` | RV32IM instruction lowering and branch emission |
| `riscv/riscv_runtime.c` | JIT runtime, entry/dispatch, cache handling, helper boundaries, and debug controls |
| `cpu.cc` | Interpreter semantics and CPU-state truth source |
| `gba_memory.c` | Guest memory behavior and I/O access truth source |
| `main.c` | `update_gba()` scheduler and event progression |
| `esp32s31/uart_debug.c` | Hardware serial debugger and trace/breakpoint interface |
| `mips/mips_emit.h` | Mature native backend structural reference |
| `mips/mips_stub.S` | Mature backend entry/exit and scheduler-flow reference |
| `tests/rv32im/rv32im_full_runner.c` | qemu-user full-emulator test runner |

The MIPS backend should be used as the structural reference for state ownership, helper-call boundaries, cycle exits, indirect branches, and cache synchronization. It should not be translated mechanically because RISC-V has different branch, immediate, and ABI rules.

## 21. Relevant uncommitted investigation changes

The working tree contains ongoing RV32IM fixes and debug instrumentation beyond baseline commit:

```text
8b06b4daa9b6ede65f7c408a2804fdf48cfcd80c
```

Changes relevant to the investigation include:

- using an assembler-resolved return label instead of a fixed AUIPC-relative assumption, to tolerate RVC relaxation;
- restricting indirect lookup-cache use to entries with valid mapped-register state;
- reloading invalid mapped registers on external chain entry;
- correcting an ARM PC-relative folded ADD/SUB case;
- making the Thumb BL frontend consume the scan-pass block-exit slot correctly;
- changes to terminal branch/cycle-exit paths;
- debug force-dispatch, category masks, branch probes, and host-runner instrumentation.

These changes allowed the ROM to reach and animate the title screen, but they have not fixed the post-Start wait-loop failure. Because the tree is not a clean release revision, an expert reproducing the issue should review the exact diff in addition to this report.

## 22. Minimum proof of a correct fix

A fix should not be accepted solely because the title screen moves or because the generic runner prints `PASS`. The minimum evidence should be:

```text
1. USB or deterministic host input reaches the Start transition.
2. VBlank callback entry count is greater than zero.
3. The store at 0x080007d4 writes bit 0 to 0x0300237c.
4. The main loop observes bit 0 and leaves 0x080008c6.
5. Gameplay continues for a bounded period with changing game-state/frame evidence.
6. native_blocks > 0.
7. generated_code_bytes > 0.
8. fallbacks = 0, unless a specific temporary diagnostic fallback is declared.
9. The result also works with normal direct chaining enabled and without cache-flushing breakpoints.
10. Interpreter and RV32IM snapshots match at a deterministic post-Start checkpoint.
```

## 23. Concise diagnosis as of this report

The board is not simply frozen. gpSP is running, the native RV32IM JIT is executing quickly, display transfers continue, and VBlank IRQ dispatch reaches the ROM callback. The game is blocked because the main thread never observes the VBlank-completion bit that the callback should set at `0x0300237c`.

The unresolved question is whether normal fast JIT execution fails to reach `0x080007d4`, misexecutes its `STRH`, subsequently overwrites the flag, or returns through a direct-chain/internal-gate path with invalid state. Existing breakpoints cannot answer this because they force dispatcher mode and flush the translation caches. A passive trace of the callback tail, its IWRAM store, and the normal chain/IRQ-return path is the highest-value next step.

## 24. Source-code appendix

This appendix contains the relevant implementation excerpts because the reviewer may not have access to the repository. They are taken from the current, uncommitted investigation worktree based on commit `8b06b4daa9b6ede65f7c408a2804fdf48cfcd80c`. Treat them as annotated source excerpts rather than standalone compilable files: unrelated cases, declarations, profiling counters, and preprocessor variants are sometimes removed; stringified inline assembly is rendered as ordinary assembly; and intentional gaps are marked with omission comments. Function names, state mappings, branch decisions, and code paths discussed in the report reflect the current worktree.

The excerpts are not intended to form one compilable translation unit. gpSP's standard typedefs apply: `u8`, `u16`, and `u32` are unsigned 8-, 16-, and 32-bit integers.

### 24.1 Guest CPU state and block metadata

From `cpu.h`:

```c
/* System mode and user mode share the same register bank in gpSP. */
typedef u32 cpu_mode_type;

#define MODE_USER         0x00
#define MODE_SYSTEM       0x10
#define MODE_IRQ          0x11
#define MODE_FIQ          0x12
#define MODE_SUPERVISOR   0x13
#define MODE_ABORT        0x14
#define MODE_UNDEFINED    0x15
#define MODE_INVALID      0x16

#define REG_MODE(m) (reg_mode[(m) & 0xF])
#define REG_SPSR(m) (spsr[(m) & 0xF])

#define CPU_ACTIVE          0
#define CPU_HALT            1
#define CPU_STOP            2
#define CPU_DMA             3

typedef u8 cpu_alert_type;

#define CPU_ALERT_NONE         0
#define CPU_ALERT_HALT   (1 << 0)
#define CPU_ALERT_SMC    (1 << 1)
#define CPU_ALERT_IRQ    (1 << 2)

typedef enum
{
  REG_SP            = 13,
  REG_LR            = 14,
  REG_PC            = 15,
  REG_CPSR          = 16,
  CPU_MODE          = 17,
  CPU_HALT_STATE    = 18,
  REG_ARCH_COUNT    = 19,
  REG_BUS_VALUE     = 19,
  REG_N_FLAG        = 20,
  REG_Z_FLAG        = 21,
  REG_C_FLAG        = 22,
  REG_V_FLAG        = 23,
  REG_SLEEP_CYCLES  = 24,
  OAM_UPDATED       = 25,
  REG_SAVE          = 26,
  REG_SAVE2         = 27,
  REG_SAVE3         = 28,
  REG_SAVE4         = 29,
  REG_SAVE5         = 30,
  REG_SAVE6         = 31,
  REG_USERDEF       = 32,
  REG_MAX           = 64
} ext_reg_numbers;
```

From `riscv/riscv_emit.h` and `riscv/riscv_runtime.c`:

```c
#define RISCV_BLOCK_META_BYTES 8
#define block_prologue_size RISCV_BLOCK_META_BYTES
#define RISCV_BRANCH_PATCH_BYTES 8
#define RISCV_BRANCH_PATCH_SHORT_BYTES 4

typedef struct riscv_jit_block_meta
{
  u32 start_pc;
  u16 end_delta_thumb;
  u16 chain_units : 10;
  u16 flags : 5;
  u16 reserved : 1;
} riscv_jit_block_meta;

enum
{
  RISCV_STACK_HELPER_READ32 = 8,
  RISCV_STACK_HELPER_STORE32 = 12,
  RISCV_STACK_HELPER_READ8 = 16,
  RISCV_STACK_HELPER_STORE8 = 20,
  RISCV_STACK_HELPER_READ16 = 24,
  RISCV_STACK_HELPER_BLOCK_STORE32 = 28,
  RISCV_STACK_HELPER_BLOCK_READ32 = 32,
  RISCV_STACK_HELPER_STORE16 = 36,
  RISCV_STACK_HELPER_READ8S = 40,
  RISCV_STACK_HELPER_READ16S = 44,
  RISCV_STACK_HELPER_EXECUTE_SPSR_RESTORE = 48,
  RISCV_STACK_HELPER_STORE_SPSR = 52,
  RISCV_STACK_HELPER_STORE_CPSR = 56,
  RISCV_STACK_HELPER_EXECUTE_SWI_ARM = 60,
  RISCV_STACK_HELPER_EXECUTE_SWI_THUMB = 64,
  RISCV_STACK_HELPER_HLE_DIV = 68,
  RISCV_STACK_HELPER_SWAP_U8 = 72,
  RISCV_STACK_HELPER_SWAP_U32 = 76,
  RISCV_STACK_HELPER_ARM_BLOCK_MEMORY = 80,
  RISCV_STACK_HELPER_THUMB_EXECUTE = 84,
  RISCV_STACK_JIT_LOOP_RETURN = 88,
  RISCV_STACK_JIT_RUN_BLOCK = 92,
  RISCV_STACK_CYCLES_PTR = 96,
  RISCV_STACK_CYCLES_VALUE = 100,
  RISCV_INITIAL_ROM_WATERMARK = 16,
  RISCV_BLOCK_NATIVE_SUPPORTED = 1u,
  RISCV_BLOCK_PC_WRITTEN = 2u,
  RISCV_BLOCK_PC_BASE_EMITTED = 4u,
  RISCV_BLOCK_TERMINAL_EMITTED = 8u,
  RISCV_BLOCK_NO_FALLTHROUGH = 16u
};

#define RISCV_BLOCK_CHAIN_UNIT_BYTES 4u
#define RISCV_BLOCK_CHAIN_MAX_BYTES \
  ((1u << 10) * RISCV_BLOCK_CHAIN_UNIT_BYTES - 4u)

#define RISCV_INVALID_BLOCK_ENTRY ((u8 *)(uintptr_t)~(uintptr_t)0)
```

### 24.2 Fixed guest-to-host mapping and synchronization

The RV32 backend keeps guest `r0..r14` and packed NZCV live in fixed host registers while generated blocks chain. The C state array is authoritative at helper/scheduler boundaries only after dirty mappings have been flushed.

From `riscv/riscv_runtime.c`:

```c
static u32 riscv_mapped_dirty_mask;
static u32 riscv_mapped_valid_mask;

#define RISCV_MAPPED_REG_COUNT 16u
#define RISCV_MAPPED_GPR_COUNT 15u
#define RISCV_MAPPED_NZCV_SLOT 15u
#define RISCV_MAPPED_REGS_MASK ((1u << RISCV_MAPPED_REG_COUNT) - 1u)
#define RISCV_MAPPED_NZCV_MASK (1u << RISCV_MAPPED_NZCV_SLOT)
#define RISCV_MAPPED_CALLER_SAVED_MASK ((1u << 5) - 1u)

static const riscv_reg_number riscv_mapped_host_regs[RISCV_MAPPED_REG_COUNT] =
{
  riscv_reg_a3,  /* guest r0 */
  riscv_reg_a4,  /* guest r1 */
  riscv_reg_a5,  /* guest r2 */
  riscv_reg_a6,  /* guest r3 */
  riscv_reg_a7,  /* guest r4 */
  riscv_reg_s1,  /* guest r5 */
  riscv_reg_s2,  /* guest r6 */
  riscv_reg_s3,  /* guest r7 */
  riscv_reg_s4,  /* guest r8 */
  riscv_reg_s5,  /* guest r9 */
  riscv_reg_s6,  /* guest r10 */
  riscv_reg_s7,  /* guest r11 */
  riscv_reg_s8,  /* guest r12 */
  riscv_reg_s9,  /* guest r13 / SP */
  riscv_reg_s10, /* guest r14 / LR */
  riscv_reg_s11  /* packed NZCV: N=8, Z=4, C=2, V=1 */
};

static const u8 riscv_mapped_state_regs[RISCV_MAPPED_REG_COUNT] =
{
  0, 1, 2, 3, 4, 5, 6, 7,
  8, 9, 10, 11, 12, REG_SP, REG_LR, REG_CPSR
};

static void riscv_emit_mapped_regs_reload_mask(u8 **ptr_ref, u32 reload_mask)
{
  u8 *translation_ptr = *ptr_ref;
  u32 i;

  reload_mask &= RISCV_MAPPED_REGS_MASK;
  if (!reload_mask)
    return;

  for (i = 0; i < RISCV_MAPPED_GPR_COUNT; i++)
  {
    if (reload_mask & (1u << i))
    {
      riscv_emit_lw(riscv_mapped_host_regs[i], riscv_reg_s0,
                    (u32)riscv_mapped_state_regs[i] * 4u);
    }
  }
  *ptr_ref = translation_ptr;
  if (reload_mask & RISCV_MAPPED_NZCV_MASK)
  {
    riscv_emit_lw(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_reg_s0, REG_CPSR * 4u);
    *ptr_ref = translation_ptr;
    riscv_emit_cpsr_pack_nzcv(
      ptr_ref, riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
      riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT]);
  }

  riscv_mapped_valid_mask |= reload_mask;
  riscv_mapped_dirty_mask &= ~reload_mask;
}

static void riscv_emit_mapped_regs_flush_mask(u8 **ptr_ref, u32 dirty_mask)
{
  u8 *translation_ptr;
  u32 i;

  dirty_mask &= riscv_mapped_valid_mask;
  if (!dirty_mask)
    return;

  translation_ptr = *ptr_ref;
  for (i = 0; i < RISCV_MAPPED_GPR_COUNT; i++)
  {
    if (dirty_mask & (1u << i))
    {
      riscv_emit_sw(riscv_mapped_host_regs[i], riscv_reg_s0,
                    (u32)riscv_mapped_state_regs[i] * 4u);
    }
  }
  *ptr_ref = translation_ptr;
  if (dirty_mask & RISCV_MAPPED_NZCV_MASK)
    riscv_emit_live_nzcv_flush(ptr_ref);
  riscv_mapped_dirty_mask &= ~dirty_mask;
}

static void riscv_emit_mapped_regs_flush_dirty(u8 **ptr_ref)
{
  riscv_emit_mapped_regs_flush_mask(ptr_ref, riscv_mapped_dirty_mask);
  riscv_mapped_dirty_mask = 0;
}

static void riscv_invalidate_mapped_regs(void)
{
  riscv_mapped_valid_mask = 0;
  riscv_mapped_dirty_mask = 0;
}
```

The special `REG_PC` store flushes all dirty mappings. Ordinary mapped register stores update only the live host register and the masks:

```c
static void riscv_emit_arm_reg_store(u8 **ptr, u32 reg_index,
                                     riscv_reg_number rs)
{
  riscv_reg_number mapped_reg;
  u32 dirty_mask;
  u8 *translation_ptr = *ptr;

  if (reg_index == REG_PC)
  {
    riscv_emit_sw(rs, riscv_reg_s0, reg_index * 4u);
    *ptr = translation_ptr;
    riscv_emit_mapped_regs_flush_dirty(ptr);
    return;
  }

  /* REG_CPSR handling omitted here; it updates the packed NZCV mapping. */

  if (riscv_arm_reg_mapped(reg_index, &mapped_reg, &dirty_mask))
  {
    *ptr = translation_ptr;
    riscv_emit_reg_move(ptr, mapped_reg, rs);
    riscv_mapped_valid_mask |= dirty_mask;
    riscv_mapped_dirty_mask |= dirty_mask;
    return;
  }

  riscv_emit_sw(rs, riscv_reg_s0, reg_index * 4u);
  *ptr = translation_ptr;
}
```

### 24.3 Outer native-entry loop and generated-block return tail

The important invariant is that the outer entry loop reloads every mapped guest register before calling a block returned by the C dispatcher. A direct chain or indirect-cache hit bypasses this reload.

Condensed from the RV32 assembly in `riscv/riscv_runtime.c`:

```asm
riscv_enter_jit:
  addi sp, sp, -176
  sw   ra, 172(sp)
  sw   s0, 168(sp)
  sw   s1, 164(sp)
  sw   s2, 160(sp)
  sw   s3, 156(sp)
  sw   s4, 152(sp)
  sw   s5, 148(sp)
  sw   s6, 144(sp)
  sw   s7, 140(sp)
  sw   s8, 136(sp)
  sw   s9, 132(sp)
  sw   s10, 128(sp)
  sw   s11, 124(sp)
  mv   s0, a1                 # s0 = &reg[0]
  sw   a2, 92(sp)             # C riscv_jit_run_block address

  # Helper table setup is omitted.
  lw   t0, 76(a5)
  sw   t0, 96(sp)             # &riscv_cycles_remaining

.Lentry_loop:
  beqz a0, .Lexit
  lw   t0, 96(sp)
  lw   t0, 0(t0)
  sw   t0, 100(sp)            # current cycle value for generated code

  lw   a3, 0(s0)              # guest r0
  lw   a4, 4(s0)              # guest r1
  lw   a5, 8(s0)              # guest r2
  lw   a6, 12(s0)             # guest r3
  lw   a7, 16(s0)             # guest r4
  lw   s1, 20(s0)             # guest r5
  lw   s2, 24(s0)             # guest r6
  lw   s3, 28(s0)             # guest r7
  lw   s4, 32(s0)             # guest r8
  lw   s5, 36(s0)             # guest r9
  lw   s6, 40(s0)             # guest r10
  lw   s7, 44(s0)             # guest r11
  lw   s8, 48(s0)             # guest r12
  lw   s9, 52(s0)             # guest SP
  lw   s10, 56(s0)            # guest LR
  lw   s11, 64(s0)            # CPSR
  srli s11, s11, 28
  andi s11, s11, 15           # packed NZCV

  # The assembler may relax surrounding code to RVC, so this is label based.
  lla  t0, .Lriscv_jit_loop_return
  sw   t0, 88(sp)
  jalr ra, a0, 0              # enter generated code

.Lriscv_jit_loop_return:
  j .Lentry_loop

.Lexit:
  # Restore s0-s11, ra, and sp; then return to the emulator main loop.
```

The normal generated-block tail publishes cycles and jumps to the C dispatcher:

```asm
riscv_jit_run_block_tail:
  lw   t4, 100(sp)
  lw   t5, 96(sp)
  sw   t4, 0(t5)              # riscv_cycles_remaining = generated value
  lw   ra, 88(sp)             # outer entry-loop return label
  lw   t0, 92(sp)             # riscv_jit_run_block
  jalr zero, t0, 0
```

The indirect-cache tail can jump directly into another generated block. It first rejects cycle exhaustion, CPU alerts, halt state, and the idle-loop target:

```asm
riscv_jit_indirect_lookup_tail:
  lw   t4, 100(sp)
  lw   t5, 96(sp)
  sw   t4, 0(t5)
  bge  zero, t4, .Lslow

  # key = aligned guest PC | Thumb bit
  lw   t0, 64(s0)             # CPSR
  srli t0, t0, 5
  andi t0, t0, 1
  slli t1, t0, 1
  addi t1, t1, -4
  lw   t2, 60(s0)             # REG_PC
  and  t2, t2, t1
  or   t2, t2, t0

  # Hash-table generation/key checks are omitted.
  # A successful lookup places the generated entry in t6.

  lla  t0, riscv_cpu_alert
  lbu  t0, 0(t0)
  bnez t0, .Lslow
  lw   t0, 72(s0)             # CPU_HALT_STATE
  bnez t0, .Lslow
  lla  t0, idle_loop_target_pc
  lw   t0, 0(t0)
  lw   t1, 60(s0)             # REG_PC
  beq  t1, t0, .Lslow

  jalr zero, t6, 0            # bypasses C dispatcher and outer reload

.Lslow:
  lw   ra, 88(sp)
  lw   t0, 92(sp)
  jalr zero, t0, 0            # riscv_jit_run_block(meta)
```

The emitter chooses the indirect-cache tail only when every fixed mapping is marked valid:

```c
static void riscv_emit_jit_run_block_tail_jump(
  u8 **ptr, const riscv_jit_block_meta *meta)
{
  uintptr_t tail = (uintptr_t)riscv_jit_run_block_tail;

  /* A cache hit bypasses the outer dispatcher reload. */
  if (meta && (meta->flags & RISCV_BLOCK_PC_WRITTEN) &&
      (riscv_mapped_valid_mask & RISCV_MAPPED_REGS_MASK) ==
        RISCV_MAPPED_REGS_MASK)
  {
    tail = (uintptr_t)riscv_jit_indirect_lookup_tail;
  }

  riscv_emit_li(ptr, riscv_reg_t0, (u32)tail);
  u8 *translation_ptr = *ptr;
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_t0, 0);
  *ptr = translation_ptr;
}
```

### 24.4 C dispatcher, scheduler boundary, and lookup

From `riscv/riscv_runtime.c`:

```c
static void riscv_current_lookup_state(u32 *pc, u32 *thumb)
{
  *thumb = (reg[REG_CPSR] & 0x20u) != 0;
  *pc = *thumb ? (reg[REG_PC] & ~1u) : (reg[REG_PC] & ~0x03u);
}

static u8 *riscv_lookup_current_block(u32 *pc_out, u32 *thumb_out)
{
  u32 pc;
  u32 thumb;

  riscv_current_lookup_state(&pc, &thumb);
  if (pc_out)
    *pc_out = pc;
  if (thumb_out)
    *thumb_out = thumb;

  if (thumb)
    return block_lookup_address_thumb(pc);
  return block_lookup_address_arm(pc);
}

static u8 *riscv_lookup_or_fallback(riscv_control_lookup_kind kind)
{
  u8 *entry;
  u32 pc;
  u32 thumb;

  if (riscv_cycles_remaining <= 0)
    return NULL;

  entry = riscv_lookup_current_block(&pc, &thumb);
  if (!entry || entry == RISCV_INVALID_BLOCK_ENTRY)
  {
    riscv_interpreter_fallbacks++;
    riscv_relookup_fallbacks++;
    riscv_run_interpreter_remainder();
    return NULL;
  }

  if (kind == RISCV_CONTROL_LOOKUP_INDIRECT)
  {
    u32 key = pc | (thumb ? 1u : 0u);
    u32 index = (key >> 1) & RISCV_INDIRECT_LOOKUP_CACHE_MASK;
    riscv_indirect_lookup_cache_entry *cached =
      &riscv_indirect_lookup_cache.entries[index];

    cached->key = key;
    cached->generation = riscv_indirect_lookup_cache.generation;
    cached->entry = (u32)(uintptr_t)entry;
  }

  return entry;
}

static u8 *riscv_jit_run_block(const riscv_jit_block_meta *meta)
{
  u32 update_ret;
  cpu_alert_type alert = CPU_ALERT_NONE;

  riscv_blocks_executed++;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
  {
    /* Record unsupported fallback and run the interpreter remainder. */
    riscv_run_interpreter_remainder();
    return NULL;
  }

  riscv_note_runtime_block_execute(meta->start_pc,
                                   riscv_block_meta_end_pc(meta),
                                   riscv_block_meta_thumb(meta));

  if (riscv_cpu_alert != CPU_ALERT_NONE)
    alert = riscv_handle_cpu_alert();

  if (reg[REG_PC] == idle_loop_target_pc && riscv_cycles_remaining > 0)
    riscv_cycles_remaining = 0;

  if ((alert & CPU_ALERT_HALT) || reg[CPU_HALT_STATE] != CPU_ACTIVE ||
      riscv_cycles_remaining <= 0)
  {
    update_ret = update_gba(riscv_cycles_remaining);
    if (completed_frame(update_ret))
    {
      riscv_cycles_remaining = 0;
      return NULL;
    }

    riscv_cycles_remaining = (s32)cycles_to_run(update_ret);
  }

  return riscv_lookup_or_fallback(
    (meta->flags & RISCV_BLOCK_PC_WRITTEN) ?
      RISCV_CONTROL_LOOKUP_INDIRECT : RISCV_CONTROL_LOOKUP_FALLTHROUGH);
}
```

Initial execution enters the same outer JIT loop after looking up the current guest PC:

```c
u32 execute_arm_translate_internal(u32 cycles, void *regptr)
{
  u8 *entry_data;
  u32 pc;
  u32 thumb;

  riscv_cycles_remaining = (s32)cycles;
  riscv_cpu_alert = CPU_ALERT_NONE;
  if (cycles == 0)
    return 0;

  entry_data = riscv_lookup_current_block(&pc, &thumb);
  if (!entry_data || entry_data == RISCV_INVALID_BLOCK_ENTRY)
  {
    riscv_interpreter_fallbacks++;
    riscv_initial_lookup_fallbacks++;
    execute_arm(cycles);
    riscv_cycles_remaining = 0;
    return 0;
  }

  (void)riscv_enter_jit(entry_data, regptr,
                        (void *)(uintptr_t)riscv_jit_run_block,
                        (void *)(uintptr_t)riscv_thumb_execute,
                        (void *)(uintptr_t)riscv_thumb_execute_bl_pair,
                        riscv_helper_table);
  return 0;
}
```

### 24.5 Frontend block lookup, internal targets, and finalization

The ROM hash returns a pointer immediately after the eight-byte metadata header. A lookup is keyed by aligned guest PC plus the Thumb bit, so a runtime lookup for a different guest entry PC normally creates or finds a distinct hash entry.

Condensed from `cpu_threaded.c`:

```c
#define block_lookup_address_pc_arm() \
  u32 thumb = 0;                      \
  pc &= ~0x03

#define block_lookup_address_pc_thumb() \
  u32 thumb = 1;                        \
  pc &= ~0x01

#define block_lookup_translate_builder(type)                              \
u8 function_cc *block_lookup_translate_##type(u32 pc)                     \
{                                                                         \
  u8 pcregion = (pc >> 24);                                               \
  block_lookup_address_pc_##type();                                       \
                                                                          \
  switch(pcregion)                                                        \
  {                                                                       \
    /* RAM lookup/tag handling omitted. */                                 \
                                                                          \
    case 0x0:                                                             \
    case 0x8 ... 0xD:                                                     \
    {                                                                     \
      u32 key = pc | thumb;                                               \
      u32 hash_target =                                                   \
        ((key * 2654435761U) >> (32 - ROM_BRANCH_HASH_BITS)) &            \
        (ROM_BRANCH_HASH_SIZE - 1);                                       \
      hashhdr_type *bhdr;                                                 \
      u32 blk_offset = rom_branch_hash[hash_target];                      \
      u32 *blk_offset_addr = &rom_branch_hash[hash_target];               \
                                                                          \
      while(blk_offset)                                                   \
      {                                                                   \
        bhdr = (hashhdr_type *)&rom_translation_cache[blk_offset];        \
        if(bhdr->pc_value == key)                                         \
          return &rom_translation_cache[                                  \
            blk_offset + sizeof(hashhdr_type) + block_prologue_size];     \
        blk_offset = bhdr->next_entry;                                    \
        blk_offset_addr = &bhdr->next_entry;                              \
      }                                                                   \
                                                                          \
      bhdr = (hashhdr_type *)rom_translation_ptr;                          \
      bhdr->pc_value = key;                                               \
      bhdr->next_entry = 0;                                               \
      *blk_offset_addr = (u32)(rom_translation_ptr -                      \
                               rom_translation_cache);                    \
      rom_translation_ptr += sizeof(hashhdr_type);                        \
      u8 *blkptr = rom_translation_ptr + block_prologue_size;             \
      bool result = translate_block_##type(pc, false);                    \
      if(result)                                                          \
        return blkptr;                                                     \
      return NULL;                                                        \
    }                                                                     \
  }                                                                       \
  return (u8 *)(~0);                                                       \
}

block_lookup_translate_builder(arm);
block_lookup_translate_builder(thumb);
```

The scan pass records direct exits and forces block boundaries at configured translation-gate PCs:

```c
#define scan_block(type, smc_write_op)                                     \
{                                                                          \
  __label__ block_end;                                                     \
  do                                                                       \
  {                                                                        \
    check_pc_region(block_end_pc);                                         \
    smc_write_##type##_##smc_write_op();                                   \
    type##_load_opcode();                                                  \
    type##_pc_base_status();                                               \
    type##_flag_status();                                                  \
    block_data[block_data_position].update_cycles = 0;                     \
                                                                           \
    if(type##_exit_point)                                                  \
    {                                                                      \
      if(type##_opcode_branch)                                             \
      {                                                                    \
        __label__ no_direct_branch;                                        \
        type##_branch_target();                                            \
        block_exits[block_exit_position].branch_target = branch_target;    \
        block_exits[block_exit_position].branch_patch_short = 0;           \
        block_exit_position++;                                             \
        no_direct_branch:;                                                 \
      }                                                                    \
                                                                           \
      type##_set_condition(condition | 0x10);                              \
      /* Unconditional-exit and maximum-exit handling omitted. */          \
    }                                                                      \
    else                                                                   \
      type##_set_condition(condition);                                     \
                                                                           \
    for(i = 0; i < translation_gate_targets; i++)                          \
    {                                                                      \
      if(block_end_pc == translation_gate_target_pc[i])                    \
        goto block_end;                                                    \
    }                                                                      \
                                                                           \
    block_data_position++;                                                 \
    if(block_data_position == HOST_MAX_BLOCK_SIZE)                         \
      break;                                                               \
  } while(1);                                                              \
                                                                           \
  block_end:;                                                              \
}
```

During Thumb emission, every instruction start records its generated address. A target inside the scanned guest range is patched directly to that recorded address; an outside target is looked up and patched after translation:

```c
while(pc != block_end_pc)
{
  block_data[block_data_position].block_offset = translation_ptr;
  thumb_base_cycles();
  update_pc_limits();
  translate_thumb_instruction();
  block_data_position++;

  if (pc != block_end_pc &&
      block_data[block_data_position].update_cycles)
  {
    riscv_arm_const_mask = 0;
    riscv_arm_clear_known_flags();
    generate_cycle_update();
  }
}

generate_translation_gate(thumb);

for(i = 0; i < block_exit_position; i++)
{
  branch_target = block_exits[i].branch_target;

  if((branch_target >= block_start_pc) && (branch_target < block_end_pc))
  {
    translation_target =
      block_data[(branch_target - block_start_pc) /
                 thumb_instruction_width].block_offset;

    if(block_exits[i].branch_patch_short)
      riscv_patch_unconditional_branch_short(
        block_exits[i].branch_source, translation_target);
    else
      generate_branch_patch_unconditional(
        block_exits[i].branch_source, translation_target);
  }
  else
  {
    external_block_exits[external_block_exit_position] = block_exits[i];
    external_block_exit_position++;
  }
}

for(i = 0; i < external_block_exit_position; i++)
{
  branch_target = external_block_exits[i].branch_target;
  translation_target = (branch_target == 0x00000008) ?
    bios_swi_entrypoint : block_lookup_translate_thumb(branch_target);
  if (!translation_target)
    return false;

  if(external_block_exits[i].branch_patch_short)
    riscv_patch_external_unconditional_branch_short(
      external_block_exits[i].branch_source, translation_target);
  else
    riscv_patch_external_unconditional_branch(
      external_block_exits[i].branch_source, translation_target);
}
```

For RV32, the frontend's `generate_translation_gate(thumb)` macro calls block finalization:

```c
#define generate_translation_gate(type)                                      \
  riscv_emit_block_finalize(riscv_block_meta, &translation_ptr,              \
                            block_start_pc, block_end_pc,                     \
                            riscv_block_kind_##type)
```

The finalizer appends the C-dispatcher tail unless the block already emitted a terminal path or has no fallthrough:

```c
void riscv_emit_block_finalize(riscv_jit_block_meta *meta,
                               u8 **translation_ptr,
                               u32 block_start_pc,
                               u32 block_end_pc,
                               bool thumb_mode)
{
  u8 *ptr = *translation_ptr;
  u32 store_alert_branches = riscv_block_meta_chain_offset(meta);
  u8 *helper_tail = NULL;
  bool terminal_at_end =
    (meta->flags & RISCV_BLOCK_TERMINAL_EMITTED) &&
    ((u32)meta->end_delta_thumb << 1) == (u32)(ptr - (u8 *)meta);

  riscv_block_meta_set_final_range(meta, block_start_pc, block_end_pc,
                                   thumb_mode);

  if (!(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
  {
    ptr = ((u8 *)meta) + block_prologue_size;
    helper_tail = ptr;
    riscv_emit_helper_call(&ptr, meta);
  }
  else if (!terminal_at_end)
  {
    if ((meta->flags & RISCV_BLOCK_NO_FALLTHROUGH) &&
        !store_alert_branches)
    {
      helper_tail = NULL;
    }
    else
    {
      if (!(meta->flags & RISCV_BLOCK_PC_WRITTEN))
      {
        riscv_emit_guest_pc_load_existing_base(&ptr, meta, riscv_reg_t0,
                                               block_end_pc);
        riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
      }

      helper_tail = ptr;
      riscv_emit_helper_call(&ptr, meta);
    }
  }
  else
  {
    helper_tail = ptr - riscv_terminal_helper_size;
  }

  if ((meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED) &&
      store_alert_branches && helper_tail)
  {
    riscv_patch_store_alert_branches(meta, store_alert_branches,
                                     helper_tail);
  }

  *translation_ptr = riscv_align_ptr(ptr);
  riscv_blocks_emitted++;
}
```

### 24.6 Cycle-aware direct-chain patch sites

This is the most relevant fast-path implementation. An emitted branch first tests the live cycle counter. If cycles remain, it executes a patched direct jump. If cycles are exhausted, it stores the guest target PC and enters the dispatcher/scheduler tail.

From `riscv/riscv_runtime.c`:

```c
static u8 *riscv_emit_branch_patch_site_with_cycle_exit(
  u8 **ptr_ref,
  riscv_jit_block_meta *meta,
  u32 target_pc,
  bool short_patch_site,
  bool flush_before_patch)
{
  u8 *ptr = *ptr_ref;
  u8 *branch_source;
  u8 *cycle_exit_branch;
  u32 continuation_valid_mask;
  u32 continuation_dirty_mask;

  if (flush_before_patch)
  {
    riscv_emit_mapped_regs_flush_dirty(&ptr);
    if (!short_patch_site)
    {
      /* External targets are independently translated and assume every
       * fixed mapping is live. The dispatcher would reload these mappings,
       * but a direct chain bypasses it. */
      riscv_emit_mapped_regs_reload_mask(
        &ptr, RISCV_MAPPED_REGS_MASK & ~riscv_mapped_valid_mask);
    }
  }

  continuation_valid_mask = riscv_mapped_valid_mask;
  continuation_dirty_mask = riscv_mapped_dirty_mask;

  riscv_emit_cycles_load(&ptr, riscv_reg_t4);
  cycle_exit_branch = ptr;
  riscv_emit_bge(riscv_reg_zero, riscv_reg_t4, 0);

  branch_source = short_patch_site ?
    riscv_emit_unconditional_branch_patch_site_short(&ptr, false) :
    riscv_emit_unconditional_branch_patch_site(&ptr, false);

  riscv_patch_local_branch(cycle_exit_branch, ptr);

  if (!flush_before_patch)
    riscv_emit_mapped_regs_flush_dirty(&ptr);
  riscv_invalidate_mapped_regs();
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, target_pc);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_helper_call_no_flush(&ptr, meta);

  /* The slow scheduler tail does not fall through. Restore the compile-time
   * model used by the internal hot continuation emitted after this site. */
  riscv_mapped_valid_mask = continuation_valid_mask;
  riscv_mapped_dirty_mask = continuation_dirty_mask;

  *ptr_ref = ptr;
  return branch_source;
}
```

The terminal unconditional-branch form is similar, but its normal hot path may reload invalid mappings only for an external target:

```c
static u8 *riscv_emit_terminal_branch_patch_site_with_cycle_exit(
  u8 **ptr_ref,
  riscv_jit_block_meta *meta,
  bool short_patch_site,
  bool flush_before_patch)
{
  u8 *ptr = *ptr_ref;
  u8 *branch_source;
  u8 *cycle_exit_branch;

  if (flush_before_patch)
    riscv_emit_mapped_regs_flush_dirty(&ptr);

  riscv_emit_cycles_load(&ptr, riscv_reg_t4);
  cycle_exit_branch = ptr;
  riscv_emit_bge(riscv_reg_zero, riscv_reg_t4, 0);

  if (flush_before_patch && !short_patch_site)
  {
    riscv_emit_mapped_regs_reload_mask(
      &ptr, RISCV_MAPPED_REGS_MASK & ~riscv_mapped_valid_mask);
  }

  branch_source = short_patch_site ?
    riscv_emit_unconditional_branch_patch_site_short(&ptr, false) :
    riscv_emit_unconditional_branch_patch_site(&ptr, false);

  riscv_patch_local_branch(cycle_exit_branch, ptr);
  riscv_emit_terminal_helper_call_no_flush(&ptr, meta);

  *ptr_ref = ptr;
  return branch_source;
}
```

The two patch encodings are:

```c
void riscv_patch_unconditional_branch(u8 *source, const u8 *target)
{
  s32 offset = (s32)((intptr_t)target - (intptr_t)source);
  s32 upper = (offset + 0x800) >> 12;
  s32 lower = offset - (upper << 12);

  ((u32 *)source)[0] =
    riscv_encode_u(riscv_opcode_auipc, riscv_reg_t6, (u32)upper);
  ((u32 *)source)[1] =
    riscv_encode_i(riscv_opcode_jalr, 0x0,
                   riscv_reg_zero, riscv_reg_t6, lower);
}

void riscv_patch_unconditional_branch_short(u8 *source, const u8 *target)
{
  s32 offset = (s32)((intptr_t)target - (intptr_t)source);
  ((u32 *)source)[0] = riscv_encode_j_inst(riscv_reg_zero, offset);
}
```

Only external patches honor debug forced-dispatch mode. Internal patches are still applied directly by `cpu_threaded.c`:

```c
void riscv_patch_external_unconditional_branch(u8 *source,
                                               const u8 *target)
{
  if (!riscv_debug_force_dispatch)
    riscv_patch_unconditional_branch(source, target);
}

void riscv_patch_external_unconditional_branch_short(u8 *source,
                                                     const u8 *target)
{
  if (!riscv_debug_force_dispatch)
    riscv_patch_unconditional_branch_short(source, target);
}
```

### 24.7 Thumb conditional-branch lowering used by the wait loop

The `BEQ 0x080008c6` at `0x080008ce` is an internal direct target when it lies inside the currently scanned block. The frontend selects a short one-instruction patch for such targets:

```c
#define riscv_branch_patch_internal()                                      \
  ((block_exits[block_exit_position].branch_target >= block_start_pc) &&   \
   (block_exits[block_exit_position].branch_target < block_end_pc))

#define riscv_branch_patch_short() \
  riscv_branch_patch_internal()

#define thumb_conditional_branch(...)                                      \
  do                                                                       \
  {                                                                        \
    bool riscv_short_patch = riscv_branch_patch_short();                   \
    if (riscv_emit_native_thumb_conditional_branch(                        \
          &translation_ptr, riscv_block_meta,                              \
          &block_exits[block_exit_position].branch_source,                \
          opcode, pc, cycle_count, riscv_arm_known_flag_mask,              \
          riscv_arm_known_flags, riscv_short_patch, true))                 \
    {                                                                      \
      block_exits[block_exit_position].branch_patch_short =                \
        riscv_short_patch;                                                 \
      block_exit_position++;                                               \
    }                                                                      \
    else                                                                   \
      riscv_emit_thumb_instruction(true);                                  \
  } while (0)
```

The RV32 conditional-branch emitter preserves separate compile-time mapping masks for the non-taken continuation while emitting the taken chain path:

```c
bool riscv_emit_native_thumb_conditional_branch(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u8 **branch_source,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 known_flag_mask,
  u32 known_flags,
  bool short_patch_site,
  bool flush_before_patch_site)
{
  u32 hi = opcode >> 8;
  u32 condition = hi & 0x0fu;
  u32 target_pc =
    pc + 4u + (u32)((s32)((opcode & 0xffu) << 24) >> 23);
  u8 *ptr = *translation_ptr_ref;
  u8 *branch_skip;
  u32 fallthrough_valid_mask;
  u32 fallthrough_dirty_mask;

  if (branch_source)
    *branch_source = NULL;
  if (hi < 0xd0u || hi > 0xddu)
    return false;

  riscv_emit_adjust_cycles(&ptr, cycles);

  /* Branch if the guest condition is false, around the taken path. */
  if (!riscv_emit_arm_condition_branch(&ptr, condition ^ 1u, 0,
                                       &branch_skip))
    return false;

  fallthrough_valid_mask = riscv_mapped_valid_mask;
  fallthrough_dirty_mask = riscv_mapped_dirty_mask;

  if (branch_source)
    *branch_source = riscv_emit_branch_patch_site_with_cycle_exit(
      &ptr, meta, target_pc, short_patch_site, flush_before_patch_site);
  else
    (void)riscv_emit_branch_patch_site_with_cycle_exit(
      &ptr, meta, target_pc, short_patch_site, flush_before_patch_site);

  riscv_patch_local_branch(branch_skip, ptr);
  riscv_mapped_valid_mask = fallthrough_valid_mask;
  riscv_mapped_dirty_mask = fallthrough_dirty_mask;

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}
```

### 24.8 Thumb BL, BLH, and BX paths relevant to the callback

The frontend must consume the scan-pass exit slot for a native Thumb BL pair even though native BL returns through dispatcher/indirect lookup rather than an exposed direct patch site:

```c
#define thumb_bl()                                                          \
  do                                                                        \
  {                                                                         \
    block_exits[block_exit_position].branch_source = NULL;                  \
    block_exits[block_exit_position].branch_patch_short = 0;                \
    block_exit_position++;                                                  \
    if (riscv_emit_native_thumb_bl_pair(&translation_ptr, riscv_block_meta,  \
                                        last_opcode, opcode, pc,            \
                                        cycle_count))                       \
      cycle_count = 0;                                                       \
    else                                                                     \
      riscv_emit_current_thumb_instruction();                               \
  } while (0)
```

The native BL pair computes the destination and odd Thumb link value, stores LR and PC, and terminates through the helper tail:

```c
bool riscv_emit_native_thumb_bl_pair(u8 **translation_ptr_ref,
                                     riscv_jit_block_meta *meta,
                                     u32 first_opcode,
                                     u32 second_opcode,
                                     u32 pc,
                                     u32 cycles)
{
  u8 *ptr = *translation_ptr_ref;
  s32 high_offset;
  u32 low_offset;
  u32 target_pc;
  u32 link_value;
  s32 target_delta;

  if (first_opcode < 0xf000u || first_opcode >= 0xf800u ||
      second_opcode < 0xf800u)
    return false;

  high_offset = (s32)((first_opcode & 0x07ffu) << 21) >> 9;
  low_offset = (second_opcode & 0x07ffu) * 2u;
  target_pc = pc + 2u + (u32)high_offset + low_offset;
  link_value = (pc + 2u) | 1u;
  target_delta = (s32)(target_pc - link_value);

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, link_value);
  riscv_emit_arm_reg_store(&ptr, REG_LR, riscv_reg_t0);
  if (riscv_i12_fits((u32)target_delta))
  {
    u8 *translation_ptr = ptr;
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, target_delta);
    ptr = translation_ptr;
  }
  else
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, target_pc);
  }
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_adjust_cycles(&ptr, cycles);
  meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  riscv_emit_terminal_helper_call(&ptr, meta);

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}
```

The standalone BLH path, used only when the second half is entered independently, consumes the old LR as the high-half base:

```c
bool riscv_emit_native_thumb_blh(u8 **translation_ptr_ref,
                                 riscv_jit_block_meta *meta,
                                 u32 opcode,
                                 u32 pc,
                                 u32 cycles)
{
  u32 hi = opcode >> 8;
  u32 low_offset = (opcode & 0x07ffu) * 2u;
  u32 link_value = (pc + 2u) | 1u;
  u8 *ptr = *translation_ptr_ref;

  if (hi < 0xf8u)
    return false;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, REG_LR);
  if (low_offset)
  {
    riscv_emit_li(&ptr, riscv_reg_t1, low_offset);
    u8 *translation_ptr = ptr;
    riscv_emit_add(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
    ptr = translation_ptr;
  }
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, link_value);
  riscv_emit_arm_reg_store(&ptr, REG_LR, riscv_reg_t0);
  riscv_emit_adjust_cycles(&ptr, cycles);
  meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  riscv_emit_terminal_helper_call(&ptr, meta);

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}
```

The callback subroutine return uses Thumb `BX`. This code extracts the interworking bit before the `REG_PC` store flushes dirty live mappings:

```c
bool riscv_emit_native_thumb_bx(u8 **translation_ptr_ref,
                                riscv_jit_block_meta *meta,
                                u32 opcode,
                                u32 pc,
                                u32 cycles)
{
  u32 hi = opcode >> 8;
  u32 hrs = (opcode >> 3) & 0x0fu;
  u8 *ptr = *translation_ptr_ref;

  if (hi != 0x47u)
    return false;

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, hrs, pc + 4u);
  u8 *translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t3, riscv_reg_t0, 1);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 5);
  riscv_emit_andi(riscv_reg_t1, riscv_reg_t0, -2);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t1);
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t2, REG_CPSR);
  translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t2, riscv_reg_t2, -33);
  riscv_emit_or(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t2);

  riscv_emit_adjust_cycles(&ptr, cycles);
  meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  riscv_emit_terminal_helper_call(&ptr, meta);

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}
```

### 24.9 Native Thumb halfword load/store lowering

The frontend sends all Thumb memory formats, including the callback's `LDRH` and `STRH`, to one native decoder:

```c
#define thumb_access_memory(...)                                             \
  do                                                                         \
  {                                                                          \
    bool riscv_thumb_cycles_emitted = false;                                 \
    if (riscv_emit_native_thumb_access_memory(                               \
          &translation_ptr, riscv_block_meta, opcode, pc, cycle_count,       \
          &riscv_thumb_cycles_emitted))                                      \
    {                                                                        \
      if (riscv_thumb_cycles_emitted)                                        \
        cycle_count = 0;                                                     \
    }                                                                        \
    else                                                                     \
      riscv_emit_thumb_instruction(false);                                   \
  } while(0)
```

The immediate halfword encoding range is `0x80..0x8f`. `mem_type=1` means 16-bit access; bit 11 selects load versus store; and the five-bit immediate is multiplied by two:

```c
bool riscv_emit_native_thumb_access_memory(u8 **translation_ptr_ref,
                                           riscv_jit_block_meta *meta,
                                           u32 opcode,
                                           u32 pc,
                                           u32 cycles,
                                           bool *cycles_emitted)
{
  u32 hi = opcode >> 8;
  u32 rd = opcode & 7u;
  u32 rb = (opcode >> 3) & 7u;
  u32 ro = (opcode >> 6) & 7u;
  u32 imm = opcode & 0xffu;
  u32 mem_type = 0;
  u32 offset = 0;
  bool load = false;
  bool reg_offset = false;
  bool pc_relative = false;
  bool sp_relative = false;
  bool known_nonram = false;
  u8 *ptr = *translation_ptr_ref;

  if (hi >= 0x50u && hi <= 0x5fu)
  {
    u32 access_type = (opcode >> 9) & 7u;
    reg_offset = true;
    switch (access_type)
    {
      case 0: load = false; mem_type = 0; break; /* STR  */
      case 1: load = false; mem_type = 1; break; /* STRH */
      case 2: load = false; mem_type = 2; break; /* STRB */
      case 3: load = true;  mem_type = 3; break; /* LDSB */
      case 4: load = true;  mem_type = 0; break; /* LDR  */
      case 5: load = true;  mem_type = 1; break; /* LDRH */
      case 6: load = true;  mem_type = 2; break; /* LDRB */
      default:load = true;  mem_type = 4; break; /* LDSH */
    }
  }
  else if (hi >= 0x60u && hi <= 0x8fu)
  {
    u32 imm5 = (opcode >> 6) & 0x1fu;
    load = (hi & 0x08u) != 0;
    if (hi < 0x70u)
    {
      mem_type = 0;
      offset = imm5 * 4u;
    }
    else if (hi < 0x80u)
    {
      mem_type = 2;
      offset = imm5;
    }
    else
    {
      mem_type = 1;
      offset = imm5 * 2u;
    }
  }
  else
  {
    /* PC-relative and SP-relative formats omitted. */
    return false;
  }

  if (!pc_relative)
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rb);
    if (reg_offset)
    {
      riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, ro);
      u8 *translation_ptr = ptr;
      riscv_emit_add(riscv_reg_a0, riscv_reg_a0, riscv_reg_t0);
      ptr = translation_ptr;
    }
    else if (offset || sp_relative)
    {
      riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_a0,
                                       riscv_reg_a0, offset, true);
    }
  }

  /* Loads pass PC in a1; stores pass PC in a2. */
  riscv_emit_guest_pc_load(&ptr, meta,
                           load ? riscv_reg_a1 : riscv_reg_a2,
                           pc + 2u);

  if (load)
  {
    switch (mem_type)
    {
      case 0:
        riscv_emit_memory_read_call_stack_known(
          &ptr, RISCV_STACK_HELPER_READ32, (uintptr_t)read_memory32,
          known_nonram);
        break;
      case 1:
        riscv_emit_memory_read_call_stack_known(
          &ptr, RISCV_STACK_HELPER_READ16, (uintptr_t)read_memory16,
          known_nonram);
        break;
      case 2:
        riscv_emit_memory_read_call_stack_known(
          &ptr, RISCV_STACK_HELPER_READ8, (uintptr_t)read_memory8,
          known_nonram);
        break;
      case 3:
        riscv_emit_memory_read_call_stack_known(
          &ptr, RISCV_STACK_HELPER_READ8S, (uintptr_t)read_memory8s,
          known_nonram);
        break;
      default:
        riscv_emit_memory_read_call_stack_known(
          &ptr, RISCV_STACK_HELPER_READ16S, (uintptr_t)read_memory16s,
          known_nonram);
        break;
    }

    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    riscv_emit_adjust_cycles(&ptr, cycles + 2u);
  }
  else
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);
    switch (mem_type)
    {
      case 0:
        riscv_emit_memory_store_call_stack_known(
          &ptr, RISCV_STACK_HELPER_STORE32,
          (uintptr_t)riscv_store_u32_pc, false);
        break;
      case 1:
        riscv_emit_memory_store_call_stack_known(
          &ptr, RISCV_STACK_HELPER_STORE16,
          (uintptr_t)riscv_store_u16_pc, false);
        break;
      case 2:
        riscv_emit_memory_store_call_stack_known(
          &ptr, RISCV_STACK_HELPER_STORE8,
          (uintptr_t)riscv_store_u8_pc, false);
        break;
      default:
        return false;
    }

    riscv_emit_adjust_cycles(&ptr, cycles + 1u);
    riscv_emit_store_alert_branch(&ptr, meta);
  }

  if (cycles_emitted)
    *cycles_emitted = true;
  *translation_ptr_ref = ptr;
  return true;
}
```

### 24.10 Helper-call mapping rules and the IWRAM halfword paths

Memory reads flush only dirty caller-saved mappings and then invalidate them. The fast RAM read helper is expected to preserve all callee-saved mappings:

```c
static void riscv_emit_memory_read_call_stack(u8 **ptr, u32 stack_offset)
{
  riscv_emit_mapped_regs_flush_mask(
    ptr, riscv_mapped_dirty_mask & RISCV_MAPPED_CALLER_SAVED_MASK);
  riscv_emit_c_call_stack_raw(ptr, stack_offset);
  riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
  riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
}
```

Fast RAM stores first flush every dirty mapping. The common leaf store is expected to preserve all fixed guest host registers; the slow C path explicitly reloads only `a3..a7`, because `s1..s11` are callee-saved by the RV32 ABI:

```c
static void riscv_emit_memory_store_call_stack(u8 **ptr, u32 stack_offset)
{
  if (riscv_fast_ram_stores_enabled())
  {
    riscv_emit_mapped_regs_flush_dirty(ptr);
    riscv_emit_c_call_stack_raw(ptr, stack_offset);
    return;
  }
  riscv_emit_c_call_stack(ptr, stack_offset);
}
```

The callback's Thumb emitter passes `known_ram=false`. With fast stores enabled, the following selector therefore calls `riscv_store_u16_pc` directly, invalidates caller-saved mapped slots, and reloads them. It does **not** use the assembly `riscv_fast_store_u16` helper shown afterward:

```c
static void riscv_emit_memory_store_call_stack_known(
  u8 **ptr, u32 stack_offset, uintptr_t direct_target, bool known_ram)
{
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
  if (riscv_fast_ram_stores_enabled() && !known_ram)
  {
    riscv_emit_mapped_regs_flush_dirty(ptr);
    riscv_emit_c_call_address_raw(ptr, direct_target);
    riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
    riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
    riscv_emit_mapped_regs_reload_mask(ptr,
                                       RISCV_MAPPED_CALLER_SAVED_MASK);
    return;
  }
#else
  (void)direct_target;
  (void)known_ram;
#endif
  riscv_emit_memory_store_call_stack(ptr, stack_offset);
}
```

The generated fast read stub maps `0x03000000..0x03ffffff` to `iwram+32768` and implements GBA odd-address halfword rotation:

```asm
# riscv_fast_ram_ptr leaves the host data pointer in t0.
# For IWRAM:
slli t0, a0, 17
srli t0, t0, 17
lla  t1, iwram+32768
add  t0, t1, t0

riscv_fast_read_u16:
  andi t2, a0, 1
  riscv_fast_ram_ptr riscv_fast_read_u16_slow
  sub  t0, t0, t2
  lhu  a0, 0(t0)
  beqz t2, 1f
  srli t0, a0, 8
  slli a0, a0, 24
  or   a0, a0, t0
1:
  ret
```

For completeness, the aligned assembly fast halfword helper used by the helper-table path maps the same data region to `iwram+32768`, checks the parallel SMC tag area at `iwram`, performs `SH`, and returns zero when no code tag is present. This is relevant to other sites, but it is not the path selected by the callback's current `STRH` lowering:

```asm
# IWRAM part of riscv_fast_store_ptr:
slli t2, a0, 17
srli t2, t2, 17
lla  t0, iwram+32768
add  t0, t0, t2            # data address
lla  t1, iwram
add  t1, t1, t2            # SMC tag address

riscv_fast_store_u16:
  andi t0, a0, 1
  bnez t0, riscv_fast_store_u16_unaligned
  riscv_fast_store_ptr riscv_fast_store_u16_slow
  lhu  t2, 0(t1)
  sh   a1, 0(t0)
  bnez t2, .Lriscv_fast_store_smc
  li   a0, 0
  ret
```

The slow halfword wrapper publishes the guest PC and uses the complete memory subsystem:

```c
static u32 function_cc riscv_store_u16_pc(u32 address, u32 value, u32 pc)
{
  reg[REG_PC] = pc;
  return riscv_store_u16(address, value);
}

static u32 function_cc riscv_store_u16(u32 address, u32 value)
{
  cpu_alert_type alert;

  if ((address >> 25) == 1u)
  {
    u8 *data;
    const u8 *shadow;
    u32 offset;
    u32 mask;
    bool tagged;

    riscv_store_ram_regions(address, &data, &shadow, &offset, &mask);
    *((u16 *)data) = (u16)value;
    tagged = !(offset & 1u) ? *((const u16 *)shadow) != 0 :
      riscv_store_shadow_tagged_unaligned(shadow, offset, mask, 2u);
    alert = tagged ? CPU_ALERT_SMC : CPU_ALERT_NONE;
  }
  else
  {
    alert = write_memory16(address, (u16)value);
  }

  riscv_cpu_alert |= alert;
  return alert;
}
```

Every emitted store checks the returned alert. Alert branches are linked to the block's terminal helper tail at finalization:

```c
static void riscv_emit_store_alert_branch(u8 **ptr_ref,
                                          riscv_jit_block_meta *meta)
{
  u8 *ptr = *ptr_ref;
  u32 source_offset = (u32)(ptr - (u8 *)meta);
  u32 previous_offset = riscv_block_meta_chain_offset(meta);

  if ((source_offset & (RISCV_BLOCK_CHAIN_UNIT_BYTES - 1u)) ||
      source_offset > RISCV_BLOCK_CHAIN_MAX_BYTES ||
      previous_offset > RISCV_BLOCK_CHAIN_MAX_BYTES)
  {
    riscv_emit_terminal_helper_call(&ptr, meta);
    *ptr_ref = ptr;
    return;
  }

  u8 *translation_ptr = ptr;
  riscv_emit_bne(riscv_reg_a0, riscv_reg_zero, (s32)previous_offset);
  ptr = translation_ptr;
  riscv_block_meta_set_chain_offset(meta, source_offset);
  *ptr_ref = ptr;
}
```

### 24.11 Interpreter and generic-memory truth paths

The interpreter's Thumb memory macro advances the guest PC, decodes operands, and calls the generic memory operation:

```c
#define thumb_access_memory(access_type, op_type, address, reg_op, mem_type) \
{                                                                            \
  thumb_pc_offset(2);                                                        \
  thumb_decode_##op_type();                                                  \
  access_type##_memory_##mem_type(address, reg_op);                          \
}
```

The immediate halfword cases in `cpu.cc` are:

```c
case 0x80 ... 0x87:
  /* STRH rd, [rb + imm] */
  thumb_access_memory(store, mem_imm,
                      reg[rb] + (imm * 2), reg[rd], u16);
  break;

case 0x88 ... 0x8F:
  /* LDRH rd, [rb + imm] */
  thumb_access_memory(load, mem_imm,
                      reg[rb] + (imm * 2), reg[rd], u16);
  break;
```

The generic IWRAM write path in `gba_memory.c` stores the halfword into `GPSP_IWRAM_DATA` at the masked 32 KiB offset:

```c
#define write_memory(type)                                                   \
  switch(address >> 24)                                                      \
  {                                                                          \
    case 0x02:                                                               \
      address##type(ewram, (address & 0x3FFFF)) = eswap##type(value);        \
      break;                                                                 \
                                                                             \
    case 0x03:                                                               \
      address##type(GPSP_IWRAM_DATA, address & 0x7FFF) =                    \
        eswap##type(value);                                                  \
      break;                                                                 \
                                                                             \
    case 0x04:                                                               \
      return write_io_register##type(address, value);                        \
                                                                             \
    /* Other address regions omitted. */                                     \
  }

cpu_alert_type function_cc write_memory16(u32 address, u16 value)
{
  write_memory(16);
  return CPU_ALERT_NONE;
}
```

Generic halfword reads align the address down and rotate an odd-address result by eight bits, matching the fast helper:

```c
u32 function_cc read_memory16(u32 address)
{
  u32 value;
  bool unaligned = (address & 0x01);
  address &= ~0x01;
  read_memory(16);
  if (unaligned)
    ror(value, value, 8);
  return value;
}
```

The callback address `0x0300237c` is aligned, so odd-address behavior should not be involved in this particular failure.

### 24.12 VBlank event creation and IRQ entry

The relevant portion of `main.c:update_gba()` advances the video state, flags VBlank at scanline 160, raises pending interrupts, and returns bit 30 when interrupt entry changed the guest PC:

```c
u32 function_cc update_gba(int remaining_cycles)
{
  u32 changed_pc = 0;
  u32 frame_complete = 0;
  irq_type irq_raised = IRQ_NONE;
  int dma_cycles;

  remaining_cycles = MAX(remaining_cycles, -64);

  do
  {
    unsigned completed_cycles = execute_cycles - remaining_cycles;
    cpu_ticks += completed_cycles;
    remaining_cycles = 0;

    dma_cycles = update_timers(&irq_raised, completed_cycles);
    if (update_serial(completed_cycles))
      irq_raised |= IRQ_SERIAL;

    video_count -= completed_cycles;
    if(video_count <= 0)
    {
      u32 vcount = read_ioreg(REG_VCOUNT);
      u32 dispstat = read_ioreg(REG_DISPSTAT);

      if ((dispstat & 0x02) == 0)
      {
        /* Enter HBlank. */
        dispstat |= 0x02;
        video_count += 272;
        if ((dispstat & 0x01) == 0)
          update_scanline();
        if (dispstat & 0x10)
          irq_raised |= IRQ_HBLANK;
      }
      else
      {
        /* Leave HBlank and advance one scanline. */
        video_count += 960;
        dispstat &= ~0x02;
        vcount++;

        if(vcount == 160)
        {
          dispstat |= 0x01;
          video_reload_counters();
          if (dispstat & 0x8)
            irq_raised |= IRQ_VBLANK;

          for (u32 i = 0; i < 4; i++)
          {
            if(dma[i].start_type == DMA_START_VBLANK)
              dma_transfer(i, &dma_cycles);
          }
        }
        else if (vcount == 228)
        {
          vcount = 0;
          dispstat &= ~0x01;
          render_gbc_sound();
          frame_complete = 0x80000000;
          frame_counter++;
        }

        if(vcount == (dispstat >> 8))
        {
          dispstat |= 0x04;
          if(dispstat & 0x20)
            irq_raised |= IRQ_VCOUNT;
        }
        else
          dispstat &= ~0x04;

        write_ioreg(REG_VCOUNT, vcount);
      }
      write_ioreg(REG_DISPSTAT, dispstat);
    }

    if (irq_raised)
      flag_interrupt(irq_raised);

    if (check_and_raise_interrupts())
      changed_pc = 0x40000000;

    execute_cycles = MAX(video_count, 0);
    for (unsigned i = 0; i < 4; i++)
    {
      if (timer[i].status == TIMER_PRESCALE &&
          timer[i].count < execute_cycles)
        execute_cycles = timer[i].count;
    }
  } while(reg[CPU_HALT_STATE] != CPU_ACTIVE && !frame_complete);

  dma_cycles = MIN(64, dma_cycles);
  dma_cycles = MIN(execute_cycles, dma_cycles);
  return (execute_cycles - dma_cycles) | changed_pc | frame_complete;
}
```

IRQ eligibility and architectural entry are implemented in `cpu.cc`:

```c
#define cpu_has_interrupt()                                  \
  (!(reg[REG_CPSR] & 0x80) && read_ioreg(REG_IME) &&         \
    (read_ioreg(REG_IE) & read_ioreg(REG_IF)))

u32 function_cc check_and_raise_interrupts()
{
  if (cpu_has_interrupt())
  {
    reg[REG_BUS_VALUE] = 0xe55ec002;

    REG_MODE(MODE_IRQ)[6] = reg[REG_PC] + 4;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = 0xD2;
    reg[REG_PC] = 0x00000018;

    set_cpu_mode(MODE_IRQ);

    if (reg[CPU_HALT_STATE] == CPU_STOP ||
        reg[CPU_HALT_STATE] == CPU_HALT)
      reg[CPU_HALT_STATE] = CPU_ACTIVE;

    return 1;
  }
  return 0;
}

cpu_alert_type function_cc flag_interrupt(irq_type irq_raised)
{
  write_ioreg(REG_IF, read_ioreg(REG_IF) | irq_raised);
  return check_interrupt();
}
```

The C dispatcher processes a pending JIT-side alert before deciding whether to call `update_gba()`:

```c
static cpu_alert_type riscv_handle_cpu_alert(void)
{
  cpu_alert_type alert = riscv_cpu_alert;
  riscv_cpu_alert = CPU_ALERT_NONE;

  if (alert & CPU_ALERT_SMC)
    flush_translation_cache_ram();

  if (alert & CPU_ALERT_IRQ)
    check_and_raise_interrupts();

  return alert;
}
```

### 24.13 Exact reason current breakpoints perturb the suspected path

The serial debugger arms both breakpoints and exact traces by disabling external patching and flushing all dynarec caches.

From `esp32s31/uart_debug.c`:

```c
static void breakpoint_arm(uint32_t pc, uint32_t probe_pc,
                           bool only_bad_next, uint32_t expected_next)
{
  trace_cancel();
  g_break_pc = pc;
  g_break_probe_pc = probe_pc;
  g_break_only_bad_next = only_bad_next;
  g_break_expected_next = expected_next;
  g_break_complete_pending = false;
  g_break_active = true;
  g_break_trace_total = 0u;
  riscv_set_runtime_debug_branch_probe_pc(probe_pc);
  riscv_set_runtime_debug_force_dispatch(true);
  flush_dynarec_caches();
}

static void trace_arm(uint32_t count, uint32_t range_start,
                      uint32_t range_length)
{
  g_trace_count = 0u;
  g_trace_remaining = count;
  g_trace_range_start = range_start;
  g_trace_range_end = range_start + range_length - 1u;
  g_trace_active = true;

  /* Existing blocks may already jump directly to one another. Rebuild with
   * external branch patching disabled so every translated block returns
   * through riscv_note_runtime_block_execute() while capture is armed. */
  riscv_set_runtime_debug_force_dispatch(true);
  flush_dynarec_caches();
}

static void trace_restore_fast_dispatch(void)
{
  if (riscv_runtime_debug_force_dispatch())
  {
    riscv_set_runtime_debug_force_dispatch(false);
    flush_dynarec_caches();
  }
}
```

The trace hook is invoked from `riscv_jit_run_block()`, after a generated block has returned to C. A directly chained block never reaches this hook:

```c
void riscv_note_runtime_block_execute(u32 start_pc, u32 end_pc, u32 thumb)
{
  if (g_break_active)
  {
    uart_trace_entry_t *entry =
      &g_break_trace[g_break_trace_total % UART_DEBUG_TRACE_CAPACITY];
    entry->kind = UART_TRACE_BLOCK;
    entry->pc = start_pc;
    entry->end_or_lookup = end_pc;
    entry->next_pc_or_cycles = reg[REG_PC];
    entry->cpsr_or_fallback = reg[REG_CPSR];
    entry->thumb = thumb;
    g_break_trace_total++;
  }

  if (g_break_active && start_pc == g_break_pc &&
      (!g_break_only_bad_next || reg[REG_PC] != g_break_expected_next))
  {
    for (uint32_t i = 0; i < 16u; i++)
      g_break_snapshot.regs[i] = reg[i];
    g_break_snapshot.cpsr = reg[REG_CPSR];
    g_break_snapshot.mode = reg[CPU_MODE];
    g_break_snapshot.next_pc = reg[REG_PC];
    g_break_snapshot.irq_sp = REG_MODE(MODE_IRQ)[5];
    g_break_snapshot.irq_lr = REG_MODE(MODE_IRQ)[6];
    g_break_snapshot.irq_spsr = REG_SPSR(MODE_IRQ);

    g_break_active = false;
    g_break_complete_pending = true;
    g_paused = true;
    riscv_request_runtime_debug_stop();
  }

  if (!g_trace_active || start_pc < g_trace_range_start ||
      start_pc > g_trace_range_end)
    return;

  /* Store the bounded trace record and decrement g_trace_remaining. */
}
```

This explains why the existing observation mechanism cannot prove or disprove an external direct-chain failure. Internal branches inside a translated block may still be directly patched, but all external block-to-block branches return through C and the outer entry reload while tracing.

### 24.14 Why the current QEMU runner can print a false-positive PASS

The current full runner's acceptance condition does not contain a game-progress predicate. From `tests/rv32im/rv32im_full_runner.c`:

```c
passed = g_video_frames == frames &&
         g_frame_width == 240u &&
         g_frame_height == 160u &&
         g_frame_pitch == 480u &&
         !pc_is_bios_reset(reg[REG_PC]);

if (g_use_dynarec)
{
  passed = passed &&
           stats.blocks_executed != 0u &&
           stats.blocks_emitted != 0u &&
           stats.interpreter_fallbacks == 0u;
}

uint32_t irq_flags = 0u;
memcpy(&irq_flags, iwram + 0x237cu, sizeof(irq_flags));

printf("result=%s ... pc=0x%08x ... irq_flags=0x%08x ... reason=%s\n",
       passed ? "PASS" : "FAIL",
       reg[REG_PC], irq_flags,
       passed ? "full_frontend_native_run" : "full_frontend_diverged");
```

In other words, `pc=0x080008c6` and `irq_flags & 1 == 0` do not currently make the test fail.

### 24.15 Mature MIPS backend contract for comparison

The mature MIPS backend always saves the mapped guest registers and collapses flags before calling `update_gba()`. If the returned bit 30 says the PC changed, it performs a fresh lookup instead of returning to the prior generated continuation.

Condensed from `mips/mips_stub.S`:

```asm
mips_update_gba:
  sw   $4, REG_PC($16)          # publish current guest PC
  sw   $ra, REG_SAVE2($16)
  collapse_flags
  save_registers
  move $4, reg_cycles
  cfncall update_gba, 0

  bltz $2, return_to_main       # bit 31: frame complete
  and  reg_cycles, $2, 0x7FFF

  sll  $1, $2, 1               # move bit 30 into sign position
  bltz $1, lookup_pc            # interrupt/event changed guest PC
  lw   $ra, REG_SAVE2($16)

  restore_registers
  jr   $ra                      # resume old generated continuation
  nop

mips_indirect_branch_thumb:
  save_registers
  cfncall block_lookup_address_thumb, 2
  restore_registers
  jr   $2
  nop
```

Its translation gate explicitly performs an indirect lookup without applying another cycle update:

```c
#define generate_translation_gate(type)           \
  generate_load_pc(reg_a0, pc);                   \
  generate_indirect_branch_no_cycle_update(type)
```

The MIPS branch macro checks the cycle counter at every patched branch. When cycles expire, it calls `mips_update_gba`; otherwise it jumps directly:

```c
#define generate_branch_no_cycle_update(writeback_location, new_pc)         \
  if(pc == idle_loop_target_pc)                                              \
  {                                                                          \
    generate_load_pc(reg_a0, new_pc);                                        \
    mips_emit_lui(reg_cycles, 0);                                            \
    generate_function_call_swap_delay(mips_update_gba);                      \
    mips_emit_j_filler(writeback_location);                                  \
    mips_emit_nop();                                                         \
  }                                                                          \
  else                                                                       \
  {                                                                          \
    generate_load_pc(reg_a0, new_pc);                                        \
    mips_emit_bltzal(reg_cycles,                                             \
      mips_relative_offset(translation_ptr, update_trampoline));             \
    generate_swap_delay();                                                   \
    mips_emit_j_filler(writeback_location);                                  \
    mips_emit_nop();                                                         \
  }
```

The expert review should compare the RV32 cycle-exit path against two MIPS invariants visible above:

1. all guest state is authoritative in memory before `update_gba()` can enter an IRQ;
2. a scheduler-reported PC change forces lookup of the new architectural PC rather than resumption at the pre-event continuation.

### 24.16 Thumb ALU lowering used by the callback and polling loop

The relevant guest instructions also include `MOVS`, `ANDS`, `ORRS`, `ADDS`, and `CMP`. The frontend routes ordinary Thumb data processing through this entry:

```c
#define thumb_data_proc(...)                                                \
  do                                                                        \
  {                                                                         \
    if (!riscv_emit_native_thumb_alu_dead_flags_known(                      \
          &translation_ptr, riscv_block_meta, opcode, flag_status,          \
          riscv_arm_const_mask, riscv_arm_const_values,                     \
          riscv_arm_known_flag_mask, riscv_arm_known_flags))                \
    {                                                                       \
      riscv_emit_thumb_instruction(false);                                  \
    }                                                                       \
  } while (0)
```

If any result flags remain live after frontend dead-flag analysis, the Thumb decoder constructs an equivalent unconditional ARM data-processing opcode and delegates to the shared ARM emitter:

```c
static bool riscv_emit_native_thumb_alu_flags(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 flag_status,
  bool clobber_dead_arithmetic_flags,
  u32 const_mask,
  const u32 *const_values,
  u32 known_flag_mask,
  u32 known_flags)
{
  u32 hi = opcode >> 8;
  u32 alu_op = (opcode >> 6) & 3u;
  u32 rd = opcode & 7u;
  u32 rs = (opcode >> 3) & 7u;
  u32 rn = (opcode >> 6) & 7u;
  u32 imm = opcode & 0xffu;
  u32 arm_op = 0;
  u32 arm_rn = 0;
  u32 arm_rd = rd;
  u32 arm_operand2 = 0;
  bool immediate = false;
  bool test_op = false;

  if (hi >= 0x18u && hi <= 0x1fu)       /* ADD/SUB register or imm3 */
  {
    bool subtract = (hi & 0x02u) != 0;
    arm_op = subtract ? 0x2u : 0x4u;
    arm_rn = rs;
    immediate = (hi & 0x04u) != 0;
    arm_operand2 = rn;
  }
  else if (hi >= 0x20u && hi <= 0x27u)  /* MOVS imm8 */
  {
    arm_op = 0xdu;
    arm_rd = hi & 7u;
    immediate = true;
    arm_operand2 = imm;
  }
  else if (hi >= 0x28u && hi <= 0x2fu)  /* CMP imm8 */
  {
    arm_op = 0xau;
    arm_rn = hi & 7u;
    immediate = true;
    arm_operand2 = imm;
    test_op = true;
  }
  else if (hi >= 0x30u && hi <= 0x3fu)  /* ADDS/SUBS imm8 */
  {
    arm_op = (hi & 0x08u) ? 0x2u : 0x4u;
    arm_rn = hi & 7u;
    arm_rd = arm_rn;
    immediate = true;
    arm_operand2 = imm;
  }
  else if (hi == 0x40u && alu_op <= 1u) /* ANDS/EORS */
  {
    arm_op = alu_op;
    arm_rn = rd;
    arm_operand2 = rs;
  }
  else if (hi == 0x42u)                 /* TST/NEG/CMP/CMN */
  {
    switch (alu_op)
    {
      case 0:
        arm_op = 0x8u;
        arm_rn = rd;
        arm_operand2 = rs;
        test_op = true;
        break;
      case 1:
        arm_op = 0x3u;
        arm_rn = rs;
        arm_rd = rd;
        immediate = true;
        arm_operand2 = 0;
        break;
      case 2:
        arm_op = 0xau;
        arm_rn = rd;
        arm_operand2 = rs;
        test_op = true;
        break;
      default:
        arm_op = 0xbu;
        arm_rn = rd;
        arm_operand2 = rs;
        test_op = true;
        break;
    }
  }
  else if (hi == 0x43u)                 /* ORRS/MULS/BICS/MVNS */
  {
    switch (alu_op)
    {
      case 0:
        arm_op = 0xcu;
        arm_rn = rd;
        arm_operand2 = rs;
        break;
      case 2:
        arm_op = 0xeu;
        arm_rn = rd;
        arm_operand2 = rs;
        break;
      case 3:
        arm_op = 0xfu;
        arm_operand2 = rs;
        break;
      default:
        return false; /* MUL has a separate path. */
    }
  }
  else
  {
    return false;
  }

  u32 arm_opcode =
    (0xeu << 28) | (immediate ? (1u << 25) : 0u) |
    (arm_op << 21) | (1u << 20) | (arm_rn << 16) |
    (arm_rd << 12) | arm_operand2;

  if (test_op)
  {
    return riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags(
      translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status,
      false, NULL);
  }

  return riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags(
    translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status,
    false, NULL);
}
```

If no flags are live, the shorter direct Thumb path performs the operation in fixed mapped registers. The relevant cases are:

```c
static bool riscv_emit_native_thumb_alu2(/* arguments omitted */)
{
  u32 hi = opcode >> 8;
  u32 alu_op = (opcode >> 6) & 3u;
  u32 rd = opcode & 7u;
  u32 rs = (opcode >> 3) & 7u;
  u32 rn = (opcode >> 6) & 7u;
  u32 imm = opcode & 0xffu;
  bool need_flags = (flag_status & 0x0fu) != 0;
  bool load_rd = true;
  riscv_reg_number result_reg = riscv_reg_t2;
  u8 *ptr = *translation_ptr_ref;

  if (need_flags)
    return riscv_emit_native_thumb_alu_flags(/* same state arguments */);

  /* Decode validation and test-op early returns omitted. */

  if ((hi >= 0x18u && hi <= 0x1fu) || hi == 0x42u)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);
  else if (hi < 0x20u || hi >= 0x40u)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);

  if (!riscv_arm_reg_mapped(rd, &result_reg, NULL))
    result_reg = riscv_reg_t2;

  if (load_rd)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rd);

  u8 *translation_ptr = ptr;
  if (hi >= 0x20u && hi <= 0x27u)          /* MOVS imm, flags dead */
  {
    if (imm)
      riscv_emit_addi(result_reg, riscv_reg_zero, imm);
    else
      result_reg = riscv_reg_zero;
  }
  else if (hi >= 0x30u && hi <= 0x3fu)     /* ADD/SUB imm */
  {
    if (hi & 0x08u)
      riscv_emit_addi(result_reg, riscv_reg_t0, -(s32)imm);
    else
      riscv_emit_addi(result_reg, riscv_reg_t0, imm);
  }
  else if (hi == 0x40u)                    /* AND/EOR */
  {
    if (alu_op == 0u)
      riscv_emit_and(result_reg, riscv_reg_t0, riscv_reg_t1);
    else
      riscv_emit_xor(result_reg, riscv_reg_t0, riscv_reg_t1);
  }
  else if (hi == 0x43u)                    /* OR/MUL/BIC/MVN */
  {
    switch (alu_op)
    {
      case 0:
        riscv_emit_or(result_reg, riscv_reg_t0, riscv_reg_t1);
        break;
      case 1:
        riscv_emit_mul(result_reg, riscv_reg_t0, riscv_reg_t1);
        break;
      case 2:
        riscv_emit_xori(riscv_reg_t1, riscv_reg_t1, -1);
        riscv_emit_and(result_reg, riscv_reg_t0, riscv_reg_t1);
        break;
      default:
        riscv_emit_xori(result_reg, riscv_reg_t1, -1);
        break;
    }
  }

  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, rd, result_reg);
  *translation_ptr_ref = ptr;
  return true;
}
```

The actual path taken for each instruction depends on `flag_status`, which is produced by the frontend's backward dead-flag analysis. A passive probe should therefore record not only guest register values at `0x080007d4`, but also the translated block boundaries and, ideally, the emitted code for `0x080007c2..0x080007da`.



*Thinking...*

> I'm diving into the technical details of this gpSP GBA emulator JIT bug on ESP32, particularly focusing on how the dynarec architecture handles block translation and branch patching in the cpu_threaded.c implementation.
> 
> The key insight here is understanding gpSP's block lookup mechanism: when a branch target falls within the current block being translated, gpSP doesn't maintain per-instruction entry tables like some other dynarecs do. Instead, it re-translates from that internal entry point as a fresh block, which means multiple overlapping translations can coexist. There's also the translation gate concept—specific PCs where block translation forcibly stops to allow interrupt checks, which is crucial for games that poll flags in tight loops without returning to the dispatcher.
> 
> For the polling loop scenario, each branch iteration decrements the cycle counter, and when cycles expire, the generated code jumps to the cycle-exhaust handler which calls `update_gba`, processes the VBlank interrupt, changes the PC to the IRQ vector, dispatches through the BIOS to the ROM handler, and eventually returns to the interrupted loop so it can read the flag that was set.
> 
> The critical issue is that the MIPS stub must spill all live ARM registers to the `reg[]` array before calling `update_gba`, because the IRQ dispatch mechanism reads and writes directly to `reg[]` — it saves the old CPSR to SPSR, stores registers into mode-banked storage, switches to IRQ mode, sets the PC to 0x18, and sets LR_irq. If any guest register is only live in a host register and hasn't been spilled, the IRQ handler captures a stale value, and when execution resumes at the interrupted address after the IRQ return, the block reloads from `reg[]` and gets the wrong value.
> 
> In the polling loop specifically, r2 holds the address 0x03002360 loaded before the loop, and the loop body repeatedly loads from that address — gpSP's MIPS backend uses static register allocation where certain ARM registers are permanently mapped to MIPS registers across blocks, so r2 must be reloaded from `reg[2]` at block entry.
> 
> The callback code at the end is loading that same address, reading the value twice, ORing with r1 (which was set to 1 earlier), and writing back — this looks like it's manipulating a flag at 0x03002360 + 28.
> 
> The real question is whether the IRQ handler itself is ARM or Thumb mode when it jumps to the game's handler at 0x08000738, and whether the register state assumptions are correct across that boundary. The handler runs in System mode with interrupts re-enabled, which is typical for nested interrupt frameworks like libgba's style — the saved SPSR captures the wait loop's state (Thumb, System, IRQ enabled), and the handler will eventually return through the BIOS dispatcher via BX instruction. Now I'm considering whether SMC cache invalidation could be the culprit: IWRAM writes normally trigger SMC checks in gpSP to flush any translated code at that address, but 0x0300237c is data, not code — though if the SMC handler is miscompiled or the cache flush is incomplete, it might leave stale translated code that never observes the flag write.
> 
> Another angle: the STRH instruction in fast mode might be computing the address incorrectly — Thumb halfword stores scale the immediate by 2, so if the RV32 lowering got the scaling wrong for stores but right for loads (which is why reads return 0x0004), the write could be hitting the wrong address entirely. But the earlier STRH that clears bit 0 at 0x080008b8 appears to work, so that seems less likely. Or maybe there's a different writer — the observed value 0x0004 has bit 2 set, which could correspond to a different interrupt's completion flag (like Timer0) rather than the callback's bit 0, meaning another IRQ handler is writing to this location and overwriting the callback's write.
> 
> Looking at the interrupt enable register IE=0x0005, that's bits 0 and 2 set, which maps to VBlank and VCount. The DISPSTAT value 0x9628 decodes to VBlank IRQ enabled, VCount IRQ enabled at line 150. So the game is using a classic interrupt-wait pattern where a shared flag word at 0x0300237c mirrors the IE bits — bit 0 for VBlank, bit 2 for VCount. The value 0x0004 means the VCount handler ran and set bit 2, but bit 0 (VBlank) never got set. The callback at 0x08000738 might be a common IRQ dispatcher that reads the interrupt flags, determines which IRQ fired, calls the specific handler, then ORs the corresponding bit into the flag word.
> 
> The tail code sets bit 0 specifically at both 0x03007ff8 (the BIOS IntrWait mirror) and 0x03002360+28, which looks like the VBlank-specific handler path. Hardware shows IF=0x0000 (game acknowledged the interrupt), so the handler must have run and cleared IF elsewhere. But here's the critical gap: they never confirmed whether bit 0 actually got set during that trace window, and more importantly, whether forcing dispatch mode through the debugger actually lets the game proceed or if it stays stuck like in JIT mode.
> 
> For gpSP specifically, the key is understanding how translation gates work — they're configured per-game in the config to forcibly terminate block translation at specific guest PC addresses, which allows the main loop to check for interrupts. This is necessary because gpSP's recompiler can translate through conditional branches and even loops within a single block, so without these gates, interrupt polling would be starved. So cycle checks do happen on backward branches within blocks in gpSP, which makes me wonder what translation gates actually solve — the readme mentions they're for idle loops that write to memory the ROM polls, and they work by breaking blocks so IRQ delivery can happen at the gate rather than waiting for the block to exit. The key structural point is that gpSP has no way to enter a block from outside once it's compiled; external branches always go through the lookup table, so blocks are self-contained units that only exit through their defined branch points.
> 
> Now looking at their RISC-V port, if they added an internal guest-PC-to-host-offset gate to reuse part of a block, that's a port-specific extension and a major red flag — entering mid-block assumes the flag state and register state set up by earlier instructions, and gpSP does dead flag elimination where the scanner determines which flags are actually needed, so entering at an internal point with different assumptions about what's live could corrupt the emulation. The other big suspect is how they handle Thumb BL returns: the BL at 0x080007be calls to 0x0800dc40, then returns via `bx r0` back to 0x080007c3 with the Thumb bit set, which normally goes through the indirect branch lookup path, but their port has an indirect lookup cache that was recently restricted to entries with valid mapped-register state — that restriction might be causing the issue. And in gpSP's MIPS backend, ARM registers live in fixed MIPS registers persistently rather than always going to memory, so the register state contract between blocks is critical.
> 
> The RISC-V port uses dynamic valid/dirty masks instead, which is a different design entirely, and if a patched direct chain jumps past the entry prologue to skip reloads, it would break that contract. The most likely culprit is probably the interaction between direct block chaining, the indirect lookup cache, and possibly skipping mapped-register reloads at validated entry points. What's different about this scenario compared to the title screen is the IRQ-driven mode switching between System and IRQ modes with banked SP/LR, combined with IntrWait-style flag polling and potentially nested IRQs — the VCount IRQ fires at line 150 and could interact with or nest inside the VBlank wait that the game is using.
> 
> Looking at the handler flow, both the VBlank and VCount paths seem to be entered at the dispatcher (0x08000738), but the key issue might be that the VBlank branch is never taken in fast mode due to a condition-flag evaluation bug in the RV32 backend — something like a miscompiled `tst` and `beq` that causes the VBlank case to be skipped while the VCount case (bit 2) works, which would explain why the flag shows only 0x0004.
> 
> Now I need to focus on what the user is actually asking for: they want an expert review identifying the likely contract violation and a minimally perturbing localization fix. I should answer their numbered questions using gpSP-specific knowledge about how the dynarec handles block chaining, interrupt acknowledgment, and the actual translation contract in the MIPS backend. IRQ handling completes when the BIOS executes a return instruction that restores the CPSR from SPSR and swaps the register banks back, then the lookup dispatches to the resumed address. At block boundaries, I need to ensure all flag bits are materialized in the CPSR since subsequent blocks might depend on them—gpSP tracks flag liveness through the block and forces full materialization at exits to be conservative about unknown successors, though dead flag elimination can skip updates for flags proven unused before the next definitive write.
> 
> The freeze pattern—bit 2 set, bit 0 clear—points to a critical bug: after the IRQ returns, execution might resume at the containing block's start (0x080008ac) rather than the internal PC (0x080008c6), causing the function head to re-execute and clear bit 0 again with its strh instruction while bit 2 persists because nothing in that head clears it—the ands mask only affects bit 0. This would trap the wait loop forever with the exact flag signature observed.
> 
> The culprit is likely their indirect lookup cache: if it maps the return PC to a stale or aliased entry pointing to the block start instead of the exact PC match—perhaps due to a hash collision or masking bug in how they store block-start addresses—then bx/IRQ returns land at the function prologue, triggering the livelock. Under forced dispatch with cache flush, fresh block lookups resolve to the correct PC, masking the bug entirely. The IRQ handler restores the return address from LR_irq minus 4, which points back to 0x080008c6 in Thumb mode. When the lookup finds the block at that exact address, it would resume execution there — but this still doesn't fully explain why bit 2 stays set while bit 0 clears, unless the block's initial execution path through the flag-clearing code somehow differs from what I'd expect on re-entry.
> 
> I'm now considering a RISC-V backend issue with conditional branch patching in direct-chaining mode. When two blocks are chained with a conditional branch, the RV32 encoding has a limited immediate range (±4KiB for B-type instructions), so longer distances require an inverted branch over a JAL or AUIPC+JALR trampoline. If there's a polarity bug in this range-dependent relaxation — where the taken/not-taken semantics flip in certain distance buckets — then the VBlank handler dispatch could branch the wrong way depending on code layout. Cache flushes change memory layout and thus patching distances, which would explain why the bug appears and disappears with debug toggles. Their uncommitted changes mention handling RVC relaxation with assembler-resolved labels, so code size is variable and patching distances shift around.
> 
> My second suspect is that returning from the IRQ handler lands at an internal block entry point rather than the function head, causing the bit-0-clearing prologue to re-execute on every frame. I can test this passively by counting how many times block 0x080008ac executes while frozen — it should be rare, but if it fires once per VBlank, that's the smoking gun. Even simpler: poll the halfword at 0x0300237c from the debug interface at high frequency to see if bit 0 pulses briefly to 1, which would indicate a write followed by a re-execution clearing it.
> 
> The STRH lowering theory is weakened by the fact that bit 2 got set into that address somehow, suggesting halfword stores do work in some paths. This points more toward a control-flow or flag-state issue rather than a pure instruction lowering bug. The VCount handler keeps firing every frame and ORing bit 2, so the parity between what got set and what's frozen is suggestive of a path-dependent failure.
> 
> My fourth angle is a flag-state bug at chained conditional boundaries — where a block ending in a compare-and-branch chains directly to a successor that assumes flags are cached in host registers, but the chain skips materializing those flags to memory.  In gpSP, flags follow a convention; if the RV32 port caches them lazily in a host register and direct chains don't flush, the dispatcher's test-and-branch deciding vblank versus not could read a stale flag. Same family as the fast-path-only conditional misbehavior.
> 
> For their specific questions: gpSP stock doesn't have mid-block entry gates in the traditional sense — translation gates are forced block terminations used for IntrWait-style polling loops. I need to clarify whether this game is configured with gates. On other platforms gpSP handles the ROM fine without gates because loops do get cycle checks. The cycle counter accumulates during scanning, and branches emit cycle checks; when the cycle register goes negative, the update path triggers. Translation gates exist for idle loop elimination and forced termination points specified in the game config.
> 
> In stock gpSP, direct patched chains target block-start entries only; block_lookup_address translates a new block at any requested PC. If the RV32 port added internal-entry reuse, that's novel and must replicate full block-entry invariants — register residency per fixed convention, flag storage convention, and cycle counter accounting since block cycle deltas accumulate per segment.
> 
> Before update_gba and memory handlers, all dirty mapped guest registers get written back to the register array, flags collapse per convention, REG_PC materializes, and the cycle counter writes to its expected location. After update_gba returns, the port must reload per convention. IRQ can change PC and mode, so it must re-dispatch via lookup, never resume the patched fallthrough. In mips_stub, mips_update_gba reloads PC, does block_lookup_address, and jumps — never falls back inline. If the RV32 cycle-exit path doesn't enforce this, it could resume the interrupted block directly instead of looking up the new PC.
> 
> For probes, I'm emitting a short sequence at translate time that saves two scratch registers to a dedicated probe scratch area, writes a ring entry via a fixed base register, and restores — all inline during translation so chain topology stays unchanged. Probes at block starts work naturally, but probes at internal PCs require mid-block insertion, which changes intra-block offsets but is fine since blocks regenerate anyway. I'm keeping probes branch-free and flag-clean, using two saved temporaries, and triggering a cache flush once when arming.
> 
> For Q6, the inspection order should prioritize passive analysis: first check the write-history of that address to bisect the hypothesis space, then dump the live translation cache and block metadata over serial without executing anything — snapshot the actual generated code for the involved blocks and disassemble offline to verify each patched branch target against the metadata. This is completely non-perturbative and should be the top recommendation.
> 
> For Q7, I'm documenting the invariants at chain boundaries from the MIPS backend: fixed register residency, flags in canonical locations, cycle counter in a dedicated register with negative-trigger, PC not materialized except at exits, direct branches only to block entry points, and patch sites within reach or via literal/trampoline. On RISC-V I need fence.i after patching, and there's a potential cache coherence hazard if the translation cache lives in PSRAM XIP — code executes from IRAM/PSRAM caches, so if JIT code is in PSRAM, dcache writeback and invalidation become critical after patching.
> 
> The cache sync issue on ESP32-S3 is a strong suspect: patching a direct chain in PSRAM-executed code without proper cache writeback/invalidate could leave stale branch targets in the icache, causing intermittent execution of old code paths. This fits the pattern perfectly — initial block emission does full sync, but later patch writes into already-executing code may skip synchronization, and a stale icache line could make a conditional exit branch to an old dispatcher stub or target. Forced-dispatch and full flush would mask this by doing global cache sync.
> 
> For Q8, cycle accounting at internal entries matters because entering mid-block without adjusting gpSP's cycle deltas causes over/under-counting, which combined with IRQ timing could affect behavior; register and flag assumptions are also critical at those points.
> 
> On the BIOS side, the IRQ return path through the real BIOS involves restoring CPSR via `subs pc, lr, #4` in ARM, but SPSR restoration in RV32 appears correct (0x6000003f observed), so that's not the culprit. The wait loop block itself might be the issue — if the fast-read path for IWRAM is computing the wrong host address due to a mirroring or masking bug, the loop would read stale or garbage data each iteration, especially if the self-chained loop block 0x080008c6..0x080008d4 is involved.
> 
> I should also check whether this game has an idle_loop_eliminate_target in gpSP's game_config — if so, they'd handle it specially, though I can't verify with the anonymized filename. For the response structure, since this is an expert review, I'll write it as thorough prose with headers rather than bullet lists, and I should keep the harness gate fix recommendation brief since they already know about it. I should start with zero-perturbation experiments: polling the register from the host side to catch single-pulse transitions, then dumping the live translation cache and block metadata in the frozen state to verify every patched exit and handler block against the metadata offline—a layout-dependent patch bug would be visible statically. The ESP32-S3 cache coherence during patching is my top hardware suspect: if the translation cache lives in IRAM or PSRAM XIP, patching requires both dcache writeback and icache invalidation (fence.i alone isn't sufficient on this series), and a missed sync on small patch stores would produce exactly the symptom of failure in normal chaining but disappearance during debug flushes. RISC-V branch-range constraints also matter—B-type instructions have ±4KiB range, JAL ±1MiB—so chain targets across a multi-hundred-KiB cache need trampolines, and polarity or range-bucket bugs would appear only for certain layouts, consistent with the conflicting results after cache flushes. For the gpSP contract specifics, stock gpSP has no internal block entry points; lookup translates a fresh block at the exact PC, and overlapping translations are normal and correct—translation gates are forced block terminations at configured guest PCs from game_config, not mid-block entry points. The probe mechanism stores to a reserved scratch area and regenerates once per cycle to keep chaining active. For the investigation order, I should document the problematic address first, then verify the live cache state, with the strh lowering deferred. The MIPS invariants need to be spelled out: fixed register residency at boundaries, flags in canonical slots, cycle counter checked at every branch, direct branches only to block starts, patched sites synced with instruction cache, and indirect jumps always through lookup stubs. For internal branch targets within a block, stock gpSP emits checkpoints at translation time, though I'm uncertain whether later-discovered targets trigger block splitting. I also need to verify whether their IRQ return uses the lookup cache with the guest PC and Thumb bit as the key, since an aliasing entry could produce the steady state directly — and their minimal deopt experiment should prioritize disabling internal-entry chaining.
> 
> Forcing the lookup cache off entirely with a single flag is cheap to test. The QEMU harness distinction matters: qemu-user won't reproduce ESP32 cache-coherence issues, so differential passing on QEMU wouldn't rule out the patch-sync hypothesis, but a historical QEMU repro would suggest the bug is architectural in the emit/patch logic rather than hardware cache sync alone. I'll structure this with headings and prose, keeping enumerations inline and avoiding bullets.

## Overall assessment

The report is thorough and the elimination work is sound. I agree the failure is localized to the JIT's normal fast-dispatch behavior, and I agree the forced-dispatch observer effect is the central obstacle. But I think one piece of evidence you already have is more diagnostic than the report treats it, and it should reorder your hypotheses.

## The flag value 0x0004 is itself the strongest clue

The steady-state value at `0x0300237c` is not merely "bit 0 missing" — it is "bit 2 present." This looks like a standard IntrWait-style acknowledgement word where each bit mirrors an IE bit position: bit 0 for VBlank, bit 2 for VCount. Your captured `IE=0x0005` and `DISPSTAT=0x9628` (VBlank IRQ enable and VCount IRQ enable at LYC=150) confirm both interrupt sources are live every frame.

That means some IRQ path is successfully executing an ORR-and-STRH into this exact halfword under normal fast dispatch — presumably the VCount handler tail, which is structurally analogous to the VBlank tail you disassembled. This substantially weakens hypothesis 17.2 (generic STRH or halfword fast-store miscompilation) and generic IWRAM addressing/mirroring theories: the same store idiom to the same address demonstrably works. The failure is path-dependent, not instruction-dependent.

Two families of explanations fit a path-dependent failure with this exact signature. First, the VBlank-specific branch of the dispatcher at `0x08000738` is never taken in fast mode — a conditional-exit patching or flag-materialization bug that only manifests with direct chaining. Second, and I find this one compelling: bit 0 *is* set, and something re-clears it. Look at the only code in your report that clears bit 0: the wait function prologue at `0x080008b0..0x080008b8`, which ANDs with `0xfffe` — clearing exactly bit 0 while preserving bit 2. If anything in the fast path resolves a return or IRQ-resume to `0x080008c6` incorrectly — for example, an indirect-lookup-cache entry or internal-gate table that aliases to the *containing block* starting at `0x080008ac` — then every VBlank return re-executes the prologue, re-clears bit 0, and produces precisely the frozen state you observe, including the surviving bit 2. Your own trace note in §10 ("trace metadata identifies the containing translated block, not necessarily the exact internal guest entry point") tells me your runtime has machinery in which "containing block" and "exact PC" are distinguishable concepts, which is exactly where such aliasing lives.

This yields a nearly free discriminating experiment: passively count executions of the block starting at `0x080008ac` while frozen. Legitimately, that function head should execute zero times once the loop is entered. If it executes once per frame, you have the root cause class immediately.

## Two zero-perturbation experiments to run before anything else

Before adding probes, do two things that perturb nothing at all. First, poll `0x0300237c` from the host debug interface as fast as the serial link allows. If bit 0 ever pulses to 1 and returns to 0, the store executes and is being re-cleared — the re-execution/aliasing family. If it never pulses across thousands of samples spanning many frames, the store path is never reached. Sampling can miss narrow pulses, but at ~60 events per second you will catch a pulse eventually if it exists.

Second, dump the live translation cache and block metadata in the frozen state — without flushing, without forcing dispatch, without resuming. Disassemble offline. Statically verify every patched exit of: the loop block at `0x080008c6`, the containing block at `0x080008ac`, the handler blocks covering `0x08000738..0x080007da`, and the IRQ dispatch/return stubs. A layout-dependent patch bug is visible statically; you do not need to observe it executing. This directly addresses your §11 limitation, because the artifact of the bug (a wrong patched target, a wrong branch polarity, a stale trampoline) persists in the frozen cache.

## Two RISC-V/ESP32-specific mechanisms the report does not weigh

Where does your translation cache live, and how are patch writes made visible to the fetch path? On ESP32-S3-class parts, code executed through the cache (especially anything in PSRAM XIP, but also flash-cached regions) requires explicit data-cache writeback plus instruction-cache invalidation after modification; a bare `fence.i` is not sufficient when the code sits behind the external-memory cache. Initial block emission usually gets this right because it syncs the whole block; the small in-place stores used for *later* chain patching are exactly where a missed sync hides. A stale patched branch produces "fails only in normal chaining, and every debug intervention that flushes caches makes it disappear" — which is your exact phenomenology. That said, your historical QEMU-user reproduction argues the primary bug is architectural in the emitter rather than hardware cache sync, since qemu-user has no such coherence hazard. Both can be true; audit the patch-site sync regardless, because it will bite eventually.

Second, RISC-V branch reach. B-type conditionals reach ±4 KiB; JAL reaches ±1 MiB; anything further needs AUIPC+JALR. With ~386 KB of ROM-derived code, cross-block chains routinely exceed B-type range, so your patcher must be doing inverted-branch-over-JAL or trampoline selection per distance bucket. A polarity or bucket-selection bug manifests only for particular code layouts — consistent with your "conflicting results" when toggling debug masks (each toggle flushes and relays out the cache) and with the title screen surviving while a newly translated post-Start layout fails. Your uncommitted RVC-relaxation fix shows layout instability is already a known hazard in this port. The static cache dump above will catch this.

## Answers to the reviewer questions

On questions 1 and 2: stock gpSP has no internal entry points into translated blocks. `block_lookup_address_*` translates a fresh block beginning at the exact requested guest PC; overlapping translations of the same guest code are normal and correct, and a patched chain may only target a block's canonical entry. Translation gates in stock gpSP (`translation_gate_target` in the game config) are forced block *terminations* at configured PCs — they exist precisely for IntrWait-style flag-poll loops — not mid-block entry mechanisms. If your port added internal-entry reuse into larger blocks as an optimization, it is a novel extension of the contract, and the burden is on it to reproduce every block-entry invariant: register residency convention, flag storage convention, and correct cycle accounting (gpSP accumulates per-instruction cycle deltas from block start and emits the subtraction at exits; entering mid-block through an exit compiled with block-start-relative counts over-charges cycles). My honest recommendation is to delete internal-entry reuse for this ROM entirely and translate fresh blocks like stock gpSP. That is your §18.5 item 2, and I would run it first among the deopt experiments because it is also the cheapest.

On question 3: before `update_gba()`, IRQ dispatch, memory handlers, and indirect lookup, all dirty mapped guest registers must be written back to `reg[]`, lazily-held flags collapsed into their canonical slots, `REG_PC` materialized, and the cycle counter stored where the scheduler expects it. After `update_gba()` returns, control must re-enter through PC lookup, never resume the interrupted block inline, because IRQ dispatch may have retargeted the PC and — critically — `set_cpu_mode`/`raise_interrupt` rearrange the mode-banked registers, invalidating any host-cached SP or LR. The same applies on SPSR-restore return. If your fast path holds any banked register in a host register across an update boundary, System↔IRQ transitions are exactly where it breaks, and this game's handler runs in System mode with nesting-style conventions, which the title screen may simply not exercise as hard.

On question 4: yes. gpSP's scanner does dead-flag elimination, conservative at block exits. If your port keeps NZCV lazily in host registers, then a cycle-exhaust exit (which spills flags to memory and returns via lookup) and a direct chain (which may not) present different flag states to the same successor. A dispatcher conditional deciding VBlank-versus-VCount is a plausible consumer. This is a fast-mode-only divergence by construction.

On question 5: emit probes at translation time from a fixed guest-PC watch list, inline in the generated code, branch-free and call-free: spill two scratch registers to a dedicated save area (not the guest stack), write a ring-buffer record through an absolute base, restore. Arm the probes with a single cache flush, then run with chaining fully enabled. One caveat: a flush changes layout, so if the bug is layout-dependent it may move. That is why the static dump of the *currently frozen* cache must come first.

On question 6: order of inspection should be the write history of `0x0300237c` first, because it bisects the entire hypothesis space in one experiment; then the static dump and verification of the frozen cache; then the IRQ-return and `bx`-return resolution path (exact-PC versus containing-block, and whether the indirect lookup cache keys on exact guest PC plus Thumb bit). The `STRH` lowering at `0x080007d4` is last, for the bit-2 reason above.

On question 7: the MIPS backend invariants worth asserting at every RV32IM chain boundary are, in prose: guest registers reside per the fixed convention at every block entry and exit; flags are in canonical slots at every exit reachable by a chain; the cycle counter is decremented and checked at every emitted branch including intra-block loops; direct patched branches target block canonical entries only; every patch write is followed by the full writeback-plus-invalidate sequence for the memory the code executes from; indirect control transfers always resolve through lookup with the Thumb bit honored; and the update path never falls through inline.

On question 8: see question 1 — in stock gpSP the issue cannot arise because internal targets get their own blocks. Any port feature that makes it arise must materialize cycle and flag state independently at the gate, and I would treat every such gate as guilty until proven otherwise.

## On the harness

Your §15 self-assessment is correct: the current `PASS` is meaningless for this ROM. One addition: make the oracle exactly "bit 0 appears at `0x0300237c` within N frames of the wait-function entry at `0x080008ac`, and PC leaves `0x080008c6`." That is the tightest possible game-specific gate, it needs no title-state heuristics beyond reaching the wait function, and it directly encodes the failure. Also note that qemu-user cannot reproduce ESP32 cache-coherence bugs, so a QEMU-green result exonerates the emitter and patcher logic but not the hardware patch-sync path; you need the on-device static cache dump for the latter either way.

*Thinking...*

> I'm looking through the appendix to identify actual bugs in the code. The main area I need to examine is how `riscv_jit_run_block` handles alerts and interrupt checking, particularly the order of operations when `CPU_ALERT_IRQ` is involved.
> 
> Looking at the control flow: after `update_gba()` changes the PC (via bit 30), the code returns `changed_pc` in the update result, then `cycles_to_run()` masks this result. The issue is that `cycles_to_run()` appears to mask only the low 15 bits (like the MIPS reference does with `and reg_cycles, $2, 0x7FFF`), which means the `changed_pc` bit gets stripped out before `riscv_cycles_remaining` is set. This causes the PC change to be lost when determining how many cycles to run next, breaking the relookup logic that should follow a PC change.
> 
> The real problem emerges in the interrupt handling path: when `riscv_handle_cpu_alert()` calls `check_and_raise_interrupts()`, it performs IRQ entry directly—changing `reg[REG_PC]` to 0x18 and switching to IRQ mode—but this happens in C code after the generated block has already returned through the tail sequence. The tail doesn't flush the mapped guest registers (a3..a7, s1..s11) back to memory before returning, so any register state modified by the generated code remains in RISC-V registers rather than being written back to the guest register array.
> 
> The outer loop reloads all mapped registers from memory on the next iteration, which works fine for normal block-to-block transitions. But the indirect lookup tail jumps directly to another generated block without reloading—it assumes the mapped registers are still live in host registers and hold correct values. The problem is that if an interrupt fires between blocks, the C-level interrupt handler modifies `reg[REG_PC]` and mode without knowing about the stale host register state, potentially causing the next block to execute with inconsistent register values or to jump to the wrong address. The indirect lookup tail chains when cycles remain positive, no alert fires, the CPU isn't halted, and the PC doesn't match the idle loop target—mirroring the design intent. The critical hazard is whether the indirect lookup cache gets invalidated when translation caches are flushed on self-modifying code; if not, `t6` could point to freed or reused generated code, though the ROM-based wait loop makes this less obvious as the culprit. The cycle check (`bge zero, t4, .Lslow`) correctly branches to slow path only when cycles drop to zero or below. The real question is how interrupts get checked in fast mode—`flag_interrupt` is set from `update_gba()` during video events, which then triggers `check_and_raise_interrupts()`.
> 
> When fast chaining exhausts cycles, it exits to the slow path, stores the PC, calls the helper, re-enters `riscv_jit_run_block`, and reaches `update_gba()` where VBlank IRQs are delivered and the new PC (0x18 BIOS) is looked up. The wait loop at `0x080008c6..0x080008ce` is a tight internal branch with a short patch site, and when emitting the cycle exit with `flush_before_patch=true` and `short_patch_site=true`, the code flushes dirty state, loads cycles into `t4`, branches over the patch site when cycles are exhausted, emits the short patch (the jump to the internal target), then patches the cycle exit branch to point right after the patch site.
> 
> The cycle count lives in a stack slot that gets decremented each iteration, and when it drops to zero or below, the loop exits to the slow path which flushes, invalidates, stores the target PC, and calls the helper. The real issue emerges in the IRQ return path: after the callback returns via `pop {r0}; bx r0` back to the BIOS IRQ handler, the handler executes `subs pc, lr, #4` (SPSR restore) which is handled by `RISCV_STACK_HELPER_EXECUTE_SPSR_RESTORE`, and this mode switch from IRQ to System swaps the banked registers (r13, r14) in the register file, leaving the mapped host registers `s9` (guest SP) and `s10` (guest LR) stale after the mode change.
> 
> The C helper convention preserves `s`-registers as callee-saved across calls, but the actual values in memory get rewritten by the mode swap — the host registers still hold the old IRQ-mode values. After SPSR restore, the guest r13/r14 mappings and the NZCV mapping (s11) are invalid since the CPSR changed. The question is whether the SPSR restore path reloads these registers before continuing, or if it chains directly to the target block via the indirect lookup tail with stale mappings from IRQ mode.
> 
> If the emitter only invalidates caller-saved bits after the helper call (following the memory-read convention), the valid mask wouldn't be full, so it would take the normal tail path and reload. But if the emitter then reloads the caller-saved registers from memory afterward — like the store path does — that would mark them valid again and make the valid mask full, triggering the indirect tail chain with stale s9 (SP), s10 (LR), and s11 (NZCV) still holding IRQ-mode values. This would explain why the wait loop at 0x080008c6 executes with a corrupted r2 mapping.
> 
> Actually, reconsidering: the loop uses only caller-saved registers (a3..a6), so stale s-regs shouldn't break it. The real issue must be that bit 0 isn't set in memory or gets cleared before the loop checks it. Looking at the fast store path: it flushes dirty registers, calls the store helper directly, invalidates caller-saved, then reloads them. The store helper writes to IWRAM data and checks shadow tags, which should be consistent with the C path. The store returns an alert status, and the emitter branches on that result.
> 
> Now examining the alert branch emission more closely: it's using a linked-list-in-immediates pattern where each branch instruction's offset field encodes the previous chain member's offset, forming a chain that gets patched during finalization to point to the helper tail. The patching only happens if the block is marked as native-supported and there's actually a helper tail to patch to.
> 
> Looking at the `terminal_at_end` computation, it's comparing `end_delta_thumb` (which appears to be repurposed pre-finalization to hold the host offset of the terminal instruction divided by 2) against the current host code pointer. This is a bit of an idiom — using a guest-PC-derived field name to temporarily store host offset information before finalization sets the actual guest range. The store alert path ties into interrupt checking, where IO stores to interrupt-related registers return CPU_ALERT_IRQ.
> 
> Now examining the biggest issue: external branch targets with flush_before_patch assume all 16 mappings are valid and clean on entry, since they're independently translated. But the internal short patch case is trickier — it patches to a recorded host address within the same block, relying on compile-time state propagation. The problem is the mapped-state model at the branch source may not match what's actually live at the target instruction, since they're compiled as part of the same block but with potentially different register allocation states.
> 
> Looking at the polling loop block starting at 0x080008c6: it's both embedded within block 0x080008ac..0x080008d4 and exists as a separate translation. Before reaching 0x080008c6, there's a memory read helper that invalidates caller-saved mappings, followed by data processing through mapped registers. When a mapping becomes invalid, the emitters need to reload from memory and mark it valid again.
> 
> The core issue is that `riscv_emit_native_thumb_conditional_branch` saves and restores fallthrough masks around the taken path, but `riscv_emit_branch_patch_site_with_cycle_exit` doesn't reload invalid mappings at internal branch targets. If the target instruction was compiled assuming a register is live in a host register but the branch-in path has that register's host copy stale, that's a correctness bug. The emitter must reload any mappings that are valid at the target but invalid at the branch source — or ensure the target model is the intersection of all in-edges, which requires a fixpoint approach rather than a single forward pass.
> 
> Now looking at the specific symptom: the callback block starting at `0x080007c2` loads a value into r1, performs some memory operations that could invalidate registers, then uses r1 in an OR operation. The critical issue is whether r1 still holds its original value after those intervening loads and stores, or if the register allocation model has lost track of it.
> 
> The store path flushes dirty registers before calling the helper and reloads caller-saved registers afterward, so r1's value should be preserved in memory. But the read helper calls only flush dirty caller-saved registers without reloading afterward, leaving the mappings invalid. When the subsequent data processing instruction tries to use r1, it should detect the invalid mapping and reload from memory where the flushed value sits. The final store operation then maps the result register and writes it back, but something in this chain of invalidations and reloads is causing bit 0 to never get set.
> 
> The real issue likely surfaces at **join points** where control flow merges or across block boundaries. At patch sites where external chains enter, all mappings should be valid and clean, but the indirect lookup tail (`jalr zero, t6`) jumps into a target block that was compiled assuming a full valid mask with no dirty state. When the BX emitter stores to REG_PC, it flushes all dirty mappings, which should sync everything to memory before the jump.
> 
> But here's the bug: the BX emitter stores REG_PC first (flushing dirty state), then loads CPSR from memory to update it. The problem is that the live NZCV bits are packed in s11 (mapped and possibly dirty), and while `riscv_emit_mapped_regs_flush_dirty` handles flushing NZCV separately via `riscv_emit_live_nzcv_flush`, the order of operations might leave NZCV out of sync with what gets written back to the CPSR register.
> 
> Now looking at the conditional branch emitter more carefully — it saves the valid and dirty masks before the taken path, emits flushes on that path, then restores the masks for the fallthrough. The issue is that the flush on the taken path modifies the global `riscv_mapped_dirty_mask`, which could cause state inconsistency if not properly restored.
> 
> For internal branch targets, the taken path flushes dirty registers and jumps to the target offset. The target block was compiled with some expected model state, but if the incoming edge has a different valid mask than what the target expects, the target might read stale host registers. In the wait loop example, after a helper call invalidates caller-saved registers, the next instruction needs r3 — if the mapping shows r3 invalid, a reload happens, but if that reload doesn't properly mark r3 as valid, the subsequent operations using r3 will fail. The conditional branch at 0x080008ce jumps back to the block start, which was compiled assuming full initial validity, creating a potential mismatch. So r2 and r4 stay invalid at the branch point, but the code assumes r2 is valid when it does the move from a5 — except a5 is caller-saved and gets clobbered by the read helper call. After the loop-back, that stale a5 becomes the load address, which should cause garbage reads and random loop exits. Yet the observed behavior is stable looping, which suggests something's wrong with my analysis of the register state tracking across the C call boundary. So maybe the initial model at block entry assumes nothing is valid, and registers get reloaded lazily from memory as needed—but that contradicts the explicit comment that external targets assume all mappings are live. And if the initial state is FULL valid, then the internal loop-back should hit stale a5 after the read helper clobbers it, yet the loop reads the correct address consistently. I'm realizing the block transitions in the records show dispatcher involvement even for what looks like an internal loop, so maybe the loop-back isn't actually internal after all.
> 
> Actually, checking the internal jump condition more carefully: the beq target 0x080008c6 equals the block start, so it should qualify as internal and use the direct patched edge. But if that's the case and the loop runs millions of times through that patched path, then a5 shouldn't be stale—which means either the address was constant-folded somehow, or the memory read helper uses a fast assembly path that preserves the caller-saved registers instead of clobbering them.
> 
> Looking at the fast read helper for IWRAM: it's hand-written assembly using only t0-t2 and a0, which means a3 through a7 should survive. The model conservatively invalidates them anyway to account for potential C fallback on non-RAM reads, but in practice for IWRAM the registers stay intact. So the loop does work despite this model-reality gap being benign for fast paths.
> 
> Now back to the actual bug—I need to focus on what changed after Start and why the flag never gets set. Let me re-examine the callback tail block and that orrs instruction. Looking at the guest code around 0x080007c6 through 0x080007d4, I see r1 gets set to 1, then ORed with flags, then stored. Something between those instructions must be clearing r1's value.
> 
> When the store at 0x080007ca executes, the fast-store path flushes r1 (dirty, value 1) to memory, calls the store helper which clobbers the a3-a7 host copies, then reloads a4 from memory to restore r1—that should work correctly. But what if the store triggers an SMC alert? The address 0x03007ff8 is in IWRAM near the stack and IRQ vectors. If translated code exists in that region and the SMC shadow tag is set for that address, the store would raise an alert, branch to the terminal helper, flush the translation cache, and relookup. That would happen every VBlank and be expensive but correct, though it would still continue execution afterward.
> 
> The real question is what PC the dispatcher resumes at after the alert branch. The store helper sets REG_PC to pc + 2 (the next instruction at 0x080007cc), so the dispatcher looks up and continues from there—that's the correct design. However, there's a potential bug with dirty mapped registers: when the alert branch jumps to the terminal helper tail, all dirty registers were already flushed before the store call, and caller-saved registers were reloaded afterward. But the callee-saved mappings remain valid and clean since the flush_dirty operation flushes all dirty registers, not just caller-saved ones.
> 
> At the alert branch point, everything is clean, so the dispatcher reload is fine. NZCV is also flushed. The indirect lookup tail checks the compile-time valid mask at emission, and when the alert entry occurs, the host registers are all valid. The tail itself explicitly checks for pending alerts with a branch to the slow dispatcher, which handles SMC flushes correctly.
> 
> Now I'm shifting focus to what happens when an IRQ returns to the interrupted wait loop in fast mode—specifically, what PC value is saved in the IRQ link register when check_and_raise_interrupts stores reg[REG_PC] + 4.
> 
> For a Thumb interrupt, the hardware sets LR_irq to the address of the next instruction plus 4, and the BIOS returns with SUBS PC, LR, #4. So if reg[REG_PC] is 0x080008c6 (the next instruction to execute), then LR_irq becomes 0x080008ca, and the return lands back at 0x080008c6—standard behavior.
> 
> For SPSR, the current CPSR is saved, and I need to verify whether the T bit and NZCV bits are up to date in the fast path. The T bit is tracked in CPSR memory, so that's current. The NZCV bits live in the s11 mapping, and at cycle exit the flush_dirty routine flushes them if they're marked dirty.
> 
> In the branch patch site with cycle exit, the slow path either flushes before the cycle check (flush_before_patch=true) or flushes on the exit path itself (flush_before_patch=false), but either way NZCV gets merged into reg[REG_CPSR] before storing the PC.
> 
> The BIOS IRQ handler at 0x18 saves registers and jumps to the handler at 0x08000738 via an indirect load. After the handler returns, the SPSR restore helper executes—a C function that restores CPSR from SPSR, switches back to System mode, and returns the adjusted PC for Thumb. The generated code then stores REG_PC and terminates, dispatching back to the lookup at 0x080008c6 with Thumb mode active.
> 
> In fast mode with indirect lookup tail, after the SPSR-restore helper call, the callee-saved registers (s9 for SP, s10 for LR, s11 for NZCV) are marked valid but actually stale because set_cpu_mode swapped the register banks in memory and CPSR changed. If the tail then indirect-chains to the wait-loop block with these stale values, the stale NZCV doesn't matter since the loop immediately overwrites flags with ands/cmp, and the stale SP doesn't cause issues until the loop exits with pop {r0}; bx r0—but the loop never exits because the flag is missing. The staleness still doesn't explain the missing bit 0, which is set inside...
> 
> Writing lookup cache... The lookup proceeds normally when cycles remain, but if update_gba completes a frame, it returns NULL and zeroes cycles, exiting the outer loop. The idle-loop optimization checks if the PC lands on the idle target and zeroes cycles to trigger an immediate update—this helps rather than harms performance. Direct patched chains skip the idle check since the loop burns cycles internally anyway. Now I'm looking at translation gates, which the game database defines per ROM to mark specific PC targets where the JIT should pause and re-evaluate rather than chain further.
> 
> For the conditional branch bookkeeping, scan_block records branch targets into block_exits while emit walks through the same slots in lockstep, filling in the branch source patches. The BL macro explicitly consumes a slot, and riscv_branch_patch_internal reads the target that scan recorded for the current exit—scan and emit must process exits in the same order for this to work correctly.
> 
> Looking at which Thumb opcodes push exit slots during the scan pass: only static-target branches increment the position counter, while indirect branches like bx or pop pc skip slot allocation. The emit pass has thumb_conditional_branch pushing a slot when native emission succeeds, but if native emission fails and falls back to the interpreter helper, I need to check whether that fallback also consumes the slot or if there's a desync there.
> 
> Now examining the BL pair instruction more carefully — the calculation computes the target by combining the high and low offset halves, but the key issue is what `pc` points to during emission. In gpSP's translation loop, `pc` gets incremented after loading each opcode, so when processing the second half of BL, `pc` has already advanced to point two bytes past the second half. This affects the target calculation since the formula assumes a specific `pc` value at the time of processing.
> 
> Looking at the actual breakpoint data: a BL at 0x080007be correctly sets the link register to 0x080007c3, which matches the expected pattern of (second_half_address + 2) | 1. This confirms the `pc` convention is treating it as the second-half address before advancing, making both the target and link calculations consistent with observed runtime behavior.
> 
> For the BLH standalone case, the sequence loads the link register first, then updates the program counter using the low offset, and finally sets the link register to the post-increment address.  The critical issue is whether the terminal helper call flushes dirty register state — if it doesn't flush the updated link register, the dispatcher's reload from memory would lose that update. The paired BL case avoids this by storing the link register before updating the program counter, which triggers a flush of dirty state. The BLH ordering reverses this, so it depends entirely on the terminal helper call flushing dirty registers before dispatch.
> 
> For BX, there's a potential T-bit loss: if the CPSR store only updates the mapped NZCV bits (s11) and defers the full write to memory, then the flush operation might only merge back the NZCV bits, leaving the T-bit change stranded in the register mapping and never reaching memory. This would silently corrupt mode switches between Thumb and ARM.
> 
> But stepping back: the symptom is a frozen loop at 0x0004 in fast mode, while VBlank IRQs fire ~60/s (confirmed by LCD frame progress and frame counter increments). The handler runs every frame yet the flag never gets set. The handler does acknowledge IF (clearing it to 0x00) and writes to the BIOS mirror at 0x03007ff8. Looking at IE=0x0005, that's VBlank (bit 0) plus Timer 0 (bit 2), not VCount—so the interrupt source might be Timer 0, not VBlank.
> 
> The flag word has bit 2 set, which corresponds to VCount in the game's interrupt flag scheme, suggesting the VCount handler fired at some point. But the wait prologue only clears bit 0 once at entry, leaving bit 2 untouched, so I can't tell if bit 2 is being refreshed each frame or is just a stale leftover. The actual VBlank flag path gets set in the game's VBlank callback (likely at 0x08000738, which is either IntrMain or the VBlankIntr handler itself, called from IntrMain via a vector table).
> 
> Looking at the tail end of this callback, there's a straight-line sequence of instructions starting around 0x080007c2 that loads values, sets r1 to 1, performs some OR operations, and writes to memory addresses like 0x03007ff8 (BIOS_INTR_FLAGS for IntrWait) and 0x0300237c. The BL instruction at 0x080007be happens before this block, so the return lands in the middle of this sequence. For bit 0 to be missing from the flag word, either r1's value gets lost before the final write at 0x080007d4, the write targets the wrong address, the entire block never executes because the dispatcher chain to 0x080007c2 is broken, or the value gets clobbered later.
> 
> The return path from the callee at 0x0800dc40 uses `bx r0` with r0 = 0x080007c3, which sets REG_PC to 0x080007c2 and keeps the Thumb bit set. In fast mode, this triggers either a dispatcher translation of the block at 0x080007c2 or an indirect cache hit that jumps into it—either way, the block should execute. The trace metadata caveat about internal entries doesn't change this: a lookup at 0x080007c2 creates its own translated block since gpSP uses exact-key hashing.
> 
> Now I'm looking at the cycle accounting in the memory access and conditional branch handling—loads add 2 cycles, stores add 1, and setting `*cycles_emitted = true` resets the frontend's cycle counter.
> 
> The conditional branch emitter calls `riscv_emit_arm_condition_branch` with the inverted condition and uses the NZCV mapping in s11. On the fallthrough path (condition false), cycles are already accounted for; on the taken path, a cycle exit patch site is inserted.
> 
> But there's a potential issue with the continuation-state restoration when `flush_before_patch=true`: the mapped registers are flushed to clear the dirty mask, then the continuation masks are saved—so `continuation_dirty` ends up as 0. However, the actual fallthrough runtime path might skip those flush stores, creating a mismatch between what was accounted for and what actually happens.
> 
> The sequence is: adjust cycles, emit condition branch, save fallthrough masks, call the patch-site function (which emits flush+checks+patch on the taken path only), then restore masks. Inside the patch-site function, the flush clears the global dirty mask and saves continuation masks for its own restore. But the outer emitter then restores the fallthrough masks (which may have nonzero dirty), overriding the inner state—this is correct for the fallthrough path since those stores were skipped at runtime.
> 
> For unconditional uses of the patch-site function, though, the inner restore sets dirty=0 while the runtime path to the sequel might have a different actual state, creating another potential join problem if an incoming edge has mismatched register state.
> 
> Now I'm realizing the most critical gap: the indirect tail jump never checks for pending interrupts via `cpu_has_interrupt()`—it only checks the `riscv_cpu_alert` byte. IRQs from update_gba are handled internally, and IRQs from IO stores set the alert flag and trigger the dispatcher. But the HALT path is interesting: this game uses a custom flag loop for IntrWait rather than a SWI Halt, and it actually copies its IRQ dispatcher to IWRAM and jumps there, so the dispatcher runs from RAM code.
> 
> The flag word at 0x0300237c is IWRAM data sitting near the IWRAM code region, but it's distinct from the IntrMain code itself, so writes to it don't trigger SMC alerts. However, here's the real problem: if the game regularly modifies code in the IWRAM region—like updating a jump vector—the indirect lookup cache could hold stale entries that don't get invalidated.
> 
> Looking at the IWRAM dump, 0x03002378 contains what appears to be a callback pointer (0x0800b4f5 in Thumb), and 0x0300237c holds flags. This matches Pokémon's gMain structure. The question is whether `flush_translation_cache_ram()` actually bumps the indirect lookup cache generation counter—if it doesn't, stale indirect tail entries could jalr into freed translation memory and execute garbage, but the system's been stable for millions of blocks, so that's probably not it. The real question now is whether the VBlank callback even gets invoked each frame in fast mode, or if something in the IRQ flow is preventing it. The cycle-exit path flushes state before the branch, then invalidates the register model afterward, so when the helper call happens there's nothing dirty left to flush — the NZCV flags were already written out. But the real issue is what callback gets invoked after Start: the game switches to a different VBlank handler (0x08000738), and that's the one that's mysteriously not being called in fast mode, whereas the pre-Start callback worked fine during the title animation.
> 
> The new callback and its callees get freshly translated when Start runs, and with a heavy per-frame callback doing audio engine work and spawning multiple subcalls, some conditional path in fast mode with chaining might diverge and never return to the tail — likely due to stale NZCV flags across an internal chain. External chains do reload NZCV at patch sites, so the issue is probably an internal short chain where the compile-time flag state doesn't match what actually happened at runtime.
> 
> The real smoking gun is that internal branch targets assume the sequential compile-time valid mask, but a jumping-in edge might have a different valid mask — and if the target instruction uses a register that's valid at compile time but invalid at the branch source, it'll read stale data. Store helpers are C calls that clobber a3–a7, so any block with a store followed by an internal branch could hit this: the target assumes those registers are live, but the store path clobbered them. ust uses stale a5 since the target was compiled assuming a5 valid. So the real danger is when a C fallback (IO/ROM read) or store clobbers caller-saved regs, but an internal branch target downstream was compiled with those regs marked valid — the target then reads garbage from the stale host register. The model conservatively marks regs invalid after reads, but if the actual helper preserves them (RAM fast path), that's harmless; the problem is the inverse: model says valid but the register was actually clobbered by an IO read or store that the model didn't fully account for at the branch edge.
> 
> In the interrupt dispatcher scenario, IntrMain reads REG_IE and REG_IF (IO reads triggering C fallback, clobbering a3..a7) inside a loop that scans interrupt bits with internal branches. After the IO read, the loop-back edge carries stale registers into the loop head, which was compiled assuming those regs were valid. The bit-scanning logic then uses corrupted values — potentially setting the wrong flag bit or calling the wrong interrupt vector. The observed 0x0004 flag could be exactly this: IntrMain's bit-index register is stale after the IO read, so it sets bit 2 instead of bit 0, all because the loop-back edge after an IO read jumps to code compiled with invalid assumptions about register state.
> 
> Under forced dispatch, every block re-enters through the outer reload path, reloading all registers from memory each time, which masks the bug entirely. This explains why forced dispatch shows correct behavior at breakpoints while normal mode fails, and why the JIT is frozen while the interpreter works. The title screen likely avoids the bug because its interrupt setup (IE=0x0001, VBlank only) doesn't exercise the loop-back-after-IO-read pattern — the scan finds the single enabled bit on the first iteration with no loop-back needed. After Start, IE=0x0005 enables both VBlank and VCount, forcing multiple loop iterations and exposing the stale-register bug on the loop-back edge, which is why the VCount flag (bit 2) ends up set correctly by accident but through corrupted logic.
> 
> Now I'm tracing through the vblankCallback flow: IntrMain calls it at 0x08000738, and at the tail it should set both 0x03007ff8 and 0x0300237c with r1=1. The issue is whether r1 stays valid across the C call and subsequent memory reads — the model flushes r1 to the register file when the strh at 0x0800ca triggers a C call, then reloads it for the orrs at 0x0800d2, but the real hardware's IWRAM fast path preserves r1 in the register, creating a divergence in how the final strh executes.
> 
> The real puzzle: if the tail block does execute, the store should happen. So in fast mode, either the tail never executes or something clobbers the write afterward — likely the prologue re-executing. After the IRQ returns via SPSR restore to 0x080008c6, the fast path does an indirect cache lookup using that PC, which should find the block and set the flag. But the return address calculation at IRQ entry might be storing the wrong PC — it sets REG_MODE(MODE_IRQ)[6] = reg[REG_PC] + 4, and if reg[REG_PC] was already pointing to a function entry like 0x080008ac instead of the actual cycle-exit address, the return lands in the wrong place and re-executes the prologue, wiping out the vblank flag write.
> 
> The key insight is that during fast chaining, reg[REG_PC] only gets written at helper and exit boundaries, not on every internal loop iteration. So the stored PC value should always be accurate at update time. Now I'm looking at the dead flags issue with the `ands` instruction at 0x080008b4 and how the MOVS immediate case handles flag-dead scenarios. I'm tracing through the flag liveness across loop boundaries — the MOVS at c4 sets flags that are dead because the adds instruction at c8 redefines them before any branch consumes them. The backward edge from beq back to c6 doesn't create a problem since flags are dead at that point until adds redefines them. For the adds instruction itself, I need to verify the encoding: the hi byte 0x1c maps to ADD with immediate, and the operand is r3 + 0, which sets flags that flow into the ands and cmp instructions downstream.
> 
> Now I'm checking the store path semantics — when a store instruction passes pc+2 to the register, that's the address of the next instruction, which is correct if the SMC alert resumes execution there after the store completes. The pc convention throughout the emit loop treats pc as the current instruction's address, so pc+2 consistently points to the next instruction. This aligns with how BL instructions handle the second-half address.
> 
> I'm also verifying the alignment handling in the store alert branch emission — misaligned stores or those with offsets too large trigger a full helper call inline, and since the generated code uses 4-byte units with 8-byte metadata, offsets stay properly aligned even with RVC relaxation at runtime.
> 
> There's a potential confusion in the NO_FALLTHROUGH block finalization logic where a terminal instruction might be emitted mid-code rather than at the end, but it doesn't appear to be clearly incorrect. The real concern I need to examine is how the indirect tail cache works after an IRQ changes the PC — when riscv_lookup_or_fallback caches the entry using the new PC as the key, I need to verify this doesn't create a mismatch in the caching contract.
> 
> Now I'm tracing through a specific scenario: a block ending with a PC write to a target address gets interrupted by an IRQ that changes the PC to the BIOS vector, the tail block caches this new entry, and then when the IRQ returns to the original target address, the tail block executes correctly. The interrupt entry itself is handled through check_and_raise_interrupts, which sets the resume PC to the post-store instruction and properly swaps CPU mode banks.
> 
> I'm also examining the case where the game's interrupt handler writes to REG_IF to acknowledge interrupts — this triggers a CPU_ALERT_IRQ that flows through the dispatcher to check_and_raise_interrupts again, potentially allowing nested interrupt entry if another IRQ is pending and the handler has re-enabled interrupts in System mode. This is architecturally sound, but I want to isolate a specific visible bug I spotted earlier involving the memory read call stack emission.
> 
> Looking at the memory store call stack function when fast stores are enabled — it flushes dirty registers and calls the helper through the stack table, which may invoke a C function that clobbers the caller-saved registers (a3 through a7, mapping to guest r0 through r4), but then returns without invalidating or reloading the model for those registers. The model still thinks those registers are valid, so subsequent instructions emit moves from the stale register values, producing garbage. The known variant of this function correctly invalidates and reloads after the call, making this a genuine bug.
> 
> Now I'm tracing where the unknown variant gets used — through the stack helper macros for 32/16/8-bit stores, which appear in ARM block memory operations, single stores, and Thumb push/pop instructions. Thumb push/pop are particularly concerning since they go through the block-memory helper path with the buggy flush-and-call-without-invalidate pattern. After a push instruction, the caller-saved registers are clobbered but the model thinks they're still valid, so any code using r0 through r4 afterward reads garbage from the host registers, corrupting the callback's computation. Though pop instructions use the load path which does invalidate correctly, the push at the callback entry is the problem — and the following instructions in that block start with a fresh reload contract, so they don't immediately expose the corruption.
> 
> The real issue surfaces in functions that push registers and then immediately use the argument registers — like `push {r4,lr}; movs r4, r0` — where r0 should still be valid but the model has already marked it as clobbered.  The fast store path actually routes through assembly helpers that preserve those registers on the RAM path and only clobber on the slow C fallback for IO/ROM/SMC addresses, so the missing invalidation is mostly harmless except on those rare slow paths — exactly the kind of bug that lets the game reach the title screen but crashes on specific flows.
> 
> The other store path using `riscv_emit_memory_store_call_stack_known` calls the C function directly and correctly invalidates, so the two paths diverge here. If IntrMain's IO store to REG_IF goes through the stack-table path, it hits the slow C fallback which clobbers a3..a7, leaving the generated code with stale register state while the model thinks those values are still valid — corrupting the interrupt handler's execution. So the tail block at 0x080007c2 that sets the flag uses only IWRAM accesses with straightforward inputs—r0 from a pool constant and r1 as an immediate 1—so if it executes with the right entry conditions, it should work correctly. The question is whether the epilogue block that branches into it via indirect tail actually maintains valid register state at the point of emission.
> 
> But here's the catch: even if the epilogue had clobbered registers from an earlier slow store or IO access, the tail block itself redefines every register it uses before reading them—it loads r2 from the pool, reads into r0, sets r1 to 1, then operates on those fresh values. So the tail block's own logic is sound.
> 
> The real corruption likely happens earlier: a conditional branch before the tail decides whether to even execute it. If an IO store like the IF acknowledge corrupts the flags or values feeding into conditionals at 0x080007be or 0x08000782, the callback could branch away entirely and skip the flag-setting tail, leaving the interrupt unhandled.
> 
> Under forced dispatch, the outer loop reloads values for each block, so conditionals resolve correctly and the tail executes. Under fast chaining with stale "valid" bits after a slow store, the conditional takes the wrong path and skips the tail—which explains why the bit never gets set.
> 
> So the core issue is that `riscv_emit_memory_store_call_stack` doesn't invalidate caller-saved register mappings after a call that can reach C on the slow path, especially for IO stores that trigger the slow fallback. The model incorrectly marks a3–a7 as valid across a clobbering call, and sequential code trusts those stale bits. The interpreter and forced dispatch both avoid this because they reload state per block, but fast chaining exposes the hole.
> 
> One more thing to verify: whether the asm slow helper itself preserves a3–a7 around the C call.  If it does, the hole closes. But `riscv_emit_memory_store_call_stack_known` calls C directly and still invalidates+reloads, which suggests the C functions do clobber. The slow shims might preserve, but I can't be certain—so the top audit item is to check whether `riscv_fast_store_*_slow` saves a3–a7 around the C call; if not, this is a real hole.
> 
> There's also a second issue with `riscv_emit_memory_read_call_stack`: reads invalidate caller-saved conservatively, which is fine sequentially, but internal branch targets are compiled against the sequential model and only external sites get reloaded to FULL. Internal edges get nothing, so if a target is compiled with fewer valid bits than the source guarantees at runtime, the mismatch can cause wrong behavior.
> 
> The danger surfaces with slow-path clobbers combined with internal branches—like IO polling loops (VCOUNT, DISPSTAT, KEYINPUT) where the read C-clobbers a3–a7 including r4, and a loop-back edge to a target compiled with r4 valid would load garbage on iteration 2. The fix is to reset the model at every internal branch target by reloading invalid registers and flushing dirty ones, forcing a canonical state at the target instruction itself so both sequential and jump-in paths see the same state.
> 
> Finding 3 surfaces a separate issue: the indirect-lookup tail's compile-time gate relies on the emitter's possibly-incorrect valid mask, and blocks ending in SPSR restore (BIOS IRQ returns) can leave host registers stale after mode changes without proper invalidation.
> 
> Finding 4 offers a surgical test: disabling fast RAM stores to route them through the standard calling convention instead — if the game then runs normally in full fast-chaining mode, that confirms Finding 1. It's a one-line toggle that preserves chaining while isolating the root cause.
> 
> Earlier host experiments disabled fast stores without success, but those predated a reliable progress oracle and should be repeated on actual hardware with the flag-bit test. QEMU experiments may have been unreliable, though qemu-user should reproduce Finding 1 identically since it's a pure emitter bug.
> 
> Findings 5 through 7 are minor: the store-alert path resumes correctly after the write, indirect entry caching post-IRQ is benign, and conditional branch cycle accounting charges both paths appropriately. I should also verify the thumb BX instruction handling.
> 
> Now I'm checking `riscv_emit_native_thumb_alu2` for a potential issue where the result register might be written before operands are consumed. Looking at the ADDS/SUBS immediate cases, the imm8 variant correctly loads the destination into t0 first, but the imm3 ADD case (0x18-0x1f range) loads rs into t1 yet appears to have no corresponding emission code in the visible section.
> 
> I should also address their core concerns directly: identify contract violations in the excerpts ranked by severity, with concrete experiments to test them. I need to revise my earlier hypothesis about internal-entry aliasing — the source shows lookups use exact keys and blocks translate fresh at any PC, so that theory doesn't hold. The indirect lookup cache stores exact keys, which is good. The re-clear-by-prologue mechanism seems less likely now. On the `.Lslow` label reachability after a cache miss in the indirect tail, the omitted lookup code presumably branches there on miss. For `riscv_emit_branch_patch_site_with_cycle_exit`, the continuation restore comment indicates it saves masks after the flush, but when called from conditional-branch emitters, the outer code then overwrites with fallthrough masks — that's fine. The question is what happens for unconditional terminal-ish sites.
> 
> Now examining `riscv_emit_terminal_branch_patch_site_with_cycle_exit` — it does the cycle check first, then reloads for external on the hot path after the cycle-exit branch, but the flush happens before the cycle check so memory is synced on exit. The hot path reloads and patches the jump correctly. On the cycle-exit path though, after patching the local branch and calling the terminal helper with no flush, I'm checking whether REG_PC gets stored — the non-terminal version explicitly does a guest_pc_load and REG_PC store on exit, but the terminal version doesn't, relying instead on the assumption that terminal sites already wrote REG_PC via the PC_WRITTEN flag. For a direct unconditional branch target though, PC isn't written on the hot path since that's the whole point of chaining, so on the cycle-exit path there's a gap.
> 
> The terminal helper might emit REG_PC through metadata, but looking at who actually calls this function — probably `thumb_b` unconditional with a static target where the hot path is a patched direct jump and the exit path is the terminal helper. If REG_PC isn't stored on the exit path with the target PC, the dispatcher resumes at stale REG_PC, which could be a real bug causing wrong control flow when cycles expire exactly at an unconditional branch. In forced dispatch mode, external patching is disabled so the patch site never gets patched, leaving an unpatched placeholder that either falls through or jumps to itself. In the terminal variant, the flush happens before the cycle-exit branch so dirty state is saved correctly, but in the non-terminal case with flush_before_patch=false, the hot path jumps with unflushed dirty registers — the target block expects all registers valid but dirty=0, so if a helper later reads from memory (like update_gba or IRQ entry), it gets stale values instead of what's in the host registers.
> 
> This means any call site passing flush_before_patch=false for an external-capable branch would silently lose register updates at the next dispatcher boundary — the symptom would be writes vanishing after an IRQ, which is a serious state corruption bug. I need to audit all call sites of both patch-site emitters to ensure flush_before_patch=true everywhere external chains are possible, and verify the entry contract is enforced.
> 
> Now I'm restructuring the findings: the internal-entry-aliasing lead doesn't hold up (the source shows exact-key matching and fresh blocks, stock behavior), so the real culprit is the mapped-state model lying across helper calls and joins. The entry contract is now clear — all 16 mapped registers must be valid and memory-synced (clean).
> 
> The primary finding is in the fast-store branch of the memory-store call stack: when the fast path executes, caller-saved registers aren't invalidated or reloaded, so the poisoned model persists through the block and feeds into the indirect-tail gate's full-validity check. This means actually-clobbered registers get carried into the next block whose model trusts them as live. It only manifests when the stack-table helper exits the RAM fast path — IO stores like REG_IF ack, DISPSTAT, sound, or DMA registers trigger it, which explains why it's fast-mode-only, forced-dispatch-masked, clean in the interpreter, reproducible in QEMU, and why the post-Start VBlank callback takes the wrong conditional path so the tail never executes. The bit patterns also match: bit2 present, bit0 absent, different code shapes. The discriminating test is whether the fast-store shims save and restore the caller-saved registers around the C call — if they do, the hole closes; if not, this is the bug.
> 
> For the fix, I can either disable fast RAM stores on hardware with the bit-0 oracle, or make the fast branch match the `_known` path by invalidating and reloading caller-saved registers. The better approach is to preserve a3..a7 in the shim itself, keeping the hot path lean since reloading after every store costs five load-word instructions while shim-preservation only adds cost on the slow path.
> 
> The second finding is that internal join reconciliation is missing entirely. The asymmetry is stark — external reloads go to full validity, but internal targets get nothing — and the unsound direction is that a valid target model implies the actual edge state is valid, which isn't guaranteed. Right now it's mostly shielded by assembly fast paths preserving a3..a7, so it only fires for joins downstream of slow-path calls, the same trigger class as the first finding. The fix is to canonicalize the model at every scan-recorded internal target by flushing dirty registers and reloading invalid ones at the target's emission point, or track per-target masks and reconcile at the edge.
> 
> The third finding concerns the entry-contract "clean" dimension: the hot path with flush_before_patch=false crosses chain boundaries with dirty registers. I need to verify all external-capable sites pass true and state the contract explicitly — at any cross-block transfer (patched direct or indirect tail), all 16 registers must be valid, dirty must be zero, and the register array must be synced. The indirect tail additionally requires REG_PC and CPSR to be memory-current, which they are via the PC_WRITTEN path.
> 
> The fourth finding involves mode-change helpers like SPSR restore and CPSR store, and SWI...
> 
> Checking for latent stack corruption... I need to re-run those earlier QEMU tests with a proper oracle since they predate reliable injection, and verify that disabling fast stores actually didn't resolve it — the toggle only matters if the code path honestly invalidates the model. The caller-saved register mask (r0..r4 / a3..a7) is correct, but I should also check whether Thumb PUSH/POP and block memory operations crossing into IO or ROM regions might clobber state, especially around DMA and sound FIFO writes in callbacks.
> 
> The sound engine runs every frame during VBlank, writing IO registers deterministically — that matches the freeze pattern perfectly. Title mode survives because corruption consequences depend on what code runs after the store; if downstream code redefines the register before using it, the corruption stays hidden. The corrupted guest register copies only matter when the model says they're valid, the register gets used before redefinition, and the access goes through a mapped move rather than a memory reload — that's why the freeze is stable and deterministic, not random.

## What the appendix changes

Two of my earlier positions need revision before anything else. First, I proposed internal-entry aliasing (a return to `0x080007c2` resolving to the containing block at `0x080008ac`-style granularity) as a leading mechanism. Section 24.5 rules that out: lookups are exact-key (`pc | thumb`) into the ROM hash, and a miss translates a fresh block starting at the requested PC, exactly like stock gpSP. The indirect lookup cache in 24.4 also keys on the exact aligned PC plus Thumb bit. Retract that hypothesis. Second, the "flag set then re-cleared" theory is now much weaker, because the resume PC after cycle exits is always written explicitly at the exit site, so IRQ return addresses are well-founded.

What the appendix establishes instead is the port's actual cross-block contract, and I believe I can now point at a specific place where it is violated. The contract, assembled from 24.3, 24.4, and the comment inside `riscv_emit_branch_patch_site_with_cycle_exit`, is: **at every cross-block transfer — dispatcher entry, patched external chain, or indirect-tail chain — all sixteen mapped slots must be valid in host registers, dirty must be zero, and `reg[]` must be in sync.** The dispatcher path enforces this by brute force (the outer loop reloads everything). The external patch sites enforce it by flushing dirty and reloading invalid slots. Everything else has to preserve it, and the emitter's `riscv_mapped_valid_mask` is the only witness. Any place where that mask *lies* — claims validity for a host register that a helper actually clobbered — is invisible under forced dispatch (per-block outer reload repairs it), invisible to the interpreter, faithfully reproduced under qemu-user, and deterministic per translation layout. That is your exact phenomenology.

## Finding A: the fast-store convention leaves the valid mask lying

Compare the two store emitters in 24.10. The `_known` variant, used by the Thumb access-memory path, calls the C function directly and then does the honest thing:

```c
riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
riscv_emit_mapped_regs_reload_mask(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
```

The stack-table variant does not:

```c
if (riscv_fast_ram_stores_enabled())
{
  riscv_emit_mapped_regs_flush_dirty(ptr);
  riscv_emit_c_call_stack_raw(ptr, stack_offset);
  return;   /* a3..a7 still marked valid */
}
```

Whether this is a real bug hinges on one question you can answer in five minutes: **do `riscv_fast_store_u8/u16/u32_slow` and the block-store slow shims save and restore `a3..a7` around their C calls?** The asm fast paths shown (`sh` into `iwram+32768`) preserve `a3..a7` by construction, so RAM stores are safe either way. But any store that leaves the fast path — every IO store — reaches `write_memory16` and friends, which are ordinary C and clobber `a3..a7`, i.e., the host copies of guest `r0..r4`. If the slow shims don't save them, the emitter continues compiling with a valid mask that is false, and every subsequent use of `r0..r4` compiled as a mapped-register move reads garbage. Worse, the poisoned mask feeds `riscv_emit_jit_run_block_tail_jump`'s full-validity gate, so the corruption is carried across an indirect chain into the *next* block, whose own entry model trusts all sixteen slots.

Now map this onto the ROM. The post-Start VBlank callback is not the four-instruction tail; the trace shows it calling into `0x080334e0`, `0x0806f050`, `0x0828e6ac` — this has the shape of a sound-engine main call plus OAM/BG register transfer work, which means **IO stores every single frame inside the callback**, upstream of the two conditionals at `0x08000782` and `0x080007be` that gate the path to the tail. A stale `r0..r4` value feeding one of those compares flips it the same way every frame — deterministic, stable, and the tail at `0x080007c2` simply never runs. The tail block itself is immune (it redefines every register it uses from a literal pool and an immediate before use), which is consistent with your instruction-level checks finding nothing wrong there. The title screen surviving is not evidence against this: the consequence depends entirely on whether corrupted registers are consumed before redefinition in the specific downstream code shape, and the title-mode callback is different code. Bit 2 surviving while bit 0 doesn't falls out the same way — different handler shape, different luck.

This finding also explains every observer effect you documented. Forced dispatch masks it because the outer loop reloads `a3..a7` from `reg[]` at every block boundary, and the flush before the store call put the correct values there. The interpreter is untouched. qemu-user reproduces it, because it is a pure emitter-model bug with no hardware cache component — matching your historical differential signal.

The discriminating experiment preserves chaining completely: flip `riscv_fast_ram_stores_enabled()` to false, nothing else, and run on hardware against the bit-0 oracle. Your §16 table says fast-store disabling was tried in the old QEMU runs, but those predate a valid oracle and frame-based Start injection, and you flagged them yourself as needing repetition — this is the first one to repeat. Before even that, read the slow shims. If they don't preserve `a3..a7`, the cheapest correct fix is to make them preserve (save/restore only on the slow path, keeping the RAM hot path untouched), rather than paying a five-load reload after every store at every emit site.

## Finding B: internal join points have no state reconciliation at all

`riscv_emit_branch_patch_site_with_cycle_exit` reloads `RISCV_MAPPED_REGS_MASK & ~valid` for external sites — the comment even names the invariant — but for `short_patch_site` (internal) targets it emits nothing. Internal targets are patched to `block_data[n].block_offset`, code compiled against whatever the *sequential* model happened to be at that instruction. The unsound direction is: target compiled while slot X was modeled valid, entered via an edge on which X's host copy was actually clobbered. Take your own wait loop: the block at `0x080008c6` is compiled with the full-valid entry model, so instruction zero emits `mv a0, a5` for the `ldrh r1, [r2, #28]` base. The read helper on iteration one invalidates `a3..a7` in the model, `r2` is never touched again, and the loop-back edge jumps to code that trusts `a5`. The only reason this works today is that the IWRAM fast-read asm preserves `a3..a7` in reality even though the model says invalid — correctness by accident. The moment a slow-path call sits on a looping path (an IO poll loop, a KEYINPUT wait, DISPSTAT spin), the same structure executes with a clobbered base register on iteration two.

The sound fix is to canonicalize at joins: during emission, whenever the current instruction's guest PC appears as a `branch_target` in `block_exits` within the block range, emit a flush-dirty plus reload-invalid at that point, restoring the all-valid/all-clean state. The sequential path executes it harmlessly, and every jump-in edge (which already flushed dirty at the branch) lands on a canonical state. This is a single-pass, layout-stable fix and avoids per-edge mask tracking entirely.

## Finding C: verify the "clean" half of the chain contract

The hot patched path in `riscv_emit_branch_patch_site_with_cycle_exit` only flushes dirty when `flush_before_patch` is true. If any external-capable call site passes false, the chain crosses with dirty host registers, the target block's entry model says dirty-zero, and the update is silently lost at the next dispatcher boundary — the outer loop reload overwrites the newer host value with stale memory. The conditional-branch emitter passes true; audit every other call site of both patch-site emitters. While there, confirm the terminal variant's cycle-exit path: unlike the non-terminal version, the excerpt shows no `REG_PC` store on that path, which is fine only if every caller has already stored PC (the BL/BLH/BX emitters have) — if a plain unconditional `B` uses it without a prior PC store, a cycle exhaustion landing exactly on that branch resumes at a stale PC.

## Finding D: mode-changing helpers and banked mappings

`s9` and `s10` mirror guest `r13`/`r14`, which `set_cpu_mode` rebanks in `reg[]` behind the JIT's back. After the SPSR-restore, CPSR-store, and SWI helpers, the emitter must invalidate at least `r13`, `r14`, and the NZCV slot (and `r8..r12` if you ever care about FIQ), otherwise a block ending in `subs pc, lr, #4` that qualifies for the indirect tail chains into System-mode code carrying the IRQ-mode SP in `s9`. That specific failure would not block the flag — your loop doesn't touch SP until the `pop {r0}` at exit — but it means the *first thing that happens after you fix Finding A* is a stack corruption at loop exit. Check it now, in the same audit.

## A systematic detector that doesn't perturb chaining

There is a build-time experiment that converts every "valid mask lies" bug into an immediate loud failure without touching chain topology: in a debug build, make every stack-table helper — including the asm fast paths — poison `a3..a7` with a recognizable constant immediately before returning. Under the intended contract this must be behavior-neutral: reads invalidate the model afterward, the `_known` store path reloads, and callee-saved slots are untouched. Any divergence from baseline behavior identifies a site relying on preservation the model doesn't guarantee. This will catch Finding A on the boot screen, and it will also flush out any remaining sites (ARM-side store emitters, block memory, swap helpers) with the same hole, which grep alone might miss. It is strictly better than the category-mask toggles that gave you conflicting results, because it changes no layout, no dispatch mode, and no cycle behavior.

## Revised order of work

Read the slow-store shims for `a3..a7` handling first; that is likely the whole answer. If the hole is real, fix it in the shims, rebuild with full chaining and no debug instrumentation, and run the hardware oracle: bit 0 appears at `0x0300237c` within a frame of wait entry and PC leaves `0x080008c6`. Then run the poison build to sweep the rest of the convention. Then land the internal-join canonicalization from Finding B regardless of whether it is implicated here, because it is unsound as written and only shielded by the accidental preservation behavior of the RAM fast paths. Then the Finding C/D audits. The frozen-cache static dump I recommended previously drops to a confirmation step — worth doing only if the shim audit and the fast-store toggle both come back clean, which I do not expect.

One process note: your earlier evidence that the BL call/return at `0x080007be` is correct was gathered under forced dispatch, and under Finding A that evidence is exactly what the bug predicts — correct under per-block reload, wrong under chaining. Nothing in your investigation was wasted; the forced-dispatch observations bounded the bug to precisely the class of state that per-block reloads repair, which is what made it findable in the source.
