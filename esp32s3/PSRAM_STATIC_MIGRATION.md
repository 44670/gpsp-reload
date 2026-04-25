# ESP32-S3 Static PSRAM Migration Report

## Goal

Move the ESP32-S3 port away from runtime heap allocation for gpSP-owned
buffers. The target design uses fixed static buffers placed in PSRAM, including
the JIT translation caches, and uses an instruction alias for executing
generated Xtensa code.

This report is intentionally written before implementation. It defines the
migration shape and the correctness checks needed before the backend should
execute translated blocks from PSRAM.

## Target Constraints

- Target board: M5Stack CoreS3 SE / ESP32-S3.
- PSRAM budget: 8 MB.
- Flash: 8 MB partition / SPI mmap direction for ROM data.
- No M5 library dependency.
- No dynamic allocation for ESP32-S3-owned emulator buffers in the target path.
- JIT caches should be fixed-size static PSRAM buffers.
- Generated code must be written through a writable data view and executed
  through an instruction view.
- Cache synchronization after emission and after backpatching remains mandatory.

## Pre-Migration State

The ESP32-S3 dynarec cache allocation is currently dynamic:

- `libretro/libretro.c` allocates `rom_translation_cache` and
  `ram_translation_cache` with `heap_caps_malloc(..., MALLOC_CAP_EXEC)`.
- This keeps the first backend simple because `MALLOC_CAP_EXEC` routes to
  internal executable memory.
- It is too small for a useful long-running JIT cache.

Some large ESP32-S3 data already uses static PSRAM placement:

- `GPSP_EXT_RAM_BSS` maps to `EXT_RAM_BSS_ATTR` on ESP32-S3.
- `rom_branch_hash` is already `GPSP_EXT_RAM_BSS`.
- `gamepak_backup` is already `GPSP_EXT_RAM_BSS`.
- The Xtensa compiled-instruction metadata arrays are already
  `GPSP_EXT_RAM_BSS`.

The IDF test app already enables external BSS:

- `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`
- `CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION=y`

## Size Reality

The default gpSP cache sizes do not fit in 8 MB PSRAM:

- Default ROM translation cache: 10 MB.
- Default RAM translation cache: 512 KB.
- `SMALL_TRANSLATION_CACHE` ROM translation cache: 2 MB.
- `SMALL_TRANSLATION_CACHE` RAM translation cache: 384 KB.

The ESP32-S3 static target should not use the 10 MB default ROM JIT cache.
Use an ESP32-S3-specific static budget, initially based on the small-cache
profile.

Approximate existing static PSRAM pressure:

- ROM branch hash: 256 KB.
- Xtensa compiled ARM/Thumb metadata arrays: about 2 MB total.
- Gamepak backup: 128 KB.
- Current Xtensa gamepak fallback buffer: 1 MB.
- Three video buffers, if all enabled statically: about 232 KB total.
- Audio sample buffer: small, under a few KB.
- Small-profile JIT caches plus safety thresholds: about 2.4 MB.

This is workable in 8 MB only if ROM data itself is not copied into PSRAM as a
large static 32 MB buffer. The ROM path should use the flash partition / SPI
mmap direction, with a small fixed PSRAM window only if a fallback is required.

## Static Buffer Policy

For ESP32-S3 target-owned buffers:

- Replace heap allocation with `GPSP_EXT_RAM_BSS` static storage.
- Add explicit alignment for JIT storage, at least `CONFIG_MMU_PAGE_SIZE`.
- Do not call `free()` for static buffers. Deinit paths should only clear
  pointers and reset state.
- Build should fail or disable the static PSRAM target if external BSS is not
  enabled.
- Keep static declarations in a small ESP32-S3-owned module rather than
  scattering board-specific arrays across generic frontend code.

Example shape:

```c
GPSP_EXT_RAM_BSS static u8 esp32s3_jit_rom_data[
  ROM_TRANSLATION_CACHE_SIZE + TRANSLATION_CACHE_LIMIT_THRESHOLD]
  __attribute__((aligned(CONFIG_MMU_PAGE_SIZE)));

GPSP_EXT_RAM_BSS static u8 esp32s3_jit_ram_data[
  RAM_TRANSLATION_CACHE_SIZE + TRANSLATION_CACHE_LIMIT_THRESHOLD]
  __attribute__((aligned(CONFIG_MMU_PAGE_SIZE)));
```

The RAM translation tag table lives inside the top of
`RAM_TRANSLATION_CACHE_SIZE`; `cpu_threaded.c` already reserves space for it by
moving `translation_cache_limit` downward as tags are allocated. The static RAM
cache still needs the existing overrun threshold, but it does not need a second
separate tag allocation.

## JIT Cache Address Model

The existing dynarec mostly treats a translation-cache pointer as both:

- the writable byte pointer used by the emitter, and
- the callable pointer returned by block lookup.

PSRAM execution requires splitting those concepts:

- Data alias: writable DBUS address, normally in the `0x3c...` range.
- Exec alias: instruction IBUS address, normally in the `0x42...` range.

Offsets should remain the stable cache identity. A block offset from the base
must be convertible both ways:

```c
data_ptr = jit_data_base + offset;
exec_ptr = jit_exec_base + offset;
```

Recommended ownership:

- Keep emitter writes, `memset`, hash headers, RAM tag metadata, and backpatch
  writes on the data alias.
- Return or dispatch block entrypoints through the exec alias.
- Any emitted literal or absolute host address that points inside the generated
  code region must use the exec alias.
- Any patch location to be modified must use the data alias.
- Platform cache sync should accept data-alias ranges and internally derive the
  matching exec-alias range.

This split is the most important codebase migration. Without it, blocks may be
written successfully but branch targets and returned function pointers can still
point at non-executable data addresses.

## ESP32-S3 MMU Direction

ESP32-S3 has external-memory DBUS and IBUS virtual aliases:

- DBUS base: `SOC_MMU_DBUS_VADDR_BASE` (`0x3c000000`)
- IBUS base: `SOC_MMU_IBUS_VADDR_BASE` (`0x42000000`)
- Linear mask: `SOC_MMU_LINEAR_ADDR_MASK` (`0x01ffffff`)

For a static PSRAM BSS buffer, the data alias is already mapped by IDF. The
first experiment should derive the instruction alias from the same MMU linear
address:

```c
exec = SOC_MMU_IBUS_VADDR_BASE |
       ((uintptr_t)data & SOC_MMU_LINEAR_ADDR_MASK);
```

Because ESP32-S3 uses the same MMU linear address range for DBUS and IBUS, this
should refer to the same MMU table entry as the data mapping. The migration
should verify that each page maps to PSRAM before enabling execution.

If manual register setup is required, keep it scoped to the static JIT buffer
pages. Do not reconfigure the entire PSRAM region. The low-level write is to
the MMU table entry with the PSRAM target and valid bit, but changing entries
must reproduce IDF's important side effects:

- stop or freeze external cache while modifying entries,
- write the MMU entries,
- enable the instruction cache bus for the IBUS alias,
- invalidate affected cache lines,
- resume cache on both cores.

## Cache Synchronization

The JIT sync path must be explicit and range-based:

1. Emit or backpatch bytes through the data alias.
2. Write back D-cache for the data-alias range.
3. Invalidate I-cache for the matching exec-alias range.
4. Only then call through the exec alias.

The existing `platform_cache_sync(last_ptr, ptr)` call sites should remain the
frontend contract. On ESP32-S3, the implementation should understand that the
incoming range is a data-alias JIT range and should sync both data and
instruction visibility.

## Required Hardware Self-Test

PSRAM execution must be gated by a startup self-test before gpSP uses it:

1. Pick a page-aligned location in the static PSRAM JIT buffer.
2. Emit a tiny Xtensa function through the data alias using the existing Xtensa
   emitter helpers where possible.
3. Sync D-cache to memory.
4. Invalidate I-cache for the exec alias.
5. Call the exec alias and verify the return value.
6. Rewrite the function at the same data address.
7. Repeat sync and call the exec alias again.
8. Verify that the second return value is observed.

This checks both first execution and backpatch/rewrite visibility. QEMU can be
used for build and coarse regression coverage, but real hardware is required
before trusting PSRAM executable caches.

## Allocation Audit

ESP32-S3-owned buffers that should move to static PSRAM:

- JIT ROM translation cache.
- JIT RAM translation cache.
- Main video frame buffer.
- Optional post-process frame buffer.
- Optional previous-frame mix buffer.
- Audio sample buffer.
- QEMU/test frame capture buffer.
- Gamepak fallback window, if still needed.

Already static PSRAM or close to target:

- `rom_branch_hash`.
- `gamepak_backup`.
- Xtensa compiled-instruction metadata arrays.
- `xtensa_gamepak_buffer_fallback`.

Dynamic allocation that remains outside this immediate migration:

- Generic libretro-common path/string/VFS helpers.
- Host frontend option conversion helpers.

If the final target requires a strict process-wide no-malloc policy, those paths
need a separate frontend audit. For the CoreS3 SE embedded target, the first
migration should remove dynamic allocation from the steady emulator runtime and
JIT paths.

## Codebase Impact

Expected implementation areas:

- `libretro/libretro.c`
  - Replace ESP32-S3 translation-cache `heap_caps_malloc` with static PSRAM
    cache setup.
  - Replace ESP32-S3 video/audio heap buffers with static storage.
  - Remove ESP32-S3 `free()` calls for those static buffers.

- `cpu.h` / `cpu_threaded.c`
  - Preserve cache offsets as the common identity.
  - Ensure block lookup returns exec-alias pointers on ESP32-S3.
  - Ensure patch locations and metadata writes use data-alias pointers.

- `esp32s3/xtensa_runtime.c`
  - Add data-to-exec and exec-to-data helpers.
  - Add PSRAM JIT self-test.
  - Update cache flush and sync routines for dual aliases.
  - Keep ROM/RAM cache split and RAM invalidation behavior unchanged.

- `tests/esp32s3/idf-app/main/app_main.c`
  - Replace dynamic frame capture with fixed static PSRAM storage.
  - Keep QEMU capture behavior unchanged from the caller's perspective.

- `tests/esp32s3/idf-app/sdkconfig.defaults`
  - Require PSRAM and external BSS.
  - Keep any PSRAM XIP options disabled unless a later test proves they are
    needed. The JIT path should not depend on `CONFIG_SPIRAM_XIP_FROM_PSRAM`.

## Implementation Progress

First migration pass implemented:

- Added `esp32s3/psram_static.c` and `esp32s3/psram_static.h` as the central
  owner for ESP32-S3 static PSRAM buffers.
- Added fixed, page-aligned static PSRAM storage for ROM and RAM JIT caches.
- Enforced the initial ESP32-S3 JIT cache budget at build time:
  - ROM JIT cache must be `<= 2 MB`.
  - RAM JIT cache must be `<= 384 KB`.
- Replaced ESP32-S3 `heap_caps_malloc(..., MALLOC_CAP_EXEC)` translation-cache
  setup with static PSRAM cache setup.
- Replaced ESP32-S3 libretro video, post-process, previous-frame, and audio
  runtime buffers with static PSRAM storage.
- Replaced the ESP32-S3 test frame-capture heap allocation with a fixed static
  PSRAM capture buffer.
- Changed the ESP32-S3 gamepak paging window to use the existing static PSRAM
  fallback buffer directly instead of trying heap allocation first.
- Added DBUS-data to IBUS-exec alias helpers for the static JIT cache ranges.
- Changed the Xtensa execution loop to call translated blocks through the exec
  alias while retaining data pointers for emission and cache metadata.
- Changed ESP32-S3 `platform_cache_sync()` to route JIT ranges through a
  data-cache writeback plus instruction-cache invalidation path using the
  derived exec alias.
- Updated the IDF test app component dependencies/default config for static
  external BSS and cache sync support.

Still pending:

- Page-by-page validation that each static JIT cache page maps to PSRAM.
- Manual MMU register setup for IBUS aliases if the derived alias is not enough
  on hardware.
- Startup PSRAM executable self-test with first-run and rewrite visibility
  checks.
- Full frontend split where lookup APIs return exec aliases and patch/write
  APIs always use data aliases.
- ROM flash partition / SPI mmap path, so the 1 MB fallback window is not the
  final CoreS3 SE ROM data story.

## Verification Status

Current pass:

- `make -C tests/esp32s3 all test` passes.
- `idf.py -B build-v6.0-esp32s3 build` passes.
- `idf.py -B build-v6.0-esp32s3 qemu` reaches
  `result=PASS backend=dynarec` for the dhrystone test.
- QEMU no longer reports the earlier `esp_cache_msync()` M2C unaligned error;
  instruction invalidation now aligns the exec-alias range to the instruction
  cache line before calling `esp_cache_msync()`.

Remaining verification must be done on real ESP32-S3/CoreS3 SE hardware before
the PSRAM executable-cache path is treated as stable.

## Migration Order

1. Add static ESP32-S3 PSRAM buffer declarations and compile-time size checks.
2. Replace ESP32-S3 dynamic JIT allocation with static PSRAM data buffers and
   dispatch through the derived exec alias.
3. Add DBUS-to-IBUS alias helpers and page validation.
4. Add the PSRAM executable self-test.
5. Route block entry returns through exec aliases while keeping writes on data
   aliases.
6. Update ESP32-S3 cache sync for data/writeback plus instruction/invalidate.
7. Convert video/audio/test capture buffers to static PSRAM.
8. Move ROM loading to flash mmap or a fixed static window so 32 MB dynamic ROM
   buffering is not part of the CoreS3 SE path.
9. Treat PSRAM JIT as experimental until a successful hardware self-test.
10. Run unit tests, QEMU frame tests, longer QEMU captures, then hardware
    gameplay tests.

## Risks

- The hardware may reject instruction fetch from the derived IBUS alias for
  PSRAM BSS pages under the current memory-protection setup.
- Direct branch patching can be wrong if address calculations mix data aliases
  and exec aliases.
- Cache sync bugs may appear only after rewriting existing blocks, not on first
  execution.
- The 8 MB PSRAM budget is tight if ROM buffering, large JIT caches, video
  buffers, metadata, and test capture are all static at once.
- QEMU may not model all PSRAM cache and MMU behavior accurately.

## Acceptance Criteria

- ESP32-S3 build contains no dynamic allocation for JIT caches.
- JIT cache storage is static PSRAM and page-aligned.
- Data and exec aliases are validated at startup.
- The PSRAM executable self-test passes on hardware.
- gpSP block lookup returns executable aliases, while emission and patching use
  writable aliases.
- Cache sync handles emitted code and backpatched code.
- ROM/RAM cache split, RAM invalidation, idle-loop exits, translation gates, and
  scheduler exits are unchanged.
- QEMU frame hash tests still pass.
- Hardware runs show no instruction fetch faults or stale-code execution after
  cache flushes and RAM invalidation.
