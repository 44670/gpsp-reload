/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#if defined(RISCV_RUNTIME_STANDALONE_TEST)
#include "riscv/riscv_runtime_test_shim.h"
#else
#include "common.h"
#include "cpu.h"
#include "gba_memory.h"
#include "main.h"
#endif
#include "riscv/riscv_emit.h"

#include <stddef.h>
#include <stdint.h>

#if defined(GPSP_ESP32S31_PSRAM_FAULT_TRACE) && \
    GPSP_ESP32S31_PSRAM_FAULT_TRACE
#include "esp_attr.h"
#include "esp32s31/psram_fault_trace.h"
#endif

#if defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 32) && \
    (!defined(RISCV_RUNTIME_STANDALONE_TEST) || \
     defined(RISCV_RUNTIME_ENABLE_FAST_RAM_READS))
#define RISCV_RUNTIME_HAS_FAST_RAM_READS 1
#endif

#if defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 32) && \
    (!defined(RISCV_RUNTIME_STANDALONE_TEST) || \
     defined(RISCV_RUNTIME_ENABLE_FAST_RAM_STORES)) && \
    (defined(RISCV_RUNTIME_STANDALONE_TEST) || \
     (GPSP_EWRAM_HAS_SMC_MIRROR && GPSP_IWRAM_HAS_SMC_MIRROR))
#define RISCV_RUNTIME_HAS_FAST_RAM_STORES 1
#endif

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
extern u8 ewram[];
extern u8 iwram[];
#endif

typedef u8 *(*riscv_jit_block_fn)(void);

extern u32 rom_cache_watermark;

static u8 *riscv_jit_control_slow(const riscv_jit_block_meta *meta);
u8 *riscv_jit_lookup_fallthrough(const riscv_jit_block_meta *meta,
                                 s32 cycles);
u8 *riscv_jit_lookup_indirect(const riscv_jit_block_meta *meta,
                              s32 cycles);
void riscv_jit_control_slow_tail(void);
void riscv_jit_fallthrough_tail(void);
void riscv_jit_indirect_lookup_tail(void);
static u32 function_cc riscv_thumb_execute(u32 opcode, u32 pc);
static void function_cc riscv_thumb_execute_bl_pair(u32 first_opcode,
                                                    u32 second_opcode,
                                                    u32 pc);
static u32 function_cc riscv_store_u8_pc(u32 address, u32 value, u32 pc);
static u32 function_cc riscv_store_u16_pc(u32 address, u32 value, u32 pc);
static u32 function_cc riscv_store_u32_pc(u32 address, u32 value, u32 pc);

enum
{
  RISCV_INDIRECT_LOOKUP_CACHE_BITS = 8,
  RISCV_INDIRECT_LOOKUP_CACHE_SIZE =
    1u << RISCV_INDIRECT_LOOKUP_CACHE_BITS,
  RISCV_INDIRECT_LOOKUP_CACHE_MASK =
    RISCV_INDIRECT_LOOKUP_CACHE_SIZE - 1u
};

typedef struct riscv_indirect_lookup_cache_entry
{
  u32 key;
  u32 entry;
  u32 generation;
  u32 reserved;
} riscv_indirect_lookup_cache_entry;

typedef struct riscv_indirect_lookup_cache_state
{
  u32 generation;
  u32 reserved[3];
  riscv_indirect_lookup_cache_entry entries[RISCV_INDIRECT_LOOKUP_CACHE_SIZE];
} riscv_indirect_lookup_cache_state;

typedef char riscv_indirect_cache_entry_size_must_be_16[
  sizeof(riscv_indirect_lookup_cache_entry) == 16 ? 1 : -1];
typedef char riscv_indirect_cache_entry_pointer_offset_must_be_4[
  offsetof(riscv_indirect_lookup_cache_entry, entry) == 4 ? 1 : -1];
typedef char riscv_indirect_cache_entry_generation_offset_must_be_8[
  offsetof(riscv_indirect_lookup_cache_entry, generation) == 8 ? 1 : -1];
typedef char riscv_indirect_cache_entries_offset_must_be_16[
  offsetof(riscv_indirect_lookup_cache_state, entries) == 16 ? 1 : -1];

/* The generated tail reads this table directly. Keep its header and entries
 * power-of-two sized so the RV32 fast path needs only shifts and loads. */
__attribute__((used, aligned(16)))
static riscv_indirect_lookup_cache_state riscv_indirect_lookup_cache;
static cpu_alert_type riscv_cpu_alert;

#if defined(RISCV_RUNTIME_INDIRECT_LOOKUP_PROFILE_SWITCH)
extern volatile u32 riscv_runtime_perf_disable_indirect_lookup_cache;
#endif

enum
{
  RISCV_HELPER_READ32 = 0,
  RISCV_HELPER_STORE32,
  RISCV_HELPER_READ8,
  RISCV_HELPER_STORE8,
  RISCV_HELPER_READ16,
  RISCV_HELPER_BLOCK_STORE32,
  RISCV_HELPER_BLOCK_READ32,
  RISCV_HELPER_STORE16,
  RISCV_HELPER_READ8S,
  RISCV_HELPER_READ16S,
  RISCV_HELPER_EXECUTE_SPSR_RESTORE,
  RISCV_HELPER_STORE_SPSR,
  RISCV_HELPER_STORE_CPSR,
  RISCV_HELPER_EXECUTE_SWI_ARM,
  RISCV_HELPER_EXECUTE_SWI_THUMB,
  RISCV_HELPER_HLE_DIV,
  RISCV_HELPER_SWAP_U8,
  RISCV_HELPER_SWAP_U32,
  RISCV_HELPER_ARM_BLOCK_MEMORY,
  RISCV_HELPER_CYCLES_REMAINING,
  RISCV_HELPER_THUMB_EXECUTE,
  RISCV_HELPER_COUNT
};

static uintptr_t riscv_helper_table[RISCV_HELPER_COUNT];
typedef char riscv_helper_state_size_check[
  (REG_USERDEF + RISCV_HELPER_COUNT <= REG_MAX) ? 1 : -1];
#if defined(GPSP_ESP32S31_PSRAM_FAULT_TRACE) && \
    GPSP_ESP32S31_PSRAM_FAULT_TRACE
typedef char riscv_fault_trace_slots_must_follow_helper_state[
  REG_USERDEF + RISCV_HELPER_COUNT <=
    ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_ENTRY ? 1 : -1];
typedef char riscv_fault_trace_entry_offset_must_be_248[
  ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_ENTRY * sizeof(u32) == 248 ? 1 : -1];
typedef char riscv_fault_trace_key_offset_must_be_252[
  ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_KEY * sizeof(u32) == 252 ? 1 : -1];
#endif

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
  RISCV_STACK_CONTROL_SLOW = 92,
  RISCV_STACK_CYCLES_PTR = 96,
  RISCV_INITIAL_ROM_WATERMARK = 16,
  RISCV_BLOCK_NATIVE_SUPPORTED = 1u,
  RISCV_BLOCK_PC_WRITTEN = 2u,
  RISCV_BLOCK_PC_BASE_EMITTED = 4u,
  RISCV_BLOCK_TERMINAL_EMITTED = 8u,
  RISCV_BLOCK_NO_FALLTHROUGH = 16u
};

/* Store-alert exits are collected while one block is being emitted and
 * patched during that block's finalize step.  Keep the full byte offset here:
 * the old 10-bit metadata field silently wrapped once generated code grew
 * beyond 4 KiB. */
static u32 riscv_store_alert_branch_head_offset;

#define RISCV_INVALID_BLOCK_ENTRY ((u8 *)(uintptr_t)~(uintptr_t)0)

#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH) && \
    !defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
#error "validated-entry A/B requires RISCV_RUNTIME_PERF_PROFILE_SWITCH"
#endif

#if defined(RISCV_RUNTIME_INDIRECT_LOOKUP_PROFILE_SWITCH) && \
    !defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
#error "indirect-lookup A/B requires RISCV_RUNTIME_PERF_PROFILE_SWITCH"
#endif

/* Blocks may tail-jump into each other, so one outer JIT frame owns saved regs. */
#if defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 32)
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
u8 *riscv_enter_jit(u8 *entry_data, void *reg_base, void *control_slow,
                    void *thumb_execute, void *thumb_bl_pair,
                    const void *helper_table, u32 state_helper_calls,
                    u32 validated_entry_optimized);
#elif defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
u8 *riscv_enter_jit(u8 *entry_data, void *reg_base, void *control_slow,
                    void *thumb_execute, void *thumb_bl_pair,
                    const void *helper_table, u32 state_helper_calls);
#else
u8 *riscv_enter_jit(u8 *entry_data, void *reg_base, void *control_slow,
                    void *thumb_execute, void *thumb_bl_pair,
                    const void *helper_table);
#endif

__asm__(
  ".text\n"
  ".align 2\n"
  ".globl riscv_enter_jit\n"
  ".type riscv_enter_jit, @function\n"
  "riscv_enter_jit:\n"
  "  addi sp, sp, -176\n"
  "  sw ra, 172(sp)\n"
  "  sw s0, 168(sp)\n"
  "  sw s1, 164(sp)\n"
  "  sw s2, 160(sp)\n"
  "  sw s3, 156(sp)\n"
  "  sw s4, 152(sp)\n"
  "  sw s5, 148(sp)\n"
  "  sw s6, 144(sp)\n"
  "  sw s7, 140(sp)\n"
  "  sw s8, 136(sp)\n"
  "  sw s9, 132(sp)\n"
  "  sw s10, 128(sp)\n"
  "  sw s11, 124(sp)\n"
  "  mv s0, a1\n"
  "  sw a2, 92(sp)\n"
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  "  bnez a6, 3f\n"
#endif
#if defined(RISCV_RUNTIME_DISABLE_STATE_HELPER_OPT) || \
    defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  "  sw a3, 84(sp)\n"
  "  lw t0, 0(a5)\n"
  "  sw t0, 8(sp)\n"
  "  lw t0, 4(a5)\n"
  "  sw t0, 12(sp)\n"
  "  lw t0, 8(a5)\n"
  "  sw t0, 16(sp)\n"
  "  lw t0, 12(a5)\n"
  "  sw t0, 20(sp)\n"
  "  lw t0, 16(a5)\n"
  "  sw t0, 24(sp)\n"
  "  lw t0, 20(a5)\n"
  "  sw t0, 28(sp)\n"
  "  lw t0, 24(a5)\n"
  "  sw t0, 32(sp)\n"
  "  lw t0, 28(a5)\n"
  "  sw t0, 36(sp)\n"
  "  lw t0, 32(a5)\n"
  "  sw t0, 40(sp)\n"
  "  lw t0, 36(a5)\n"
  "  sw t0, 44(sp)\n"
  "  lw t0, 40(a5)\n"
  "  sw t0, 48(sp)\n"
  "  lw t0, 44(a5)\n"
  "  sw t0, 52(sp)\n"
  "  lw t0, 48(a5)\n"
  "  sw t0, 56(sp)\n"
  "  lw t0, 52(a5)\n"
  "  sw t0, 60(sp)\n"
  "  lw t0, 56(a5)\n"
  "  sw t0, 64(sp)\n"
  "  lw t0, 60(a5)\n"
  "  sw t0, 68(sp)\n"
  "  lw t0, 64(a5)\n"
  "  sw t0, 72(sp)\n"
  "  lw t0, 68(a5)\n"
  "  sw t0, 76(sp)\n"
  "  lw t0, 72(a5)\n"
  "  sw t0, 80(sp)\n"
#endif
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  "3:\n"
#endif
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
  /* Generated code owns a7, so preserve the eighth psABI argument before
   * entering the block loop. */
  "  sw a7, 104(sp)\n"
#endif
  "  lw t0, 76(a5)\n"
  "  sw t0, 96(sp)\n"
  "1:\n"
  "  beqz a0, 2f\n"
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
  "  lw t0, 104(sp)\n"
  "  bnez t0, 4f\n"
#endif
  /* Both lookup paths consume RISCV_INVALID_BLOCK_ENTRY themselves.  The
   * production loop therefore receives only a valid entry or NULL.  Keep the
   * old sentinel check only for the byte-identical data-selector A/B. */
#if defined(RISCV_RUNTIME_DISABLE_VALIDATED_ENTRY_OPT) || \
    defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
  "  addi t0, zero, -1\n"
  "  beq a0, t0, 2f\n"
#endif
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
  "4:\n"
#endif
  "  lw t0, 96(sp)\n"
  /* s10 is the JIT cycle register. Reload it only after the scheduler slow
   * path, which is the sole path allowed to replace the scheduler budget. */
  "  lw s10, 0(t0)\n"
  "  lw a3, 0(s0)\n"
  "  lw a4, 4(s0)\n"
  "  lw a5, 8(s0)\n"
  "  lw a6, 12(s0)\n"
  "  lw a7, 16(s0)\n"
  "  lw s1, 20(s0)\n"
  "  lw s2, 24(s0)\n"
  "  lw s3, 28(s0)\n"
  "  lw s4, 32(s0)\n"
  "  lw s5, 36(s0)\n"
  "  lw s6, 40(s0)\n"
  "  lw s7, 44(s0)\n"
  "  lw s8, 48(s0)\n"
  "  lw s9, 52(s0)\n"
  "  lw s11, 64(s0)\n"
  "  srli s11, s11, 28\n"
  "  andi s11, s11, 15\n"
  /* Do not derive this return address from a fixed instruction count.  The
   * ESP32-S31 assembler relaxes surrounding instructions to RVC, so the old
   * AUIPC+16 calculation could land in the middle of the restore epilogue. */
  "  lla t0, .Lriscv_jit_loop_return\n"
  "  sw t0, 88(sp)\n"
  "  jalr ra, a0, 0\n"
  ".Lriscv_jit_loop_return:\n"
  "  j 1b\n"
  "2:\n"
  "  lw s11, 124(sp)\n"
  "  lw s10, 128(sp)\n"
  "  lw s9, 132(sp)\n"
  "  lw s8, 136(sp)\n"
  "  lw s7, 140(sp)\n"
  "  lw s6, 144(sp)\n"
  "  lw s5, 148(sp)\n"
  "  lw s4, 152(sp)\n"
  "  lw s3, 156(sp)\n"
  "  lw s2, 160(sp)\n"
  "  lw s1, 164(sp)\n"
  "  lw s0, 168(sp)\n"
  "  lw ra, 172(sp)\n"
  "  addi sp, sp, 176\n"
  "  ret\n"
  ".size riscv_enter_jit, .-riscv_enter_jit\n");
__asm__(
  ".text\n"
  ".align 2\n"
  ".globl riscv_jit_control_slow_tail\n"
  ".type riscv_jit_control_slow_tail, @function\n"
  "riscv_jit_control_slow_tail:\n"
  "  lw t5, 96(sp)\n"
  "  sw s10, 0(t5)\n"
  "  lw ra, 88(sp)\n"
  "  lw t0, 92(sp)\n"
  "  jalr zero, t0, 0\n"
  ".size riscv_jit_control_slow_tail, .-riscv_jit_control_slow_tail\n");

/* Fast fallthrough and indirect misses call only a block-lookup helper. They
 * preserve the resident cycle budget in s10 and jump directly to the returned
 * translation, matching the MIPS lookup-stub contract. Scheduler-visible
 * state is diverted to riscv_jit_control_slow_tail before the lookup call. */
__asm__(
  ".text\n"
  ".align 2\n"
  ".globl riscv_jit_fallthrough_tail\n"
  ".type riscv_jit_fallthrough_tail, @function\n"
  "riscv_jit_fallthrough_tail:\n"
  "  bge zero, s10, .Lriscv_jit_fallthrough_control\n"
  "  lla t0, riscv_cpu_alert\n"
  "  lbu t0, 0(t0)\n"
  "  bnez t0, .Lriscv_jit_fallthrough_control\n"
  "  lw t0, 72(s0)\n"
  "  bnez t0, .Lriscv_jit_fallthrough_control\n"
  "  lla t0, idle_loop_target_pc\n"
  "  lw t0, 0(t0)\n"
  "  lw t1, 60(s0)\n"
  "  beq t1, t0, .Lriscv_jit_fallthrough_control\n"
  "  mv a1, s10\n"
  "  call riscv_jit_lookup_fallthrough\n"
  "  j .Lriscv_jit_lookup_return\n"
  ".Lriscv_jit_fallthrough_control:\n"
  "  j riscv_jit_control_slow_tail\n"
  ".size riscv_jit_fallthrough_tail, .-riscv_jit_fallthrough_tail\n"

  ".globl riscv_jit_indirect_lookup_tail\n"
  ".type riscv_jit_indirect_lookup_tail, @function\n"
  "riscv_jit_indirect_lookup_tail:\n"
  /* Scheduler work takes precedence over both a cache hit and a C lookup. */
  "  bge zero, s10, .Lriscv_jit_indirect_control\n"
  "  lla t0, riscv_cpu_alert\n"
  "  lbu t0, 0(t0)\n"
  "  bnez t0, .Lriscv_jit_indirect_control\n"
  "  lw t0, 72(s0)\n"
  "  bnez t0, .Lriscv_jit_indirect_control\n"
  "  lla t0, idle_loop_target_pc\n"
  "  lw t0, 0(t0)\n"
  "  lw t1, 60(s0)\n"
  "  beq t1, t0, .Lriscv_jit_indirect_control\n"
#if !defined(RISCV_RUNTIME_DISABLE_INDIRECT_LOOKUP_CACHE) || \
    defined(RISCV_RUNTIME_INDIRECT_LOOKUP_PROFILE_SWITCH)
#if defined(RISCV_RUNTIME_INDIRECT_LOOKUP_PROFILE_SWITCH)
  "  lla t0, riscv_runtime_perf_disable_indirect_lookup_cache\n"
  "  lw t0, 0(t0)\n"
  "  bnez t0, .Lriscv_jit_indirect_miss\n"
#endif
#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
  "  lla t0, riscv_control_indirect_cache_attempts\n"
  "  lw t1, 0(t0)\n"
  "  addi t1, t1, 1\n"
  "  sw t1, 0(t0)\n"
#endif

  /* key=(aligned PC)|thumb. ARM and Thumb entries sharing an address must not
   * alias, and ARM targets must receive the dispatcher's word alignment. */
  "  lw t0, 64(s0)\n"
  "  srli t0, t0, 5\n"
  "  andi t0, t0, 1\n"
  "  slli t1, t0, 1\n"
  "  addi t1, t1, -4\n"
  "  lw t2, 60(s0)\n"
  "  and t2, t2, t1\n"
  "  or t2, t2, t0\n"

  "  srli t1, t2, 1\n"
  "  andi t1, t1, 255\n"
  "  slli t1, t1, 4\n"
  "  lla t3, riscv_indirect_lookup_cache\n"
  "  lw t0, 0(t3)\n"
  "  addi t3, t3, 16\n"
  "  add t3, t3, t1\n"
  "  lw t1, 8(t3)\n"
  "  bne t1, t0, .Lriscv_jit_indirect_miss\n"
  "  lw t1, 0(t3)\n"
  "  bne t1, t2, .Lriscv_jit_indirect_miss\n"
  "  lw t6, 4(t3)\n"
  "  beqz t6, .Lriscv_jit_indirect_miss\n"
#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
  "  lla t0, riscv_control_indirect_cache_hits\n"
  "  lw t1, 0(t0)\n"
  "  addi t1, t1, 1\n"
  "  sw t1, 0(t0)\n"
#endif
#if defined(GPSP_ESP32S31_PSRAM_FAULT_TRACE) && \
    GPSP_ESP32S31_PSRAM_FAULT_TRACE
  /* Publish the pair only after every generation/key check passed. If the
   * following JALR fetches through a stale PSRAM/MMU mapping, the fault ISR
   * sees the exact cached key and entry that triggered it. Bit 31 marks this
   * as a fast-tail hit; real GBA guest PCs never use that bit. */
  "  sw t6, 248(s0)\n"
  "  li t0, -2147483648\n"
  "  or t0, t0, t2\n"
  "  sw t0, 252(s0)\n"
#endif
  "  jalr zero, t6, 0\n"

  ".Lriscv_jit_indirect_miss:\n"
#endif
  "  mv a1, s10\n"
  "  call riscv_jit_lookup_indirect\n"
  ".Lriscv_jit_lookup_return:\n"
  "  beqz a0, .Lriscv_jit_lookup_exit\n"
  /* A C lookup preserves s0/s1-s11 by psABI, but reloading the canonical
   * mapping also covers paths whose preceding instruction helper deliberately
   * invalidated caller- or callee-mapped guest state. */
  "  lw a3, 0(s0)\n"
  "  lw a4, 4(s0)\n"
  "  lw a5, 8(s0)\n"
  "  lw a6, 12(s0)\n"
  "  lw a7, 16(s0)\n"
  "  lw s1, 20(s0)\n"
  "  lw s2, 24(s0)\n"
  "  lw s3, 28(s0)\n"
  "  lw s4, 32(s0)\n"
  "  lw s5, 36(s0)\n"
  "  lw s6, 40(s0)\n"
  "  lw s7, 44(s0)\n"
  "  lw s8, 48(s0)\n"
  "  lw s9, 52(s0)\n"
  "  lw s11, 64(s0)\n"
  "  srli s11, s11, 28\n"
  "  andi s11, s11, 15\n"
  "  jalr zero, a0, 0\n"
  ".Lriscv_jit_lookup_exit:\n"
  "  lw ra, 88(sp)\n"
  "  ret\n"
  ".Lriscv_jit_indirect_control:\n"
  "  j riscv_jit_control_slow_tail\n"
  ".size riscv_jit_indirect_lookup_tail, .-riscv_jit_indirect_lookup_tail\n");
#else
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
static u8 *riscv_enter_jit(u8 *entry_data, void *reg_base,
                           void *control_slow,
                           void *thumb_execute, void *thumb_bl_pair,
                           const void *helper_table,
                           u32 state_helper_calls,
                           u32 validated_entry_optimized)
#elif defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
static u8 *riscv_enter_jit(u8 *entry_data, void *reg_base,
                           void *control_slow,
                           void *thumb_execute, void *thumb_bl_pair,
                           const void *helper_table,
                           u32 state_helper_calls)
#else
static u8 *riscv_enter_jit(u8 *entry_data, void *reg_base,
                           void *control_slow,
                           void *thumb_execute, void *thumb_bl_pair,
                           const void *helper_table)
#endif
{
  (void)reg_base;
  (void)control_slow;
  (void)thumb_execute;
  (void)thumb_bl_pair;
  (void)helper_table;
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  (void)state_helper_calls;
#endif
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
  (void)validated_entry_optimized;
#endif

  do
  {
    riscv_jit_block_fn entry = (riscv_jit_block_fn)entry_data;
    entry_data = entry();
  } while (entry_data && entry_data != RISCV_INVALID_BLOCK_ENTRY);

  return entry_data;
}

void riscv_jit_control_slow_tail(void)
{
}

void riscv_jit_fallthrough_tail(void)
{
}

void riscv_jit_indirect_lookup_tail(void)
{
}
#endif

static s32 riscv_cycles_remaining;
static u32 riscv_blocks_emitted;
static u32 riscv_blocks_executed;
static u32 riscv_bios_native_blocks_emitted;
static u32 riscv_bios_native_blocks_executed;
static u32 riscv_bios_interpreter_fallbacks;
static u32 riscv_interpreter_fallbacks;
static u32 riscv_initial_lookup_fallbacks;
static u32 riscv_relookup_fallbacks;
static u32 riscv_unsupported_fallbacks;
static u32 riscv_native_data_proc_insns;
static u32 riscv_native_branch_insns;
static u32 riscv_native_load_insns;
static u32 riscv_native_store_insns;
static u32 riscv_native_psr_insns;
static u32 riscv_thumb_helper_insns;
static bool riscv_debug_force_dispatch;
static u32 riscv_debug_disable_thumb_native;
static u32 riscv_debug_disable_arm_native;
static u32 riscv_debug_branch_probe_pc;
static volatile riscv_runtime_debug_branch_probe
  riscv_debug_branch_probe_state;
static u32 riscv_debug_arm_probe_pc;
static volatile riscv_runtime_debug_arm_probe riscv_debug_arm_probe_state;
#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
static u32 riscv_control_stub_entries;
static u32 riscv_control_direct_chain_attempts;
static u32 riscv_control_direct_chain_hits;
static u32 riscv_control_cycle_exits;
static u32 riscv_control_indirect_lookup_hits;
static u32 riscv_control_indirect_lookup_misses;
/* Referenced by the hand-written RV32 tail above, so these need stable
 * assembler-visible names rather than compiler-local aliases. */
u32 riscv_control_indirect_cache_attempts;
u32 riscv_control_indirect_cache_hits;
static u32 riscv_control_fallthrough_lookup_hits;
static u32 riscv_control_fallthrough_lookup_misses;
static u32 riscv_control_scheduler_updates;
static u32 riscv_control_lookup_stub_entries;
static u32 riscv_control_slow_path_entries;

#define RISCV_CONTROL_COUNT(counter) ((counter)++)
#else
#define RISCV_CONTROL_COUNT(counter) ((void)0)
#endif

#if defined(RISCV_RUNTIME_PERF_COUNTERS)
static u32 riscv_perf_helper_call_sites;
static u32 riscv_perf_terminal_call_sites;
static u32 riscv_perf_mapped_flush_sites;
static u32 riscv_perf_mapped_store_ops;
static u32 riscv_perf_mapped_invalidate_sites;
static u32 riscv_perf_mapped_reload_sites;
static u32 riscv_perf_mapped_reload_ops;

static u32 riscv_perf_popcount(u32 value)
{
  u32 count = 0;

  while (value)
  {
    count += value & 1u;
    value >>= 1;
  }
  return count;
}
#endif

static u32 riscv_arm_expand_imm(u32 opcode);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void riscv_note_runtime_fallback(u32 kind, u32 pc, u32 thumb,
                                 u32 lookup_result,
                                 u32 cycles_remaining)
{
  (void)kind;
  (void)pc;
  (void)thumb;
  (void)lookup_result;
  (void)cycles_remaining;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void riscv_note_runtime_block_emit(u32 start_pc, u32 end_pc, u32 thumb,
                                   u32 code_bytes)
{
  (void)start_pc;
  (void)end_pc;
  (void)thumb;
  (void)code_bytes;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void riscv_note_runtime_block_execute(u32 start_pc, u32 end_pc, u32 thumb)
{
  (void)start_pc;
  (void)end_pc;
  (void)thumb;
}

static u8 *riscv_align_ptr(u8 *ptr)
{
  uintptr_t value = (uintptr_t)ptr;
  value = (value + 3u) & ~(uintptr_t)3u;
  return (u8 *)value;
}

static u32 riscv_mapped_dirty_mask;
static u32 riscv_mapped_valid_mask;
static u32 riscv_terminal_helper_size;
static bool riscv_arm_conditional_block_active;
static u32 riscv_arm_conditional_entry_dirty_mask;
static u32 riscv_arm_conditional_entry_valid_mask;

#define RISCV_MAPPED_REG_COUNT 15u
#define RISCV_MAPPED_GPR_COUNT 14u
#define RISCV_MAPPED_NZCV_SLOT 14u
#define RISCV_MAPPED_REGS_MASK ((1u << RISCV_MAPPED_REG_COUNT) - 1u)
#define RISCV_MAPPED_NZCV_MASK (1u << RISCV_MAPPED_NZCV_SLOT)
#define RISCV_MAPPED_CALLER_SAVED_MASK ((1u << 5) - 1u)
#define RISCV_CYCLES_REG riscv_reg_s10

static const riscv_reg_number riscv_mapped_host_regs[RISCV_MAPPED_REG_COUNT] =
{
  riscv_reg_a3,  /* r0 */
  riscv_reg_a4,  /* r1 */
  riscv_reg_a5,  /* r2 */
  riscv_reg_a6,  /* r3 */
  riscv_reg_a7,  /* r4 */
  riscv_reg_s1,  /* r5 */
  riscv_reg_s2,  /* r6 */
  riscv_reg_s3,  /* r7 */
  riscv_reg_s4,  /* r8 */
  riscv_reg_s5,  /* r9 */
  riscv_reg_s6,  /* r10 */
  riscv_reg_s7,  /* r11 */
  riscv_reg_s8,  /* r12 */
  riscv_reg_s9,  /* r13 / SP */
  riscv_reg_s11  /* packed NZCV: N=8, Z=4, C=2, V=1 */
};

static const u8 riscv_mapped_state_regs[RISCV_MAPPED_REG_COUNT] =
{
  0, 1, 2, 3, 4, 5, 6, 7,
  8, 9, 10, 11, 12, REG_SP, REG_CPSR
};

static u32 riscv_block_meta_thumb(const riscv_jit_block_meta *meta)
{
  return meta->end_delta_thumb & 1u;
}

static u32 riscv_block_meta_end_pc(const riscv_jit_block_meta *meta)
{
  return meta->start_pc + (meta->end_delta_thumb & ~1u);
}

static u32 riscv_block_meta_chain_offset(const riscv_jit_block_meta *meta)
{
  (void)meta;
  return riscv_store_alert_branch_head_offset;
}

static void riscv_block_meta_set_chain_offset(riscv_jit_block_meta *meta,
                                              u32 offset)
{
  riscv_store_alert_branch_head_offset = offset;
  if (meta)
    meta->chain_units = 0;
}

static void riscv_block_meta_set_terminal_offset(riscv_jit_block_meta *meta,
                                                 u32 offset)
{
  meta->end_delta_thumb = (u16)((offset <= 0x1fffeu) ? (offset >> 1) : 0);
}

static void riscv_block_meta_set_final_range(riscv_jit_block_meta *meta,
                                             u32 block_start_pc,
                                             u32 block_end_pc,
                                             bool thumb_mode)
{
  u32 delta = block_end_pc - block_start_pc;

  meta->start_pc = block_start_pc;
  meta->end_delta_thumb = (u16)((delta & ~1u) | (thumb_mode ? 1u : 0u));
}

static void riscv_emit_li(u8 **ptr, riscv_reg_number rd, u32 value)
{
  u8 *translation_ptr = *ptr;

  if (value <= 2047u || value >= 0xfffff800u)
  {
    riscv_emit_addi(rd, riscv_reg_zero, (s32)value);
  }
  else
  {
    u32 upper = (value + 0x800u) >> 12;
    int lower = (int)(value - (upper << 12));
    riscv_emit_lui(rd, upper);
    if (lower)
    {
      riscv_emit_addi(rd, rd, lower);
    }
  }

  *ptr = translation_ptr;
}

#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
static void riscv_emit_control_counter_increment(u8 **ptr, u32 *counter)
{
  u8 *translation_ptr;

  riscv_emit_li(ptr, riscv_reg_t0, (u32)(uintptr_t)counter);
  translation_ptr = *ptr;
  riscv_emit_lw(riscv_reg_t1, riscv_reg_t0, 0);
  riscv_emit_addi(riscv_reg_t1, riscv_reg_t1, 1);
  riscv_emit_sw(riscv_reg_t1, riscv_reg_t0, 0);
  *ptr = translation_ptr;
}
#endif

static void riscv_emit_control_tail_jump(
  u8 **ptr, const riscv_jit_block_meta *meta)
{
  u8 *translation_ptr;
  uintptr_t tail = (uintptr_t)riscv_jit_fallthrough_tail;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    tail = (uintptr_t)riscv_jit_control_slow_tail;

#if !defined(RISCV_RUNTIME_DISABLE_INDIRECT_LOOKUP_CACHE) || \
    defined(RISCV_RUNTIME_INDIRECT_LOOKUP_PROFILE_SWITCH)
  /* The cache-hit tail jumps straight into the next generated block, so all
   * mapped guest registers must still be live.  An exiting C helper updates
   * the state array and deliberately skips its reload because the normal
   * dispatcher return reloads at the outer loop.  Sending that block through
   * the cache-hit tail would bypass the reload and execute with stale mapped
   * guest values. */
  if (meta && (meta->flags & RISCV_BLOCK_PC_WRITTEN) &&
      (riscv_mapped_valid_mask & RISCV_MAPPED_REGS_MASK) ==
        RISCV_MAPPED_REGS_MASK)
    tail = (uintptr_t)riscv_jit_indirect_lookup_tail;
#else
  if (meta && (meta->flags & RISCV_BLOCK_PC_WRITTEN))
    tail = (uintptr_t)riscv_jit_indirect_lookup_tail;
#endif

  riscv_emit_li(ptr, riscv_reg_t0, (u32)tail);
  translation_ptr = *ptr;
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_t0, 0);
  *ptr = translation_ptr;
}

static bool riscv_i12_fits(u32 value)
{
  s32 signed_value = (s32)value;

  return signed_value >= -2048 && signed_value <= 2047;
}

static bool riscv_arm_data_proc_is_noop(u32 opcode)
{
  u32 op = (opcode >> 21) & 0xfu;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 imm_op = (opcode >> 25) & 1u;

  if (set_flags || rd == REG_PC)
    return false;

  if (imm_op)
  {
    u32 immediate = riscv_arm_expand_imm(opcode);

    if (immediate != 0 || rd != rn)
      return false;

    return op == 0x1 || op == 0x2 || op == 0x4 ||
           op == 0xc || op == 0xe;
  }

  if ((opcode & 0x00000ff0u) != 0)
    return false;

  return op == 0xdu && rd == (opcode & 0xfu);
}

static bool riscv_arm_reg_mapped(u32 reg_index,
                                 riscv_reg_number *host_reg,
                                 u32 *dirty_mask);
static void riscv_emit_mapped_regs_reload_mask(u8 **ptr_ref,
                                                u32 reload_mask);
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
extern volatile u32 riscv_runtime_perf_disable_mapped_alu_fastpath;
extern volatile u32 riscv_runtime_perf_disable_fast_ram_reads;
extern volatile u32 riscv_runtime_perf_disable_fast_ram_stores;
extern volatile u32 riscv_runtime_perf_disable_entry_setup_opt;
extern volatile u32 riscv_runtime_perf_disable_state_helper_opt;
#endif
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
extern volatile u32 riscv_runtime_perf_disable_validated_entry_opt;
#endif

#if !defined(RISCV_RUNTIME_DISABLE_MAPPED_ALU_FASTPATH)
static bool riscv_emit_arm_data_proc_mapped_reg_alu(u8 **ptr_ref,
                                                     u32 opcode)
{
  u32 condition = opcode >> 28;
  u32 op = (opcode >> 21) & 0xfu;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 rm = opcode & 0xfu;
  riscv_reg_number rn_host;
  riscv_reg_number rd_host;
  riscv_reg_number rm_host;
  u32 rn_mask;
  u32 rd_mask;
  u32 rm_mask;
  u32 source_mask;
  u8 *translation_ptr;

  if (condition != 0xe || set_flags || ((opcode >> 25) & 1u) ||
      (opcode & 0x00000ff0u) != 0 ||
      rn == REG_PC || rd == REG_PC || rm == REG_PC)
  {
    return false;
  }

  if (op != 0x0 && op != 0x1 && op != 0x2 && op != 0x3 &&
      op != 0x4 && op != 0xc && op != 0xd && op != 0xe && op != 0xf)
  {
    return false;
  }

  if (!riscv_arm_reg_mapped(rn, &rn_host, &rn_mask) ||
      !riscv_arm_reg_mapped(rd, &rd_host, &rd_mask) ||
      !riscv_arm_reg_mapped(rm, &rm_host, &rm_mask))
  {
    return false;
  }

  source_mask = rm_mask;
  if (op != 0xd && op != 0xf)
    source_mask |= rn_mask;
  riscv_emit_mapped_regs_reload_mask(
    ptr_ref, source_mask & ~riscv_mapped_valid_mask);

  translation_ptr = *ptr_ref;
  switch (op)
  {
    case 0x0:
      riscv_emit_and(rd_host, rn_host, rm_host);
      break;
    case 0x1:
      riscv_emit_xor(rd_host, rn_host, rm_host);
      break;
    case 0x2:
      riscv_emit_sub(rd_host, rn_host, rm_host);
      break;
    case 0x3:
      riscv_emit_sub(rd_host, rm_host, rn_host);
      break;
    case 0x4:
      riscv_emit_add(rd_host, rn_host, rm_host);
      break;
    case 0xc:
      riscv_emit_or(rd_host, rn_host, rm_host);
      break;
    case 0xd:
      riscv_emit_addi(rd_host, rm_host, 0);
      break;
    case 0xe:
      riscv_emit_xori(riscv_reg_t0, rm_host, -1);
      riscv_emit_and(rd_host, rn_host, riscv_reg_t0);
      break;
    case 0xf:
      riscv_emit_xori(rd_host, rm_host, -1);
      break;
    default:
      return false;
  }
  *ptr_ref = translation_ptr;
  riscv_mapped_valid_mask |= rd_mask;
  riscv_mapped_dirty_mask |= rd_mask;
  return true;
}
#endif

static void riscv_emit_guest_pc_load_ex(u8 **ptr_ref,
                                        riscv_jit_block_meta *meta,
                                        riscv_reg_number rd,
                                        u32 pc_value)
{
  (void)meta;
  riscv_emit_li(ptr_ref, rd, pc_value);
}

static void riscv_emit_guest_pc_load(u8 **ptr_ref,
                                     riscv_jit_block_meta *meta,
                                     riscv_reg_number rd,
                                     u32 pc_value)
{
  riscv_emit_guest_pc_load_ex(ptr_ref, meta, rd, pc_value);
}

static void riscv_emit_guest_pc_load_existing_base(u8 **ptr_ref,
                                                   riscv_jit_block_meta *meta,
                                                   riscv_reg_number rd,
                                                   u32 pc_value)
{
  riscv_emit_guest_pc_load_ex(ptr_ref, meta, rd, pc_value);
}

static void riscv_emit_cpsr_pack_nzcv(u8 **ptr_ref,
                                      riscv_reg_number rd,
                                      riscv_reg_number cpsr_reg)
{
  u8 *translation_ptr = *ptr_ref;

  riscv_emit_srli(rd, cpsr_reg, 28);
  riscv_emit_andi(rd, rd, 0x0f);

  *ptr_ref = translation_ptr;
}

static riscv_reg_number riscv_scratch_not(riscv_reg_number avoid)
{
  return avoid == riscv_reg_t6 ? riscv_reg_t5 : riscv_reg_t6;
}

static void riscv_emit_cpsr_from_live_nzcv(u8 **ptr_ref,
                                           riscv_reg_number rd)
{
  riscv_reg_number nzcv_reg =
    riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT];
  riscv_reg_number cpsr_reg = (rd == nzcv_reg) ? riscv_scratch_not(rd) : rd;
  riscv_reg_number tmp = riscv_scratch_not(cpsr_reg);
  u8 *translation_ptr = *ptr_ref;

  riscv_emit_lw(cpsr_reg, riscv_reg_s0, REG_CPSR * 4u);
  riscv_emit_slli(cpsr_reg, cpsr_reg, 4);
  riscv_emit_srli(cpsr_reg, cpsr_reg, 4);
  riscv_emit_slli(tmp, nzcv_reg, 28);
  riscv_emit_or(cpsr_reg, cpsr_reg, tmp);
  if (cpsr_reg != rd)
    riscv_emit_addi(rd, cpsr_reg, 0);
  *ptr_ref = translation_ptr;
}

static void riscv_emit_live_nzcv_flush(u8 **ptr_ref)
{
  u8 *translation_ptr = *ptr_ref;

  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, REG_CPSR * 4u);
  riscv_emit_slli(riscv_reg_t0, riscv_reg_t0, 4);
  riscv_emit_srli(riscv_reg_t0, riscv_reg_t0, 4);
  riscv_emit_slli(riscv_reg_t1,
                  riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 28);
  riscv_emit_or(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_s0, REG_CPSR * 4u);

  *ptr_ref = translation_ptr;
}

static void riscv_emit_live_nzcv_bit_load(u8 **ptr_ref,
                                          riscv_reg_number rd,
                                          u32 mask,
                                          u32 shift)
{
  u8 *translation_ptr;

  if (!(riscv_mapped_valid_mask & RISCV_MAPPED_NZCV_MASK))
    riscv_emit_mapped_regs_reload_mask(ptr_ref, RISCV_MAPPED_NZCV_MASK);

  translation_ptr = *ptr_ref;
  riscv_emit_srli(rd, riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], shift);
  riscv_emit_andi(rd, rd, mask);
  *ptr_ref = translation_ptr;
}

static void riscv_emit_live_nzcv_update_begin(u8 **ptr_ref,
                                              u32 flag_mask,
                                              bool preserve_dead_flags)
{
  u8 *translation_ptr;

  flag_mask &= 0x0fu;
  if (preserve_dead_flags &&
      !(riscv_mapped_valid_mask & RISCV_MAPPED_NZCV_MASK))
  {
    riscv_emit_mapped_regs_reload_mask(ptr_ref, RISCV_MAPPED_NZCV_MASK);
  }

  translation_ptr = *ptr_ref;
  if (preserve_dead_flags)
    riscv_emit_andi(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                    riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                    (~flag_mask) & 0x0fu);
  else
    riscv_emit_addi(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                    riscv_reg_zero, 0);

  *ptr_ref = translation_ptr;
  riscv_mapped_valid_mask |= RISCV_MAPPED_NZCV_MASK;
  riscv_mapped_dirty_mask |= RISCV_MAPPED_NZCV_MASK;
}

static void riscv_emit_live_nzcv_or_result_nz(u8 **ptr_ref,
                                              u32 flag_mask,
                                              riscv_reg_number result_reg)
{
  u8 *translation_ptr = *ptr_ref;

  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, result_reg, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 3);
    riscv_emit_or(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_sltiu(riscv_reg_t5, result_reg, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 2);
    riscv_emit_or(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_reg_t5);
  }

  *ptr_ref = translation_ptr;
}

static void riscv_emit_live_nzcv_or_carry(u8 **ptr_ref,
                                          riscv_reg_number carry_reg)
{
  u8 *translation_ptr = *ptr_ref;

  riscv_emit_slli(riscv_reg_t5, carry_reg, 1);
  riscv_emit_or(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                riscv_reg_t5);

  *ptr_ref = translation_ptr;
}

static void riscv_emit_live_nzcv_or_overflow(u8 **ptr_ref,
                                             riscv_reg_number overflow_reg)
{
  u8 *translation_ptr = *ptr_ref;

  riscv_emit_or(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                overflow_reg);

  *ptr_ref = translation_ptr;
}

static bool riscv_arm_reg_mapped(u32 reg_index, riscv_reg_number *host_reg,
                                 u32 *dirty_mask)
{
  u32 slot;

  if (reg_index < RISCV_MAPPED_GPR_COUNT)
  {
    slot = reg_index;
  }
  else
    return false;

  *host_reg = riscv_mapped_host_regs[slot];
  if (dirty_mask)
    *dirty_mask = 1u << slot;
  return true;
}

static void riscv_emit_reg_move(u8 **ptr_ref, riscv_reg_number rd,
                                riscv_reg_number rs)
{
  u8 *translation_ptr;

  if (rd == rs)
    return;

  translation_ptr = *ptr_ref;
  riscv_emit_addi(rd, rs, 0);
  *ptr_ref = translation_ptr;
}

static void riscv_emit_mapped_regs_reload(u8 **ptr_ref)
{
  u8 *translation_ptr = *ptr_ref;
  u32 i;

  for (i = 0; i < RISCV_MAPPED_GPR_COUNT; i++)
  {
    riscv_emit_lw(riscv_mapped_host_regs[i], riscv_reg_s0,
                  (u32)riscv_mapped_state_regs[i] * 4u);
  }
  *ptr_ref = translation_ptr;
  riscv_emit_lw(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                riscv_reg_s0, REG_CPSR * 4u);
  *ptr_ref = translation_ptr;
  riscv_emit_cpsr_pack_nzcv(
    ptr_ref, riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
    riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT]);

  riscv_mapped_valid_mask = RISCV_MAPPED_REGS_MASK;
  riscv_mapped_dirty_mask &= ~RISCV_MAPPED_REGS_MASK;
}

static void riscv_note_mapped_regs_reloaded(void)
{
  riscv_mapped_valid_mask = RISCV_MAPPED_REGS_MASK;
  riscv_mapped_dirty_mask &= ~RISCV_MAPPED_REGS_MASK;
}

static void riscv_emit_mapped_regs_reload_mask(u8 **ptr_ref, u32 reload_mask)
{
  u8 *translation_ptr = *ptr_ref;
  u32 i;

  reload_mask &= RISCV_MAPPED_REGS_MASK;
  if (!reload_mask)
    return;

#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_mapped_reload_sites++;
  riscv_perf_mapped_reload_ops += riscv_perf_popcount(reload_mask);
#endif

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

void riscv_emit_debug_arm_instruction_probe(u8 **ptr_ref, u32 pc)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if (pc != riscv_debug_arm_probe_pc)
    return;

  riscv_emit_li(&ptr, riscv_reg_t2,
                (u32)(uintptr_t)&riscv_debug_arm_probe_state);
  translation_ptr = ptr;
  riscv_emit_sw(riscv_reg_a3, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, host_r0));
  riscv_emit_sw(riscv_reg_a4, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, host_r1));
  riscv_emit_sw(riscv_reg_a5, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, host_r2));
  riscv_emit_sw(riscv_reg_a6, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, host_r3));
  riscv_emit_sw(riscv_reg_s8, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, host_r12));
  riscv_emit_sw(riscv_reg_s9, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, host_sp));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, REG_LR * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, host_lr));
  riscv_emit_sw(riscv_reg_s11, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, host_nzcv));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, 0u * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_r0));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, 1u * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_r1));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, 2u * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_r2));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, 3u * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_r3));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, 12u * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_r12));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, REG_SP * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_sp));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, REG_LR * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_lr));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, REG_CPSR * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_cpsr));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, CPU_MODE * 4u);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, state_mode));
  ptr = translation_ptr;
  riscv_emit_li(&ptr, riscv_reg_t0, pc);
  translation_ptr = ptr;
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, pc));
  ptr = translation_ptr;
  riscv_emit_li(&ptr, riscv_reg_t0, riscv_mapped_valid_mask);
  translation_ptr = ptr;
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, valid_mask));
  ptr = translation_ptr;
  riscv_emit_li(&ptr, riscv_reg_t0, riscv_mapped_dirty_mask);
  translation_ptr = ptr;
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, dirty_mask));
  riscv_emit_lw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, count));
  riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, 1);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
                offsetof(riscv_runtime_debug_arm_probe, count));
  *ptr_ref = translation_ptr;
}

static void riscv_emit_mapped_regs_flush_mask(u8 **ptr_ref, u32 dirty_mask)
{
  u8 *translation_ptr;
  u32 i;

  dirty_mask &= riscv_mapped_valid_mask;
  if (!dirty_mask)
    return;

#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_mapped_flush_sites++;
  riscv_perf_mapped_store_ops += riscv_perf_popcount(dirty_mask);
#endif

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

static void riscv_emit_arm_reg_load(u8 **ptr, riscv_reg_number rd,
                                    u32 reg_index)
{
  riscv_reg_number mapped_reg;
  u32 mapped_mask;
  u8 *translation_ptr = *ptr;

  if (reg_index == REG_CPSR)
  {
    *ptr = translation_ptr;
    if (!(riscv_mapped_valid_mask & RISCV_MAPPED_NZCV_MASK))
    {
      riscv_emit_mapped_regs_reload_mask(ptr, RISCV_MAPPED_NZCV_MASK);
      translation_ptr = *ptr;
    }
    riscv_emit_cpsr_from_live_nzcv(ptr, rd);
    return;
  }

  if (riscv_arm_reg_mapped(reg_index, &mapped_reg, &mapped_mask))
  {
    if (!(riscv_mapped_valid_mask & mapped_mask))
    {
      riscv_emit_lw(mapped_reg, riscv_reg_s0, reg_index * 4u);
      riscv_mapped_valid_mask |= mapped_mask;
      riscv_mapped_dirty_mask &= ~mapped_mask;
    }
    *ptr = translation_ptr;
    riscv_emit_reg_move(ptr, rd, mapped_reg);
    return;
  }

  riscv_emit_lw(rd, riscv_reg_s0, reg_index * 4u);

  *ptr = translation_ptr;
}

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

  if (reg_index == REG_CPSR)
  {
    riscv_reg_number tmp = (rs == riscv_reg_t0) ? riscv_reg_t1 :
      riscv_reg_t0;

    riscv_emit_srli(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                    rs, 28);
    riscv_emit_andi(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                    riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 0x0f);
    riscv_emit_slli(tmp, rs, 4);
    riscv_emit_srli(tmp, tmp, 4);
    riscv_emit_sw(tmp, riscv_reg_s0, reg_index * 4u);
    *ptr = translation_ptr;
    riscv_mapped_valid_mask |= RISCV_MAPPED_NZCV_MASK;
    riscv_mapped_dirty_mask |= RISCV_MAPPED_NZCV_MASK;
    return;
  }

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

static void riscv_emit_arm_reg_or_pc_load(u8 **ptr, riscv_reg_number rd,
                                          riscv_jit_block_meta *meta,
                                          u32 reg_index, u32 pc_value)
{
  if (reg_index == REG_PC)
    riscv_emit_guest_pc_load(ptr, meta, rd, pc_value);
  else
    riscv_emit_arm_reg_load(ptr, rd, reg_index);
}

static void riscv_arm_const_clear_reg(u32 *const_mask, u32 reg_index)
{
  if (!const_mask)
    return;

  if (reg_index < REG_PC)
    *const_mask &= ~(1u << reg_index);
  else if (reg_index == REG_PC)
    *const_mask = 0;
}

static void riscv_arm_const_set_reg(u32 *const_mask,
                                    u32 *const_values,
                                    u32 reg_index,
                                    u32 value)
{
  if (!const_mask || !const_values)
    return;

  if (reg_index < REG_PC)
  {
    const_values[reg_index] = value;
    *const_mask |= (1u << reg_index);
  }
  else if (reg_index == REG_PC)
  {
    *const_mask = 0;
  }
}

static bool riscv_arm_const_reg_value(u32 reg_index,
                                      u32 pc_value,
                                      u32 const_mask,
                                      const u32 *const_values,
                                      u32 *value_out)
{
  if (reg_index == REG_PC)
  {
    *value_out = pc_value;
    return true;
  }

  if (reg_index < REG_PC && const_values &&
      (const_mask & (1u << reg_index)))
  {
    *value_out = const_values[reg_index];
    return true;
  }

  return false;
}

static u32 riscv_arm_const_ror(u32 value, u32 shift)
{
  shift &= 31u;
  if (!shift)
    return value;

  return (value >> shift) | (value << (32u - shift));
}

static bool riscv_arm_const_imm_shift(u32 value,
                                      u32 shift_type,
                                      u32 shift,
                                      u32 *result_out)
{
  switch (shift_type)
  {
    case 0:
      *result_out = value << shift;
      return true;
    case 1:
      *result_out = shift ? (value >> shift) : 0;
      return true;
    case 2:
      *result_out = (u32)((s32)value >> (shift ? shift : 31u));
      return true;
    default:
      if (!shift)
        return false;
      *result_out = riscv_arm_const_ror(value, shift);
      return true;
  }
}

static bool riscv_arm_const_reg_shift(u32 value,
                                      u32 shift_type,
                                      u32 shift,
                                      u32 *result_out)
{
  shift &= 0xffu;

  switch (shift_type)
  {
    case 0:
      if (!shift)
        *result_out = value;
      else if (shift < 32u)
        *result_out = value << shift;
      else
        *result_out = 0;
      return true;
    case 1:
      if (!shift)
        *result_out = value;
      else if (shift < 32u)
        *result_out = value >> shift;
      else
        *result_out = 0;
      return true;
    case 2:
      if (!shift)
        *result_out = value;
      else if (shift < 32u)
        *result_out = (u32)((s32)value >> shift);
      else
        *result_out = (u32)((s32)value >> 31);
      return true;
    default:
      if (!shift)
        *result_out = value;
      else
        *result_out = riscv_arm_const_ror(value, shift);
      return true;
  }
}

static bool riscv_arm_const_operand2(u32 opcode,
                                     u32 pc,
                                     u32 const_mask,
                                     const u32 *const_values,
                                     u32 *value_out)
{
  u32 imm_op = (opcode >> 25) & 1u;
  u32 rm = opcode & 0xfu;
  u32 rs = (opcode >> 8) & 0xfu;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;
  u32 value;

  if (imm_op)
  {
    *value_out = riscv_arm_expand_imm(opcode);
    return true;
  }

  if ((opcode >> 4) & 1u)
  {
    u32 shift_value;

    if (!riscv_arm_const_reg_value(rm, pc + 12u, const_mask,
                                   const_values, &value) ||
        !riscv_arm_const_reg_value(rs, pc + 8u, const_mask,
                                   const_values, &shift_value))
    {
      return false;
    }

    return riscv_arm_const_reg_shift(value, shift_type, shift_value,
                                     value_out);
  }

  if (!riscv_arm_const_reg_value(rm, pc + 8u, const_mask,
                                 const_values, &value))
  {
    return false;
  }

  return riscv_arm_const_imm_shift(value, shift_type, shift, value_out);
}

static bool riscv_arm_data_proc_const_result(u32 opcode,
                                             u32 pc,
                                             u32 const_mask,
                                             const u32 *const_values,
                                             u32 *result_out)
{
  u32 op = (opcode >> 21) & 0xfu;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 operand1 = 0;
  u32 operand2;

  if (op == 0x5 || op == 0x6 || op == 0x7)
    return false;

  if (op != 0xdu && op != 0xfu &&
      !riscv_arm_const_reg_value(rn, pc + 8u, const_mask,
                                 const_values, &operand1))
  {
    return false;
  }

  if (!riscv_arm_const_operand2(opcode, pc, const_mask, const_values,
                                &operand2))
  {
    return false;
  }

  switch (op)
  {
    case 0x0:
      *result_out = operand1 & operand2;
      return true;
    case 0x1:
      *result_out = operand1 ^ operand2;
      return true;
    case 0x2:
      *result_out = operand1 - operand2;
      return true;
    case 0x3:
      *result_out = operand2 - operand1;
      return true;
    case 0x4:
      *result_out = operand1 + operand2;
      return true;
    case 0xc:
      *result_out = operand1 | operand2;
      return true;
    case 0xd:
      *result_out = operand2;
      return true;
    case 0xe:
      *result_out = operand1 & ~operand2;
      return true;
    case 0xf:
      *result_out = ~operand2;
      return true;
    default:
      return false;
  }
}

static u32 riscv_arm_const_nzcv(u32 result, bool carry, bool overflow)
{
  u32 flags = 0;

  if (result & 0x80000000u)
    flags |= 0x08u;
  if (!result)
    flags |= 0x04u;
  if (carry)
    flags |= 0x02u;
  if (overflow)
    flags |= 0x01u;

  return flags;
}

static bool riscv_arm_const_old_carry(u32 known_flag_mask,
                                      u32 known_flags,
                                      u32 *carry_out)
{
  if (!(known_flag_mask & 0x02u))
    return false;

  *carry_out = (known_flags & 0x02u) ? 1u : 0u;
  return true;
}

static bool riscv_arm_const_imm_shift_with_carry(u32 value,
                                                 u32 shift_type,
                                                 u32 shift,
                                                 u32 known_flag_mask,
                                                 u32 known_flags,
                                                 u32 *result_out,
                                                 u32 *carry_known_out,
                                                 u32 *carry_out)
{
  u32 old_carry;

  *carry_known_out = 1u;

  switch (shift_type)
  {
    case 0:
      *result_out = value << shift;
      if (shift)
        *carry_out = (value >> (32u - shift)) & 1u;
      else if (!riscv_arm_const_old_carry(known_flag_mask, known_flags,
                                          &old_carry))
        *carry_known_out = 0;
      else
        *carry_out = old_carry;
      return true;
    case 1:
      if (shift)
      {
        *result_out = value >> shift;
        *carry_out = (value >> (shift - 1u)) & 1u;
      }
      else
      {
        *result_out = 0;
        *carry_out = value >> 31;
      }
      return true;
    case 2:
      if (shift)
      {
        *result_out = (u32)((s32)value >> shift);
        *carry_out = (value >> (shift - 1u)) & 1u;
      }
      else
      {
        *result_out = (u32)((s32)value >> 31);
        *carry_out = value >> 31;
      }
      return true;
    default:
      if (shift)
      {
        *result_out = riscv_arm_const_ror(value, shift);
        *carry_out = (value >> (shift - 1u)) & 1u;
        return true;
      }
      if (!riscv_arm_const_old_carry(known_flag_mask, known_flags,
                                     &old_carry))
        return false;
      *result_out = (old_carry << 31) | (value >> 1);
      *carry_out = value & 1u;
      return true;
  }
}

static bool riscv_arm_const_reg_shift_with_carry(u32 value,
                                                 u32 shift_type,
                                                 u32 shift,
                                                 u32 known_flag_mask,
                                                 u32 known_flags,
                                                 u32 *result_out,
                                                 u32 *carry_known_out,
                                                 u32 *carry_out)
{
  u32 old_carry;

  shift &= 0xffu;
  *carry_known_out = 1u;

  if (!shift)
  {
    *result_out = value;
    if (!riscv_arm_const_old_carry(known_flag_mask, known_flags,
                                   &old_carry))
      *carry_known_out = 0;
    else
      *carry_out = old_carry;
    return true;
  }

  switch (shift_type)
  {
    case 0:
      if (shift < 32u)
      {
        *result_out = value << shift;
        *carry_out = (value >> (32u - shift)) & 1u;
      }
      else
      {
        *result_out = 0;
        *carry_out = (shift == 32u) ? (value & 1u) : 0u;
      }
      return true;
    case 1:
      if (shift < 32u)
      {
        *result_out = value >> shift;
        *carry_out = (value >> (shift - 1u)) & 1u;
      }
      else
      {
        *result_out = 0;
        *carry_out = (shift == 32u) ? (value >> 31) : 0u;
      }
      return true;
    case 2:
      if (shift < 32u)
      {
        *result_out = (u32)((s32)value >> shift);
        *carry_out = (value >> (shift - 1u)) & 1u;
      }
      else
      {
        *result_out = (u32)((s32)value >> 31);
        *carry_out = value >> 31;
      }
      return true;
    default:
      *result_out = riscv_arm_const_ror(value, shift);
      *carry_out = (value >> ((shift - 1u) & 31u)) & 1u;
      return true;
  }
}

static bool riscv_arm_const_operand2_with_carry(u32 opcode,
                                                u32 pc,
                                                u32 const_mask,
                                                const u32 *const_values,
                                                u32 known_flag_mask,
                                                u32 known_flags,
                                                u32 *value_out,
                                                u32 *carry_known_out,
                                                u32 *carry_out)
{
  u32 imm_op = (opcode >> 25) & 1u;
  u32 rm = opcode & 0xfu;
  u32 rs = (opcode >> 8) & 0xfu;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;
  u32 value;

  *carry_known_out = 0;
  *carry_out = 0;

  if (imm_op)
  {
    *value_out = riscv_arm_expand_imm(opcode);
    if ((opcode >> 8) & 0xfu)
    {
      *carry_known_out = 1;
      *carry_out = *value_out >> 31;
    }
    else if (riscv_arm_const_old_carry(known_flag_mask, known_flags,
                                       carry_out))
    {
      *carry_known_out = 1;
    }
    return true;
  }

  if ((opcode >> 4) & 1u)
  {
    u32 shift_value;

    if (!riscv_arm_const_reg_value(rm, pc + 12u, const_mask,
                                   const_values, &value) ||
        !riscv_arm_const_reg_value(rs, pc + 8u, const_mask,
                                   const_values, &shift_value))
    {
      return false;
    }

    return riscv_arm_const_reg_shift_with_carry(
      value, shift_type, shift_value, known_flag_mask, known_flags,
      value_out, carry_known_out, carry_out);
  }

  if (!riscv_arm_const_reg_value(rm, pc + 8u, const_mask,
                                 const_values, &value))
  {
    return false;
  }

  return riscv_arm_const_imm_shift_with_carry(
    value, shift_type, shift, known_flag_mask, known_flags, value_out,
    carry_known_out, carry_out);
}

bool riscv_arm_const_data_proc_test_flags(u32 opcode,
                                          u32 pc,
                                          u32 const_mask,
                                          const u32 *const_values,
                                          u32 *flag_mask_out,
                                          u32 *flags_out)
{
  u32 op = (opcode >> 21) & 0xfu;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 operand1;
  u32 operand2;
  u32 result;
  bool carry;
  bool overflow;

  if (!flag_mask_out || !flags_out ||
      (opcode >> 28) != 0x0eu || !((opcode >> 20) & 1u))
    return false;

  if (op != 0x08u && op != 0x09u && op != 0x0au && op != 0x0bu)
    return false;

  if (!riscv_arm_const_reg_value(rn, pc + 8u, const_mask,
                                 const_values, &operand1) ||
      !riscv_arm_const_operand2(opcode, pc, const_mask, const_values,
                                &operand2))
  {
    return false;
  }

  if (op == 0x08u || op == 0x09u)
  {
    result = (op == 0x08u) ? (operand1 & operand2) :
                             (operand1 ^ operand2);
    *flag_mask_out = 0x0cu;
    *flags_out = riscv_arm_const_nzcv(result, false, false);
    return true;
  }

  if (op == 0x0au)
  {
    result = operand1 - operand2;
    carry = operand1 >= operand2;
    overflow = (((operand1 ^ operand2) & (operand1 ^ result)) >> 31) != 0;
  }
  else
  {
    result = operand1 + operand2;
    carry = result < operand1;
    overflow = ((~(operand1 ^ operand2) & (operand1 ^ result)) >> 31) != 0;
  }

  *flag_mask_out = 0x0fu;
  *flags_out = riscv_arm_const_nzcv(result, carry, overflow);
  return true;
}

bool riscv_arm_const_data_proc_flags(u32 opcode,
                                     u32 pc,
                                     u32 const_mask,
                                     const u32 *const_values,
                                     u32 known_flag_mask,
                                     u32 known_flags,
                                     u32 *flag_mask_out,
                                     u32 *flags_out)
{
  u32 op = (opcode >> 21) & 0xfu;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 operand1 = 0;
  u32 operand2;
  u32 result;
  u32 shifter_carry_known = 0;
  u32 shifter_carry = 0;
  u32 carry_in = 0;
  bool carry;
  bool overflow;

  if (!flag_mask_out || !flags_out ||
      (opcode >> 28) != 0x0eu || !((opcode >> 20) & 1u) ||
      rd == REG_PC)
  {
    return false;
  }

  if (op == 0x8u || op == 0x9u || op == 0xau || op == 0xbu)
    return false;

  if (op != 0xdu && op != 0xfu &&
      !riscv_arm_const_reg_value(rn, pc + 8u, const_mask,
                                 const_values, &operand1))
  {
    return false;
  }

  if (!riscv_arm_const_operand2_with_carry(
        opcode, pc, const_mask, const_values, known_flag_mask, known_flags,
        &operand2, &shifter_carry_known, &shifter_carry))
  {
    return false;
  }

  *flag_mask_out = 0;
  *flags_out = 0;

  switch (op)
  {
    case 0x0:
      result = operand1 & operand2;
      break;
    case 0x1:
      result = operand1 ^ operand2;
      break;
    case 0xcu:
      result = operand1 | operand2;
      break;
    case 0xdu:
      result = operand2;
      break;
    case 0xeu:
      result = operand1 & ~operand2;
      break;
    case 0xfu:
      result = ~operand2;
      break;
    case 0x2:
      result = operand1 - operand2;
      carry = operand1 >= operand2;
      overflow = (((operand1 ^ operand2) & (operand1 ^ result)) >> 31) != 0;
      *flag_mask_out = 0x0fu;
      *flags_out = riscv_arm_const_nzcv(result, carry, overflow);
      return true;
    case 0x3:
      result = operand2 - operand1;
      carry = operand2 >= operand1;
      overflow = (((operand2 ^ operand1) & (operand2 ^ result)) >> 31) != 0;
      *flag_mask_out = 0x0fu;
      *flags_out = riscv_arm_const_nzcv(result, carry, overflow);
      return true;
    case 0x4:
      result = operand1 + operand2;
      carry = result < operand1;
      overflow = ((~(operand1 ^ operand2) & (operand1 ^ result)) >> 31) != 0;
      *flag_mask_out = 0x0fu;
      *flags_out = riscv_arm_const_nzcv(result, carry, overflow);
      return true;
    case 0x5:
      if (!riscv_arm_const_old_carry(known_flag_mask, known_flags,
                                     &carry_in))
        return false;
      {
        u64 sum = (u64)operand1 + operand2 + carry_in;

        result = (u32)sum;
        carry = (sum >> 32) != 0;
      }
      overflow = ((~(operand1 ^ operand2) & (operand1 ^ result)) >> 31) != 0;
      *flag_mask_out = 0x0fu;
      *flags_out = riscv_arm_const_nzcv(result, carry, overflow);
      return true;
    case 0x6:
      if (!riscv_arm_const_old_carry(known_flag_mask, known_flags,
                                     &carry_in))
        return false;
      {
        u64 rhs = (u64)operand2 + (carry_in ? 0u : 1u);

        result = (u32)((u64)operand1 - rhs);
        carry = (u64)operand1 >= rhs;
      }
      overflow = (((operand1 ^ operand2) & (operand1 ^ result)) >> 31) != 0;
      *flag_mask_out = 0x0fu;
      *flags_out = riscv_arm_const_nzcv(result, carry, overflow);
      return true;
    case 0x7:
      if (!riscv_arm_const_old_carry(known_flag_mask, known_flags,
                                     &carry_in))
        return false;
      {
        u64 rhs = (u64)operand1 + (carry_in ? 0u : 1u);

        result = (u32)((u64)operand2 - rhs);
        carry = (u64)operand2 >= rhs;
      }
      overflow = (((operand2 ^ operand1) & (operand2 ^ result)) >> 31) != 0;
      *flag_mask_out = 0x0fu;
      *flags_out = riscv_arm_const_nzcv(result, carry, overflow);
      return true;
    default:
      return false;
  }

  *flag_mask_out = 0x0cu;
  *flags_out = riscv_arm_const_nzcv(result, false, false) & 0x0cu;
  if (shifter_carry_known)
  {
    *flag_mask_out |= 0x02u;
    if (shifter_carry)
      *flags_out |= 0x02u;
  }
  if (known_flag_mask & 0x01u)
  {
    *flag_mask_out |= 0x01u;
    *flags_out |= known_flags & 0x01u;
  }
  return true;
}

bool riscv_arm_const_condition_passed(u32 flag_mask,
                                      u32 flags,
                                      u32 condition,
                                      bool *passed_out)
{
  bool n = (flags & 0x08u) != 0;
  bool z = (flags & 0x04u) != 0;
  bool c = (flags & 0x02u) != 0;
  bool v = (flags & 0x01u) != 0;

  if (!passed_out)
    return false;

  switch (condition & 0x0fu)
  {
    case 0x0:
    case 0x1:
      if (!(flag_mask & 0x04u))
        return false;
      *passed_out = z == ((condition & 1u) == 0);
      return true;
    case 0x2:
    case 0x3:
      if (!(flag_mask & 0x02u))
        return false;
      *passed_out = c == ((condition & 1u) == 0);
      return true;
    case 0x4:
    case 0x5:
      if (!(flag_mask & 0x08u))
        return false;
      *passed_out = n == ((condition & 1u) == 0);
      return true;
    case 0x6:
    case 0x7:
      if (!(flag_mask & 0x01u))
        return false;
      *passed_out = v == ((condition & 1u) == 0);
      return true;
    case 0x8:
    case 0x9:
      if ((flag_mask & 0x06u) != 0x06u)
        return false;
      *passed_out = (condition == 0x8u) ? (c && !z) : (!c || z);
      return true;
    case 0x0a:
    case 0x0b:
      if ((flag_mask & 0x09u) != 0x09u)
        return false;
      *passed_out = (condition == 0x0au) ? (n == v) : (n != v);
      return true;
    case 0x0c:
    case 0x0d:
      if ((flag_mask & 0x0du) != 0x0du)
        return false;
      *passed_out = (condition == 0x0cu) ? (!z && (n == v)) :
                                           (z || (n != v));
      return true;
    case 0x0e:
      *passed_out = true;
      return true;
    default:
      return false;
  }
}

void riscv_arm_const_update_data_proc(u32 opcode,
                                      u32 pc,
                                      u32 condition,
                                      u32 *const_mask,
                                      u32 *const_values)
{
  u32 rd = (opcode >> 12) & 0xfu;
  u32 value;

  if (rd >= REG_PC)
  {
    riscv_arm_const_clear_reg(const_mask, rd);
    return;
  }

  if ((condition & 0x0fu) != 0x0eu)
  {
    riscv_arm_const_clear_reg(const_mask, rd);
    return;
  }

  if (riscv_arm_data_proc_const_result(opcode, pc, *const_mask,
                                       const_values, &value))
    riscv_arm_const_set_reg(const_mask, const_values, rd, value);
  else
    riscv_arm_const_clear_reg(const_mask, rd);
}

static bool riscv_arm_memory_reg_offset_const(u32 opcode,
                                              u32 pc,
                                              u32 const_mask,
                                              const u32 *const_values,
                                              u32 *offset_out);

void riscv_arm_const_update_access_memory(u32 opcode,
                                          u32 pc,
                                          u32 condition,
                                          u32 *const_mask,
                                          u32 *const_values)
{
  u32 pre_index = (opcode >> 24) & 1u;
  u32 up = (opcode >> 23) & 1u;
  u32 writeback = (opcode >> 21) & 1u;
  u32 load = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 base_value = 0;
  u32 offset = 0;
  bool offset_valid = false;
  bool writeback_address = writeback || !pre_index;
  bool writeback_overwritten_by_load = load && rd == rn;

  /* The native emitter receives an AL-rewritten opcode because its runtime
   * condition gate surrounds the emitted body.  Constant propagation still
   * has to merge the executed and skipped paths.  A conditional access may
   * change rd/rn, but it may not invent the post-writeback value as though the
   * access were unconditional. */
  if ((condition & 0x0fu) != 0x0eu)
  {
    if (load)
      riscv_arm_const_clear_reg(const_mask, rd);
    if (writeback_address)
      riscv_arm_const_clear_reg(const_mask, rn);
    return;
  }

  if ((opcode & 0x0c000000u) == 0x04000000u)
  {
    if ((opcode >> 25) & 1u)
    {
      offset_valid = riscv_arm_memory_reg_offset_const(
        opcode, pc, const_mask ? *const_mask : 0, const_values, &offset);
    }
    else
    {
      offset = opcode & 0xfffu;
      offset_valid = true;
    }
  }
  else if ((opcode & 0x0e000090u) == 0x00000090u)
  {
    if ((opcode >> 22) & 1u)
    {
      offset = ((opcode >> 4) & 0xf0u) | (opcode & 0x0fu);
      offset_valid = true;
    }
    else
    {
      u32 rm = opcode & 0x0fu;
      offset_valid = riscv_arm_const_reg_value(
        rm, pc + 8u, const_mask ? *const_mask : 0, const_values, &offset);
    }
  }

  if (load)
    riscv_arm_const_clear_reg(const_mask, rd);

  if (!writeback_address)
    return;

  if (writeback_overwritten_by_load)
  {
    riscv_arm_const_clear_reg(const_mask, rn);
    return;
  }

  if (riscv_arm_const_reg_value(rn, pc + 8u, const_mask ? *const_mask : 0,
                                const_values, &base_value) &&
      offset_valid)
  {
    u32 writeback_value = up ? (base_value + offset) : (base_value - offset);
    riscv_arm_const_set_reg(const_mask, const_values, rn, writeback_value);
  }
  else
  {
    riscv_arm_const_clear_reg(const_mask, rn);
  }
}

void riscv_arm_const_update_block_memory(u32 opcode, u32 *const_mask)
{
  u32 sbit = (opcode >> 22) & 1u;
  u32 writeback = (opcode >> 21) & 1u;
  u32 load = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 reglist = opcode & 0xffffu;

  if (sbit || rn == REG_PC)
  {
    if (const_mask)
      *const_mask = 0;
    return;
  }

  if (load)
  {
    if (reglist & (1u << REG_PC))
    {
      if (const_mask)
        *const_mask = 0;
    }
    else if (const_mask)
    {
      *const_mask &= ~reglist;
    }
  }

  if (writeback)
    riscv_arm_const_clear_reg(const_mask, rn);
}

void riscv_arm_const_update_multiply(u32 opcode, u32 *const_mask)
{
  riscv_arm_const_clear_reg(const_mask, (opcode >> 16) & 0xfu);
}

void riscv_arm_const_update_multiply_long(u32 opcode, u32 *const_mask)
{
  riscv_arm_const_clear_reg(const_mask, (opcode >> 12) & 0xfu);
  riscv_arm_const_clear_reg(const_mask, (opcode >> 16) & 0xfu);
}

void riscv_arm_const_update_psr(u32 opcode, u32 *const_mask)
{
  if ((opcode & 0x0fbf0fffu) == 0x010f0000u)
    riscv_arm_const_clear_reg(const_mask, (opcode >> 12) & 0xfu);
}

static bool riscv_thumb_const_arm_opcode(u32 opcode,
                                         u32 *arm_opcode_out,
                                         bool *test_op_out)
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

  if (hi >= 0x18u && hi <= 0x1fu)
  {
    arm_op = (hi & 0x02u) ? 0x2u : 0x4u;
    arm_rn = rs;
    if (hi & 0x04u)
    {
      immediate = true;
      arm_operand2 = rn;
    }
    else
    {
      arm_operand2 = rn;
    }
  }
  else if (hi >= 0x20u && hi <= 0x27u)
  {
    arm_op = 0xdu;
    arm_rd = hi & 7u;
    immediate = true;
    arm_operand2 = imm;
  }
  else if (hi >= 0x28u && hi <= 0x2fu)
  {
    arm_op = 0xau;
    arm_rn = hi & 7u;
    immediate = true;
    arm_operand2 = imm;
    test_op = true;
  }
  else if (hi >= 0x30u && hi <= 0x3fu)
  {
    arm_op = (hi & 0x08u) ? 0x2u : 0x4u;
    arm_rn = hi & 7u;
    arm_rd = arm_rn;
    immediate = true;
    arm_operand2 = imm;
  }
  else if (hi == 0x40u)
  {
    if (alu_op > 1u)
      return false;
    arm_op = alu_op;
    arm_rn = rd;
    arm_operand2 = rs;
  }
  else if (hi == 0x41u)
  {
    if (alu_op != 1u && alu_op != 2u)
      return false;
    arm_op = alu_op == 1u ? 0x5u : 0x6u;
    arm_rn = rd;
    arm_operand2 = rs;
  }
  else if (hi == 0x42u)
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
  else if (hi == 0x43u)
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
        return false;
    }
  }
  else
  {
    return false;
  }

  if (arm_opcode_out)
  {
    *arm_opcode_out = (0xeu << 28) | (immediate ? (1u << 25) : 0u) |
      (arm_op << 21) | (1u << 20) | (arm_rn << 16) |
      (arm_rd << 12) | arm_operand2;
  }
  if (test_op_out)
    *test_op_out = test_op;
  return true;
}

static void riscv_thumb_const_set_flags(u32 flag_status,
                                        u32 computed_mask,
                                        u32 computed_flags,
                                        u32 *known_flag_mask,
                                        u32 *known_flags)
{
  u32 live_mask = flag_status & 0x0fu;

  if (!live_mask)
    return;

  computed_mask &= live_mask;
  computed_flags &= computed_mask;
  /* Only flags in live_mask are written by this instruction.  Thumb logical
   * operations such as TST/MOV/AND update N/Z while architecturally preserving
   * C/V; replacing the whole lattice here loses those preserved constants and
   * can make a following condition use the wrong producer. */
  *known_flag_mask = (*known_flag_mask & ~live_mask) | computed_mask;
  *known_flags = (*known_flags & ~live_mask) | computed_flags;
  *known_flags &= *known_flag_mask;
}

static void riscv_thumb_const_clear_live_flags(u32 flag_status,
                                               u32 *known_flag_mask,
                                               u32 *known_flags)
{
  u32 live_mask = flag_status & 0x0fu;

  *known_flag_mask &= ~live_mask;
  *known_flags &= *known_flag_mask;
}

static void riscv_thumb_const_update_data_proc(u32 opcode,
                                               u32 pc,
                                               u32 flag_status,
                                               u32 *const_mask,
                                               u32 *const_values,
                                               u32 *known_flag_mask,
                                               u32 *known_flags)
{
  u32 arm_opcode;
  bool test_op;

  if (!riscv_thumb_const_arm_opcode(opcode, &arm_opcode, &test_op))
    return;

  if (flag_status & 0x0fu)
  {
    u32 flag_mask = 0;
    u32 flags = 0;
    bool known = test_op ?
      riscv_arm_const_data_proc_test_flags(
        arm_opcode, pc, *const_mask, const_values, &flag_mask, &flags) :
      riscv_arm_const_data_proc_flags(
        arm_opcode, pc, *const_mask, const_values, *known_flag_mask,
        *known_flags, &flag_mask, &flags);

    if (known)
      riscv_thumb_const_set_flags(flag_status, flag_mask, flags,
                                  known_flag_mask, known_flags);
    else
      riscv_thumb_const_clear_live_flags(flag_status, known_flag_mask,
                                         known_flags);
  }

  if (!test_op)
    riscv_arm_const_update_data_proc(arm_opcode, pc, 0x0eu, const_mask,
                                     const_values);
}

static bool riscv_thumb_const_reg_value(u32 reg,
                                        u32 pc_value,
                                        u32 const_mask,
                                        const u32 *const_values,
                                        u32 *value_out)
{
  if (reg == REG_PC)
  {
    *value_out = pc_value;
    return true;
  }

  if (reg < 16u && (const_mask & (1u << reg)))
  {
    *value_out = const_values[reg];
    return true;
  }

  return false;
}

static u32 riscv_const_read_u32_le(const u8 *base, u32 offset)
{
  return ((u32)base[offset]) |
         ((u32)base[offset + 1u] << 8) |
         ((u32)base[offset + 2u] << 16) |
         ((u32)base[offset + 3u] << 24);
}

static bool riscv_thumb_const_shift(u32 opcode,
                                    u32 pc,
                                    u32 flag_status,
                                    u32 *const_mask,
                                    u32 *const_values,
                                    u32 *known_flag_mask,
                                    u32 *known_flags)
{
  u32 hi = opcode >> 8;
  u32 alu_op = (opcode >> 6) & 3u;
  u32 rd = opcode & 7u;
  u32 rs = (opcode >> 3) & 7u;
  u32 shift_type;
  u32 shift = 0;
  u32 value = 0;
  u32 result = 0;
  u32 carry_known = 0;
  u32 carry = 0;
  bool result_known = false;

  if (hi <= 0x17u)
  {
    shift_type = hi >> 3;
    shift = (opcode >> 6) & 0x1fu;
    if (riscv_thumb_const_reg_value(rs, pc + 4u, *const_mask, const_values,
                                    &value))
    {
      result_known = riscv_arm_const_imm_shift_with_carry(
        value, shift_type, shift, *known_flag_mask, *known_flags,
        &result, &carry_known, &carry);
    }
  }
  else if ((hi == 0x40u && alu_op >= 2u) ||
           (hi == 0x41u && (alu_op == 0u || alu_op == 3u)))
  {
    u32 shift_value = 0;

    if (hi == 0x40u)
      shift_type = alu_op == 2u ? 0u : 1u;
    else
      shift_type = alu_op == 0u ? 2u : 3u;

    if (riscv_thumb_const_reg_value(rd, pc + 4u, *const_mask, const_values,
                                    &value) &&
        riscv_thumb_const_reg_value(rs, pc + 4u, *const_mask, const_values,
                                    &shift_value))
    {
      result_known = riscv_arm_const_reg_shift_with_carry(
        value, shift_type, shift_value, *known_flag_mask, *known_flags,
        &result, &carry_known, &carry);
    }
  }
  else
  {
    return false;
  }

  if (result_known)
    riscv_arm_const_set_reg(const_mask, const_values, rd, result);
  else
    riscv_arm_const_clear_reg(const_mask, rd);

  if (flag_status & 0x0fu)
  {
    u32 flag_mask = 0;
    u32 flags = 0;

    if (result_known)
    {
      flag_mask = flag_status & 0x0cu;
      flags = riscv_arm_const_nzcv(result, false, false) & flag_mask;
      if (carry_known && (flag_status & 0x02u))
      {
        flag_mask |= 0x02u;
        if (carry)
          flags |= 0x02u;
      }
    }
    riscv_thumb_const_set_flags(flag_status, flag_mask, flags,
                                known_flag_mask, known_flags);
  }
  return true;
}

static bool riscv_thumb_const_mul(u32 opcode,
                                  u32 flag_status,
                                  u32 *const_mask,
                                  u32 *const_values,
                                  u32 *known_flag_mask,
                                  u32 *known_flags)
{
  u32 hi = opcode >> 8;
  u32 alu_op = (opcode >> 6) & 3u;
  u32 rd = opcode & 7u;
  u32 rs = (opcode >> 3) & 7u;
  u32 lhs = 0;
  u32 rhs = 0;
  u32 result = 0;
  bool known;

  if (hi != 0x43u || alu_op != 1u)
    return false;

  known = riscv_thumb_const_reg_value(rd, 0, *const_mask, const_values,
                                      &lhs) &&
          riscv_thumb_const_reg_value(rs, 0, *const_mask, const_values,
                                      &rhs);
  if (known)
  {
    result = lhs * rhs;
    riscv_arm_const_set_reg(const_mask, const_values, rd, result);
  }
  else
  {
    riscv_arm_const_clear_reg(const_mask, rd);
  }

  if (flag_status & 0x0fu)
  {
    u32 flag_mask = known ? (flag_status & 0x0cu) : 0;
    u32 flags = known ? (riscv_arm_const_nzcv(result, false, false) &
                         flag_mask) : 0;

    riscv_thumb_const_set_flags(flag_status, flag_mask, flags,
                                known_flag_mask, known_flags);
  }
  return true;
}

static void riscv_thumb_const_update_hi(u32 opcode,
                                        u32 pc,
                                        u32 flag_status,
                                        u32 *const_mask,
                                        u32 *const_values,
                                        u32 *known_flag_mask,
                                        u32 *known_flags)
{
  u32 hi = opcode >> 8;
  u32 rs = (opcode >> 3) & 0x0fu;
  u32 rd = ((opcode >> 4) & 0x08u) | (opcode & 0x07u);
  u32 lhs = 0;
  u32 rhs = 0;

  if (hi == 0x44u)
  {
    if (rd == REG_PC)
    {
      *const_mask = 0;
      riscv_thumb_const_clear_live_flags(0x0fu, known_flag_mask, known_flags);
    }
    else if (riscv_thumb_const_reg_value(rd, pc + 4u, *const_mask,
                                         const_values, &lhs) &&
             riscv_thumb_const_reg_value(rs, pc + 4u, *const_mask,
                                         const_values, &rhs))
    {
      riscv_arm_const_set_reg(const_mask, const_values, rd, lhs + rhs);
    }
    else
    {
      riscv_arm_const_clear_reg(const_mask, rd);
    }
  }
  else if (hi == 0x45u)
  {
    if (flag_status & 0x0fu)
    {
      if (riscv_thumb_const_reg_value(rd, pc + 4u, *const_mask,
                                      const_values, &lhs) &&
          riscv_thumb_const_reg_value(rs, pc + 4u, *const_mask,
                                      const_values, &rhs))
      {
        u32 result = lhs - rhs;
        u32 flags = riscv_arm_const_nzcv(
          result, lhs >= rhs,
          (((lhs ^ rhs) & (lhs ^ result)) >> 31) != 0);
        riscv_thumb_const_set_flags(flag_status, 0x0fu, flags,
                                    known_flag_mask, known_flags);
      }
      else
      {
        riscv_thumb_const_clear_live_flags(flag_status, known_flag_mask,
                                           known_flags);
      }
    }
  }
  else if (hi == 0x46u)
  {
    if (rd == REG_PC)
    {
      *const_mask = 0;
      riscv_thumb_const_clear_live_flags(0x0fu, known_flag_mask, known_flags);
    }
    else if (riscv_thumb_const_reg_value(rs, pc + 4u, *const_mask,
                                         const_values, &rhs))
    {
      riscv_arm_const_set_reg(const_mask, const_values, rd, rhs);
    }
    else
    {
      riscv_arm_const_clear_reg(const_mask, rd);
    }
  }
}

void riscv_thumb_const_update(u32 opcode,
                              u32 pc,
                              u32 flag_status,
                              bool ram_region,
                              const u8 *pc_address_block,
                              u32 *const_mask,
                              u32 *const_values,
                              u32 *known_flag_mask,
                              u32 *known_flags)
{
  u32 hi = opcode >> 8;
  u32 rd = opcode & 7u;
  u32 imm = opcode & 0xffu;

  if (!const_mask || !const_values || !known_flag_mask || !known_flags)
    return;

  if (riscv_thumb_const_shift(opcode, pc, flag_status, const_mask,
                              const_values, known_flag_mask, known_flags))
  {
    return;
  }

  if (riscv_thumb_const_mul(opcode, flag_status, const_mask, const_values,
                            known_flag_mask, known_flags))
  {
    return;
  }

  if (riscv_thumb_const_arm_opcode(opcode, NULL, NULL))
  {
    riscv_thumb_const_update_data_proc(opcode, pc, flag_status, const_mask,
                                       const_values, known_flag_mask,
                                       known_flags);
    return;
  }

  if (hi >= 0x44u && hi <= 0x46u)
  {
    riscv_thumb_const_update_hi(opcode, pc, flag_status, const_mask,
                                const_values, known_flag_mask, known_flags);
    return;
  }

  if (hi == 0x47u)
  {
    *const_mask = 0;
    *known_flag_mask = 0;
    *known_flags = 0;
    return;
  }

  if (hi >= 0x48u && hi <= 0x4fu)
  {
    u32 aoff = (pc & ~2u) + (imm * 4u) + 4u;
    rd = hi & 7u;
    if (!ram_region && pc_address_block &&
        (((aoff + 4u) >> 15) == (pc >> 15)))
    {
      riscv_arm_const_set_reg(const_mask, const_values, rd,
                              riscv_const_read_u32_le(
                                pc_address_block, aoff & 0x7fffu));
    }
    else
    {
      riscv_arm_const_clear_reg(const_mask, rd);
    }
    return;
  }

  if (hi >= 0x50u && hi <= 0x9fu)
  {
    bool load = false;

    if (hi < 0x60u)
    {
      /* Register-offset access types 0..2 are stores; 3 is LDRSB and
       * 4..7 are the other loads.  Testing only bit 11 misses LDRSB and
       * leaves a stale compile-time constant attached to its destination. */
      load = ((opcode >> 9) & 7u) >= 3u;
    }
    else if (hi < 0x90u)
      load = (hi & 0x08u) != 0;
    else
    {
      /* SP-relative transfers encode rd in the high byte; the low byte is
       * entirely the scaled offset.  Using opcode & 7 here leaves the real
       * LDR destination carrying a stale compile-time constant. */
      load = hi >= 0x98u;
      rd = hi & 7u;
    }

    if (load)
      riscv_arm_const_clear_reg(const_mask, rd);
    return;
  }

  if (hi >= 0xa0u && hi <= 0xafu)
  {
    rd = hi & 7u;
    if (hi < 0xa8u)
    {
      riscv_arm_const_set_reg(const_mask, const_values, rd,
                              (pc & ~2u) + 4u + (imm * 4u));
    }
    else if (*const_mask & (1u << REG_SP))
    {
      riscv_arm_const_set_reg(const_mask, const_values, rd,
                              const_values[REG_SP] + (imm * 4u));
    }
    else
    {
      riscv_arm_const_clear_reg(const_mask, rd);
    }
    return;
  }

  if (hi >= 0xb0u && hi <= 0xb3u)
  {
    u32 offset = (opcode & 0x7fu) * 4u;

    if (*const_mask & (1u << REG_SP))
    {
      if (opcode & 0x80u)
        const_values[REG_SP] -= offset;
      else
        const_values[REG_SP] += offset;
    }
    else
    {
      riscv_arm_const_clear_reg(const_mask, REG_SP);
    }
    return;
  }

  if (hi == 0xb4u || hi == 0xb5u)
  {
    riscv_arm_const_clear_reg(const_mask, REG_SP);
    return;
  }

  if (hi == 0xbcu || hi == 0xbdu)
  {
    *const_mask &= ~(opcode & 0xffu);
    riscv_arm_const_clear_reg(const_mask, REG_SP);
    if (hi == 0xbdu)
    {
      *const_mask = 0;
      *known_flag_mask = 0;
      *known_flags = 0;
    }
    return;
  }

  if (hi >= 0xc0u && hi <= 0xcfu)
  {
    u32 base = hi & 7u;

    if (hi >= 0xc8u)
      *const_mask &= ~(opcode & 0xffu);
    riscv_arm_const_clear_reg(const_mask, base);
    return;
  }

  if (hi >= 0xd0u && hi <= 0xddu)
  {
    bool passed = false;

    if (riscv_arm_const_condition_passed(*known_flag_mask, *known_flags,
                                         hi & 0x0fu, &passed) &&
        !passed)
    {
      return;
    }

    *const_mask = 0;
    *known_flag_mask = 0;
    *known_flags = 0;
    return;
  }

  if (hi >= 0xdeu)
  {
    *const_mask = 0;
    *known_flag_mask = 0;
    *known_flags = 0;
    return;
  }

  riscv_thumb_const_clear_live_flags(flag_status, known_flag_mask,
                                     known_flags);
}

static void riscv_emit_arm_memory_imm_offset(u8 **ptr_ref,
                                             riscv_reg_number rd,
                                             riscv_reg_number rs,
                                             u32 offset,
                                             bool up);
static bool riscv_arm_memory_reg_offset_const(u32 opcode,
                                              u32 pc,
                                              u32 const_mask,
                                              const u32 *const_values,
                                              u32 *offset_out);
static void riscv_emit_arm_memory_const_offset(u8 **ptr_ref,
                                               bool pre_index,
                                               bool up,
                                               u32 const_offset,
                                               riscv_reg_number *writeback_reg);

static void riscv_emit_cycles_set(u8 **ptr_ref, riscv_reg_number rs)
{
  u8 *translation_ptr;

  if (rs == RISCV_CYCLES_REG)
    return;

  translation_ptr = *ptr_ref;
  riscv_emit_addi(RISCV_CYCLES_REG, rs, 0);
  *ptr_ref = translation_ptr;
}

static void riscv_emit_cycles_sub_reg(u8 **ptr_ref, riscv_reg_number rs)
{
  u8 *translation_ptr = *ptr_ref;

  riscv_emit_sub(RISCV_CYCLES_REG, RISCV_CYCLES_REG, rs);
  *ptr_ref = translation_ptr;
}

static void riscv_emit_adjust_cycles(u8 **ptr, u32 cycles)
{
  u8 *translation_ptr;

  if (!cycles)
    return;

  translation_ptr = *ptr;
  if (cycles <= 2047u)
  {
    riscv_emit_addi(RISCV_CYCLES_REG, RISCV_CYCLES_REG, -(int)cycles);
  }
  else
  {
    *ptr = translation_ptr;
    riscv_emit_li(ptr, riscv_reg_t5, cycles);
    translation_ptr = *ptr;
    riscv_emit_sub(RISCV_CYCLES_REG, RISCV_CYCLES_REG, riscv_reg_t5);
  }

  *ptr = translation_ptr;
}

static void riscv_note_c_call_clobbers_mapped_regs(void)
{
  riscv_invalidate_mapped_regs();
}

/* A normal helper often happens to preserve more host registers than its
 * declared contract requires.  That can hide a stale mapped-register bug for
 * months: the emitter marks a slot invalid, but generated code keeps using
 * the old host value and appears correct until a different helper body or
 * compiler version really clobbers it.  The frontend state-contract gate
 * enables this poison pass so every declared clobber becomes observable.
 * Production builds emit nothing. */
static void riscv_emit_test_poison_mapped_regs(u8 **ptr_ref, u32 mask)
{
#if defined(RISCV_RUNTIME_TEST_POISON_MAPPED_REGS)
  u32 slot;

#if !defined(RISCV_RUNTIME_STANDALONE_TEST)
#error "mapped-register poisoning is test-only"
#endif

  mask &= RISCV_MAPPED_REGS_MASK;
  for (slot = 0; slot < RISCV_MAPPED_REG_COUNT; slot++)
  {
    if (mask & (1u << slot))
      riscv_emit_li(ptr_ref, riscv_mapped_host_regs[slot],
                    0xd00d0000u | slot);
  }
#else
  (void)ptr_ref;
  (void)mask;
#endif
}

static bool riscv_entry_setup_optimized(void);
static bool riscv_state_helpers_enabled(void);

static u32 riscv_helper_state_offset_from_stack_offset(u32 stack_offset)
{
  if (stack_offset == RISCV_STACK_HELPER_THUMB_EXECUTE)
    return (REG_USERDEF + RISCV_HELPER_THUMB_EXECUTE) * 4u;

  return (REG_USERDEF +
          (stack_offset - RISCV_STACK_HELPER_READ32) / 4u) * 4u;
}

static void riscv_emit_c_call_address_raw(u8 **ptr, uintptr_t target)
{
  u8 *translation_ptr = *ptr;
  u32 offset = (u32)target - (u32)(uintptr_t)translation_ptr;
  s32 lower = (s32)(((offset & 0xfffu) ^ 0x800u) - 0x800u);
  u32 upper = (offset - (u32)lower) >> 12;

#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_helper_call_sites++;
#endif

  /* AUIPC plus signed JALR-low reaches the complete RV32 address space.
   * Split the wrapping 32-bit delta directly; 64-bit arithmetic here would
   * make cold translation call compiler runtime helpers on RV32. */
  riscv_emit_auipc(riscv_reg_t0, upper);
  riscv_emit_jalr(riscv_reg_ra, riscv_reg_t0, lower);

  *ptr = translation_ptr;
}

static void riscv_emit_c_call_stack_raw(u8 **ptr, u32 stack_offset)
{
  u8 *translation_ptr = *ptr;
  riscv_reg_number helper_base = riscv_reg_sp;
  u32 helper_offset = stack_offset;

  if (riscv_state_helpers_enabled())
  {
    /* REG_USERDEF is backend-owned storage.  Like the mature ARM backend,
     * address immutable helpers through the already-live CPU-state base.
     * This retains the two-instruction indirect-call shape without copying
     * nineteen pointers into every outer JIT stack frame. */
    helper_base = riscv_reg_s0;
    helper_offset =
      riscv_helper_state_offset_from_stack_offset(stack_offset);
  }

#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_helper_call_sites++;
#endif

  riscv_emit_lw(riscv_reg_t0, helper_base, helper_offset);
  riscv_emit_jalr(riscv_reg_ra, riscv_reg_t0, 0);

  *ptr = translation_ptr;
}

static void riscv_emit_c_call_stack(u8 **ptr, u32 stack_offset)
{
  riscv_emit_mapped_regs_flush_dirty(ptr);
  riscv_emit_c_call_stack_raw(ptr, stack_offset);
  riscv_emit_test_poison_mapped_regs(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_mapped_invalidate_sites++;
#endif
  riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
  riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
  riscv_emit_mapped_regs_reload_mask(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
}

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
static void riscv_emit_c_call_address(u8 **ptr, uintptr_t target)
{
  riscv_emit_mapped_regs_flush_dirty(ptr);
  riscv_emit_c_call_address_raw(ptr, target);
  riscv_emit_test_poison_mapped_regs(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_mapped_invalidate_sites++;
#endif
  riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
  riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
  riscv_emit_mapped_regs_reload_mask(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
}
#endif

#if defined(RISCV_RUNTIME_DISABLE_READ_HELPER_OPT)
#define riscv_emit_read_call_stack riscv_emit_c_call_stack
#elif !defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
/* Pure memory reads observe the explicit guest PC and memory subsystem state,
 * but do not observe or mutate guest r0-r14.  Keep callee-saved mappings live,
 * spill only dirty caller-saved mappings that the C ABI will clobber, and
 * lazily reload caller-saved values if a later guest instruction needs them. */
static void riscv_emit_read_call_stack(u8 **ptr, u32 stack_offset)
{
  u8 *translation_ptr = *ptr;

  riscv_emit_sw(riscv_reg_a1, riscv_reg_s0, REG_PC * 4u);
  *ptr = translation_ptr;
  riscv_emit_mapped_regs_flush_mask(
    ptr, riscv_mapped_dirty_mask & RISCV_MAPPED_CALLER_SAVED_MASK);
  riscv_emit_c_call_stack_raw(ptr, stack_offset);
  riscv_emit_test_poison_mapped_regs(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_mapped_invalidate_sites++;
#endif
  riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
  riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
}
#endif

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
static void riscv_emit_read_call_address(u8 **ptr, uintptr_t target)
{
  u8 *translation_ptr = *ptr;

  riscv_emit_sw(riscv_reg_a1, riscv_reg_s0, REG_PC * 4u);
  *ptr = translation_ptr;
  riscv_emit_mapped_regs_flush_mask(
    ptr, riscv_mapped_dirty_mask & RISCV_MAPPED_CALLER_SAVED_MASK);
  riscv_emit_c_call_address_raw(ptr, target);
  riscv_emit_test_poison_mapped_regs(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_mapped_invalidate_sites++;
#endif
  riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
  riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
}
#endif

static bool riscv_fast_ram_reads_enabled(void)
{
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  return !riscv_runtime_perf_disable_fast_ram_reads;
#else
  return true;
#endif
#else
  return false;
#endif
}

static bool riscv_fast_ram_stores_enabled(void)
{
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  return !riscv_runtime_perf_disable_fast_ram_stores;
#else
  return true;
#endif
#else
  return false;
#endif
}

static bool riscv_entry_setup_optimized(void)
{
#if defined(RISCV_RUNTIME_DISABLE_ENTRY_SETUP_OPT)
  return false;
#elif defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  return !riscv_runtime_perf_disable_entry_setup_opt;
#else
  return true;
#endif
}

static bool riscv_state_helpers_enabled(void)
{
#if defined(RISCV_RUNTIME_DISABLE_STATE_HELPER_OPT)
  return false;
#elif defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  return !riscv_runtime_perf_disable_state_helper_opt;
#else
  return true;
#endif
}

static void riscv_emit_memory_read_call_stack(u8 **ptr, u32 stack_offset)
{
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
  if (!riscv_fast_ram_reads_enabled())
  {
    riscv_emit_c_call_stack(ptr, stack_offset);
    return;
  }
  /* The shared stub avoids the C ABI on EWRAM/IWRAM.  A slow-path tail call
   * may clobber a3-a7, so retain the pure-read spill/lazy-reload contract at
   * the call site.  The stub writes the PC only when it actually enters C. */
  riscv_emit_mapped_regs_flush_mask(
    ptr, riscv_mapped_dirty_mask & RISCV_MAPPED_CALLER_SAVED_MASK);
  riscv_emit_c_call_stack_raw(ptr, stack_offset);
  riscv_emit_test_poison_mapped_regs(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_mapped_invalidate_sites++;
#endif
  riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
  riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
#else
  riscv_emit_read_call_stack(ptr, stack_offset);
#endif
}

static void riscv_emit_memory_read_call_stack_known(
  u8 **ptr, u32 stack_offset, uintptr_t direct_target, bool known_nonram)
{
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
  if (riscv_fast_ram_reads_enabled() && known_nonram)
  {
    /* The frontend proved this site cannot reach EWRAM/IWRAM, so avoid a
     * guaranteed-failed region dispatch while retaining the complete C path. */
    riscv_emit_read_call_address(ptr, direct_target);
    return;
  }
#else
  (void)direct_target;
  (void)known_nonram;
#endif
  riscv_emit_memory_read_call_stack(ptr, stack_offset);
}

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
static u32 function_cc riscv_read_u8_pc(u32 address, u32 pc);
static u32 function_cc riscv_read_u16_pc(u32 address, u32 pc);
static u32 function_cc riscv_read_u32_pc(u32 address, u32 pc);
static u32 function_cc riscv_read_s8_pc(u32 address, u32 pc);
static u32 function_cc riscv_read_s16_pc(u32 address, u32 pc);
#define RISCV_THUMB_READ8_TARGET riscv_read_u8_pc
#define RISCV_THUMB_READ16_TARGET riscv_read_u16_pc
#define RISCV_THUMB_READ32_TARGET riscv_read_u32_pc
#define RISCV_THUMB_READS8_TARGET riscv_read_s8_pc
#define RISCV_THUMB_READS16_TARGET riscv_read_s16_pc
#else
#define RISCV_THUMB_READ8_TARGET read_memory8
#define RISCV_THUMB_READ16_TARGET read_memory16
#define RISCV_THUMB_READ32_TARGET read_memory32
#define RISCV_THUMB_READS8_TARGET read_memory8s
#define RISCV_THUMB_READS16_TARGET read_memory16s
#endif

/* The shared RAM classifier helps ARM workloads with dynamic addresses, but
 * it lengthens Thumb's dense stack/register-relative loads.  Keep Thumb on
 * the one complete C-helper contract instead of adding a second narrow-call
 * model solely to retain that classifier. */
static void riscv_emit_thumb_memory_read_call(
  u8 **ptr, u32 stack_offset, uintptr_t direct_target)
{
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
  if (riscv_fast_ram_reads_enabled())
  {
    riscv_emit_c_call_address(ptr, direct_target);
    return;
  }
#else
  (void)direct_target;
#endif
  riscv_emit_memory_read_call_stack(ptr, stack_offset);
}

static void riscv_emit_memory_store_call_stack(u8 **ptr, u32 stack_offset)
{
  if (riscv_fast_ram_stores_enabled())
  {
    /* Keep state authoritative for the rare C/SMC tails, but preserve every
     * mapped host register across the common RAM leaf.  Unlike the generic C
     * call path, a successful RAM store needs no caller-saved reloads. */
    riscv_emit_mapped_regs_flush_dirty(ptr);
    riscv_emit_c_call_stack_raw(ptr, stack_offset);
    return;
  }
  riscv_emit_c_call_stack(ptr, stack_offset);
}

static void riscv_emit_memory_store_call_stack_known(
  u8 **ptr, u32 stack_offset, uintptr_t direct_target, bool known_ram)
{
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
  if (riscv_fast_ram_stores_enabled() && !known_ram)
  {
    riscv_emit_mapped_regs_flush_dirty(ptr);
    riscv_emit_c_call_address_raw(ptr, direct_target);
    riscv_emit_test_poison_mapped_regs(ptr,
                                       RISCV_MAPPED_CALLER_SAVED_MASK);
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
    riscv_perf_mapped_invalidate_sites++;
#endif
    riscv_mapped_valid_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
    riscv_mapped_dirty_mask &= ~RISCV_MAPPED_CALLER_SAVED_MASK;
    riscv_emit_mapped_regs_reload_mask(ptr, RISCV_MAPPED_CALLER_SAVED_MASK);
    return;
  }
#else
  (void)direct_target;
  (void)known_ram;
#endif
  riscv_emit_memory_store_call_stack(ptr, stack_offset);
}

static void riscv_emit_stateful_c_call_stack(u8 **ptr, u32 stack_offset,
                                             bool reload_after)
{
  riscv_emit_mapped_regs_flush_dirty(ptr);
  riscv_emit_c_call_stack_raw(ptr, stack_offset);
  riscv_emit_test_poison_mapped_regs(ptr, RISCV_MAPPED_REGS_MASK);
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_mapped_invalidate_sites++;
#endif
  riscv_note_c_call_clobbers_mapped_regs();
  if (reload_after)
    riscv_emit_mapped_regs_reload(ptr);
}

static u32 riscv_encode_i(riscv_opcode opcode,
                          u32 funct3,
                          riscv_reg_number rd,
                          riscv_reg_number rs1,
                          s32 imm)
{
  return (((u32)imm & 0xfff) << 20) |
         (((u32)rs1 & 0x1f) << 15) |
         ((funct3 & 0x07) << 12) |
         (((u32)rd & 0x1f) << 7) |
         opcode;
}

static u32 riscv_encode_u(riscv_opcode opcode,
                          riscv_reg_number rd,
                          u32 imm20)
{
  return ((imm20 & 0xfffff) << 12) |
         (((u32)rd & 0x1f) << 7) |
         opcode;
}

static u32 riscv_encode_b_inst(u32 funct3,
                               riscv_reg_number rs1,
                               riscv_reg_number rs2,
                               s32 offset)
{
  return ((((u32)offset >> 12) & 0x01) << 31) |
         ((((u32)offset >> 5) & 0x3f) << 25) |
         (((u32)rs2 & 0x1f) << 20) |
         (((u32)rs1 & 0x1f) << 15) |
         ((funct3 & 0x07) << 12) |
         ((((u32)offset >> 1) & 0x0f) << 8) |
         ((((u32)offset >> 11) & 0x01) << 7) |
         riscv_opcode_branch;
}

static u32 riscv_encode_j_inst(riscv_reg_number rd, s32 offset)
{
  return ((((u32)offset >> 20) & 0x01) << 31) |
         ((((u32)offset >> 1) & 0x3ff) << 21) |
         ((((u32)offset >> 11) & 0x01) << 20) |
         ((((u32)offset >> 12) & 0xff) << 12) |
         (((u32)rd & 0x1f) << 7) |
         riscv_opcode_jal;
}

static bool riscv_jal_delta_fits(u32 delta)
{
  const s32 offset = (s32)delta;

  return (delta & 1u) == 0u &&
         offset >= -1048576 && offset <= 1048574;
}

static void riscv_patch_local_branch(u8 *source, const u8 *target);
static void riscv_emit_helper_call_no_flush(
  u8 **ptr, const riscv_jit_block_meta *meta);
static void riscv_emit_terminal_helper_call(u8 **ptr,
                                            riscv_jit_block_meta *meta);
static void riscv_emit_terminal_helper_call_no_flush(
  u8 **ptr, riscv_jit_block_meta *meta);
void riscv_emit_arm_conditional_block_close(u8 **ptr_ref,
                                            u8 *branch_source);
static void riscv_emit_store_alert_branch(u8 **ptr_ref,
                                          riscv_jit_block_meta *meta);

static u8 *riscv_emit_unconditional_branch_patch_site(u8 **ptr_ref,
                                                      bool flush_before_patch)
{
  if (flush_before_patch)
    riscv_emit_mapped_regs_flush_dirty(ptr_ref);

  u8 *translation_ptr = *ptr_ref;
  u8 *source = translation_ptr;

  riscv_emit_nop();
  riscv_emit_nop();

  *ptr_ref = translation_ptr;
  return source;
}

static u8 *riscv_emit_unconditional_branch_patch_site_short(
  u8 **ptr_ref, bool flush_before_patch)
{
  if (flush_before_patch)
    riscv_emit_mapped_regs_flush_dirty(ptr_ref);

  u8 *translation_ptr = *ptr_ref;
  u8 *source = translation_ptr;

  riscv_emit_nop();
  riscv_emit_nop();

  *ptr_ref = translation_ptr;
  return source;
}

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
  u8 *translation_ptr;
  u32 continuation_valid_mask;
  u32 continuation_dirty_mask;

  if (flush_before_patch)
  {
    riscv_emit_mapped_regs_flush_dirty(&ptr);
    /* Every patched edge enters code compiled from the target's canonical
     * all-mappings-live state.  This applies to internal joins as well as
     * separately translated block entries: a helper on only one predecessor
     * may have invalidated caller-saved mappings in the source path. */
    riscv_emit_mapped_regs_reload_mask(
      &ptr, RISCV_MAPPED_REGS_MASK & ~riscv_mapped_valid_mask);
  }

  continuation_valid_mask = riscv_mapped_valid_mask;
  continuation_dirty_mask = riscv_mapped_dirty_mask;

#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
  riscv_emit_control_counter_increment(
    &ptr, &riscv_control_direct_chain_attempts);
#endif
  translation_ptr = ptr;
  cycle_exit_branch = translation_ptr;
  riscv_emit_bge(riscv_reg_zero, RISCV_CYCLES_REG, 0);
  ptr = translation_ptr;

#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
  riscv_emit_control_counter_increment(&ptr,
                                       &riscv_control_direct_chain_hits);
#endif
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

  /* The slow scheduler tail does not fall through. Preserve the mapped-state
     model seen by an internal hot target emitted after this patch site. */
  riscv_mapped_valid_mask = continuation_valid_mask;
  riscv_mapped_dirty_mask = continuation_dirty_mask;

  *ptr_ref = ptr;
  return branch_source;
}

static u8 *riscv_emit_terminal_branch_patch_site_with_cycle_exit(
  u8 **ptr_ref,
  riscv_jit_block_meta *meta,
  bool short_patch_site,
  bool flush_before_patch)
{
  u8 *ptr = *ptr_ref;
  u8 *branch_source;
  u8 *cycle_exit_branch;
  u8 *translation_ptr;

  /* The caller has already published REG_PC, which also flushes dirty mapped
   * state. Keep the explicit flush for the helper's contract, but put the
   * external-entry reload on the hot chain only: the scheduler path returns
   * through the outer entry stub and must not pay for registers it will not
   * consume. */
  if (flush_before_patch)
    riscv_emit_mapped_regs_flush_dirty(&ptr);

#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
  riscv_emit_control_counter_increment(
    &ptr, &riscv_control_direct_chain_attempts);
#endif
  translation_ptr = ptr;
  cycle_exit_branch = translation_ptr;
  riscv_emit_bge(riscv_reg_zero, RISCV_CYCLES_REG, 0);
  ptr = translation_ptr;

#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
  riscv_emit_control_counter_increment(&ptr,
                                       &riscv_control_direct_chain_hits);
#endif
  if (flush_before_patch)
  {
    riscv_emit_mapped_regs_reload_mask(
      &ptr, RISCV_MAPPED_REGS_MASK & ~riscv_mapped_valid_mask);
  }
  branch_source = short_patch_site ?
    riscv_emit_unconditional_branch_patch_site_short(&ptr, false) :
    riscv_emit_unconditional_branch_patch_site(&ptr, false);

  /* A normal patched chain jumps away. Cycle exhaustion, or a debug build
   * that deliberately leaves an external site as NOPs, reaches this single
   * scheduler tail. Mark it terminal so block finalization does not append an
   * unreachable duplicate. PC_WRITTEN is set by the caller afterwards, which
   * intentionally selects the scheduler tail rather than lookup chaining. */
  riscv_patch_local_branch(cycle_exit_branch, ptr);
  riscv_emit_terminal_helper_call_no_flush(&ptr, meta);

  *ptr_ref = ptr;
  return branch_source;
}

void riscv_patch_unconditional_branch(u8 *source, const u8 *target)
{
  u32 offset;
  u32 upper;
  s32 lower;

  if (!source || !target)
    return;

  /* Split the modulo-2^32 PC-relative delta without signed overflow.  The
   * previous (s32 delta + 0x800) rounding expression was undefined near the
   * +/-2 GiB boundary, exactly where this full-range patch form is needed. */
  offset = (u32)(uintptr_t)target - (u32)(uintptr_t)source;
  lower = (s32)(((offset & 0xfffu) ^ 0x800u) - 0x800u);
  upper = (offset - (u32)lower) >> 12;

  ((u32 *)source)[0] =
    riscv_encode_u(riscv_opcode_auipc, riscv_reg_t6, upper);
  ((u32 *)source)[1] =
    riscv_encode_i(riscv_opcode_jalr, 0x0,
                   riscv_reg_zero, riscv_reg_t6, lower);
}

void riscv_patch_unconditional_branch_short(u8 *source, const u8 *target)
{
  u32 delta;

  if (!source || !target)
    return;

  delta = (u32)(uintptr_t)target - (u32)(uintptr_t)source;
  if (!riscv_jal_delta_fits(delta))
  {
    riscv_patch_unconditional_branch(source, target);
    return;
  }

  ((u32 *)source)[0] =
    riscv_encode_j_inst(riscv_reg_zero, (s32)delta);
  ((u32 *)source)[1] = riscv_encode_i(
    riscv_opcode_op_imm, 0x0, riscv_reg_zero, riscv_reg_zero, 0);
}

void riscv_set_runtime_debug_force_dispatch(bool force_dispatch)
{
  riscv_debug_force_dispatch = force_dispatch;
}

bool riscv_runtime_debug_force_dispatch(void)
{
  return riscv_debug_force_dispatch;
}

void riscv_set_runtime_debug_disable_thumb_native(u32 mask)
{
  riscv_debug_disable_thumb_native = mask;
}

bool riscv_runtime_debug_thumb_native_disabled(u32 mask)
{
  return (riscv_debug_disable_thumb_native & mask) != 0u;
}

void riscv_set_runtime_debug_disable_arm_native(bool disable)
{
  riscv_debug_disable_arm_native = disable ? ~0u : 0u;
}

void riscv_set_runtime_debug_disable_arm_native_mask(u32 mask)
{
  riscv_debug_disable_arm_native = mask;
}

void riscv_request_runtime_debug_stop(void)
{
  riscv_cycles_remaining = 0;
}

void riscv_set_runtime_debug_branch_probe_pc(u32 pc)
{
  riscv_debug_branch_probe_pc = pc;
  riscv_debug_branch_probe_state.valid = 0;
  riscv_debug_branch_probe_state.pc = 0;
  riscv_debug_branch_probe_state.r0_host = 0;
  riscv_debug_branch_probe_state.r1_host = 0;
  riscv_debug_branch_probe_state.nzcv_host = 0;
}

void riscv_set_runtime_debug_arm_probe_pc(u32 pc)
{
  volatile u32 *words =
    (volatile u32 *)(volatile void *)&riscv_debug_arm_probe_state;
  u32 i;

  riscv_debug_arm_probe_pc = pc;
  for (i = 0; i < sizeof(riscv_debug_arm_probe_state) / sizeof(u32); i++)
    words[i] = 0u;
}

void riscv_get_runtime_debug_arm_probe(riscv_runtime_debug_arm_probe *probe)
{
  const volatile u32 *source =
    (const volatile u32 *)(const volatile void *)&riscv_debug_arm_probe_state;
  u32 *dest = (u32 *)(void *)probe;
  u32 i;

  if (probe == NULL)
    return;
  for (i = 0; i < sizeof(*probe) / sizeof(u32); i++)
    dest[i] = source[i];
}

void riscv_get_runtime_debug_branch_probe(
  riscv_runtime_debug_branch_probe *probe)
{
  if (probe == NULL)
    return;

  probe->valid = riscv_debug_branch_probe_state.valid;
  probe->pc = riscv_debug_branch_probe_state.pc;
  probe->r0_host = riscv_debug_branch_probe_state.r0_host;
  probe->r1_host = riscv_debug_branch_probe_state.r1_host;
  probe->nzcv_host = riscv_debug_branch_probe_state.nzcv_host;
}

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

void riscv_patch_conditional_branch(u8 *source, const u8 *target)
{
  u32 instruction;
  s32 offset;

  if (!source || !target)
    return;

  instruction = ((u32 *)source)[0];
  if ((instruction & 0x7fu) == riscv_opcode_branch)
  {
    riscv_patch_local_branch(source, target);
    return;
  }

  offset = (s32)((u32)(uintptr_t)target - (u32)(uintptr_t)source);
  ((u32 *)source)[0] = riscv_encode_j_inst(riscv_reg_zero, offset);
}

static void riscv_patch_local_branch(u8 *source, const u8 *target)
{
  u32 instruction;
  u32 funct3;
  riscv_reg_number rs1;
  riscv_reg_number rs2;
  s32 offset;

  if (!source || !target)
    return;

  instruction = ((u32 *)source)[0];
  funct3 = (instruction >> 12) & 0x07u;
  rs1 = (riscv_reg_number)((instruction >> 15) & 0x1fu);
  rs2 = (riscv_reg_number)((instruction >> 20) & 0x1fu);
  offset = (s32)((u32)(uintptr_t)target - (u32)(uintptr_t)source);

  ((u32 *)source)[0] = riscv_encode_b_inst(funct3, rs1, rs2, offset);
}

static s32 riscv_decode_long_jump_offset(const u8 *source)
{
  const u32 auipc = ((const u32 *)source)[0];
  const u32 jalr = ((const u32 *)source)[1];
  const u32 upper = auipc & 0xfffff000u;
  const s32 lower = (s32)jalr >> 20;

  return (s32)(upper + (u32)lower);
}

static void riscv_patch_store_alert_branches(riscv_jit_block_meta *meta,
                                             u32 branch_chain,
                                             const u8 *target)
{
  while (branch_chain)
  {
    u8 *source = ((u8 *)meta) + branch_chain;
    s32 next = riscv_decode_long_jump_offset(source);

    riscv_patch_unconditional_branch(source, target);
    branch_chain = next ? branch_chain + (u32)next : 0u;
  }
}

static void riscv_emit_store_alert_branch(u8 **ptr_ref,
                                          riscv_jit_block_meta *meta)
{
  u8 *ptr = *ptr_ref;
  u32 source_offset;
  u32 previous_offset;
  u8 *translation_ptr;
  s32 link_delta;
  s32 lower;
  u32 upper;

  if (!meta)
    return;

  /* A conditional branch reaches only +/-4 KiB.  Invert it over a full-range
   * AUIPC/JALR pair so a large translated block can always reach its final
   * scheduler/helper tail.  Before finalization the pair encodes a linked-list
   * delta to the preceding site; finalization replaces it with the real exit. */
  translation_ptr = ptr;
  riscv_emit_beq(riscv_reg_a0, riscv_reg_zero, 12);
  ptr = translation_ptr;

  source_offset = (u32)(ptr - (u8 *)meta);
  previous_offset = riscv_block_meta_chain_offset(meta);
  link_delta = previous_offset ?
    (s32)(previous_offset - source_offset) : 0;
  lower = (s32)((((u32)link_delta & 0xfffu) ^ 0x800u) - 0x800u);
  upper = ((u32)link_delta - (u32)lower) >> 12;

  translation_ptr = ptr;
  riscv_emit_auipc(riscv_reg_t0, upper);
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_t0, lower);
  ptr = translation_ptr;
  riscv_block_meta_set_chain_offset(meta, source_offset);

  *ptr_ref = ptr;
}

static void riscv_emit_cpu_alert_branch(u8 **ptr_ref,
                                        riscv_jit_block_meta *meta)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_li(&ptr, riscv_reg_a0, (u32)(uintptr_t)&riscv_cpu_alert);
  translation_ptr = ptr;
  /* cpu_alert_type is one byte.  A word load is both misaligned for the
   * current object placement and aliases adjacent emitter globals, making a
   * zero alert look nonzero once (for example) terminal-helper size is set. */
  riscv_emit_lbu(riscv_reg_a0, riscv_reg_a0, 0);
  ptr = translation_ptr;

  riscv_emit_store_alert_branch(&ptr, meta);
  *ptr_ref = ptr;
}

static void riscv_emit_branch_with_source(u8 **ptr_ref,
                                          u8 **branch_source,
                                          u32 funct3,
                                          riscv_reg_number rs1,
                                          riscv_reg_number rs2,
                                          s32 offset)
{
  u8 *translation_ptr = *ptr_ref;

  if (branch_source)
    *branch_source = translation_ptr;
  riscv_emit_b(funct3, rs1, rs2, offset);

  *ptr_ref = translation_ptr;
}

static void riscv_emit_live_nzcv_bit_branch(u8 **ptr_ref,
                                            u8 **branch_source,
                                            u32 mask,
                                            bool set,
                                            s32 offset)
{
  u8 *translation_ptr;

  if (!(riscv_mapped_valid_mask & RISCV_MAPPED_NZCV_MASK))
    riscv_emit_mapped_regs_reload_mask(ptr_ref, RISCV_MAPPED_NZCV_MASK);

  translation_ptr = *ptr_ref;
  riscv_emit_andi(riscv_reg_t0,
                  riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], mask);
  riscv_emit_branch_with_source(&translation_ptr, branch_source,
                                set ? 0x1 : 0x0,
                                riscv_reg_t0, riscv_reg_zero, offset);

  *ptr_ref = translation_ptr;
}

static bool riscv_emit_arm_condition_branch(u8 **ptr_ref,
                                            u32 condition,
                                            s32 offset,
                                            u8 **branch_source)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  switch (condition)
  {
    case 0x0:
      riscv_emit_live_nzcv_bit_branch(&ptr, branch_source, 0x04u, true,
                                      offset);
      break;
    case 0x1:
      riscv_emit_live_nzcv_bit_branch(&ptr, branch_source, 0x04u, false,
                                      offset);
      break;
    case 0x2:
      riscv_emit_live_nzcv_bit_branch(&ptr, branch_source, 0x02u, true,
                                      offset);
      break;
    case 0x3:
      riscv_emit_live_nzcv_bit_branch(&ptr, branch_source, 0x02u, false,
                                      offset);
      break;
    case 0x4:
      riscv_emit_live_nzcv_bit_branch(&ptr, branch_source, 0x08u, true,
                                      offset);
      break;
    case 0x5:
      riscv_emit_live_nzcv_bit_branch(&ptr, branch_source, 0x08u, false,
                                      offset);
      break;
    case 0x6:
      riscv_emit_live_nzcv_bit_branch(&ptr, branch_source, 0x01u, true,
                                      offset);
      break;
    case 0x7:
      riscv_emit_live_nzcv_bit_branch(&ptr, branch_source, 0x01u, false,
                                      offset);
      break;
    case 0x8:
    case 0x9:
      if (!(riscv_mapped_valid_mask & RISCV_MAPPED_NZCV_MASK))
        riscv_emit_mapped_regs_reload_mask(&ptr, RISCV_MAPPED_NZCV_MASK);
      translation_ptr = ptr;
      riscv_emit_andi(riscv_reg_t0,
                      riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 0x06u);
      riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, -2);
      if (condition == 0x8)
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x0,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      else
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x1,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      ptr = translation_ptr;
      break;
    case 0xa:
    case 0xb:
      if (!(riscv_mapped_valid_mask & RISCV_MAPPED_NZCV_MASK))
        riscv_emit_mapped_regs_reload_mask(&ptr, RISCV_MAPPED_NZCV_MASK);
      translation_ptr = ptr;
      riscv_emit_srli(riscv_reg_t0,
                      riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 3);
      riscv_emit_xor(riscv_reg_t0, riscv_reg_t0,
                     riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT]);
      riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, 1);
      if (condition == 0xa)
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x0,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      else
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x1,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      ptr = translation_ptr;
      break;
    case 0xc:
    case 0xd:
      if (!(riscv_mapped_valid_mask & RISCV_MAPPED_NZCV_MASK))
        riscv_emit_mapped_regs_reload_mask(&ptr, RISCV_MAPPED_NZCV_MASK);
      translation_ptr = ptr;
      riscv_emit_srli(riscv_reg_t0,
                      riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 3);
      riscv_emit_xor(riscv_reg_t0, riscv_reg_t0,
                     riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT]);
      riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, 1);
      riscv_emit_andi(riscv_reg_t1,
                      riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 0x04u);
      riscv_emit_or(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
      if (condition == 0xc)
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x0,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      else
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x1,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      ptr = translation_ptr;
      break;
    default:
      return false;
  }

  *ptr_ref = ptr;
  return true;
}

bool riscv_emit_arm_conditional_block_header(u8 **translation_ptr_ref,
                                             riscv_jit_block_meta *meta,
                                             u32 condition,
                                             u32 cycles,
                                             u8 **branch_source)
{
  u8 *ptr = *translation_ptr_ref;
  u8 *body_branch;

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (!branch_source)
    return false;

  if (condition > 0x0du)
    return false;

  riscv_emit_mapped_regs_reload_mask(&ptr,
    RISCV_MAPPED_REGS_MASK & ~riscv_mapped_valid_mask);

  riscv_emit_adjust_cycles(&ptr, cycles);
  /* A B-type skip cannot span the largest frontend ARM block. Branch over a
     fixed AUIPC/JALR patch site when the condition passes; the false path
     then has full RV32 address range to reach the end of the block. */
  if (!riscv_emit_arm_condition_branch(&ptr, condition,
                                       4 + RISCV_BRANCH_PATCH_BYTES,
                                       &body_branch))
    return false;
  (void)body_branch;
  *branch_source = riscv_emit_unconditional_branch_patch_site(&ptr, false);

  riscv_arm_conditional_entry_valid_mask = riscv_mapped_valid_mask;
  riscv_arm_conditional_entry_dirty_mask = riscv_mapped_dirty_mask;
  riscv_arm_conditional_block_active = true;

  *translation_ptr_ref = ptr;
  return true;
}

void riscv_emit_arm_conditional_block_close_cycles(u8 **ptr_ref,
                                                   u8 *branch_source,
                                                   u32 cycles)
{
  /* This point is still inside the predicate's taken body.  Charging deferred
   * load latency here keeps the skipped path at fetch-only cost while avoiding
   * a load/store of the global cycle counter after every conditional LDR. */
  if (cycles)
    riscv_emit_adjust_cycles(ptr_ref, cycles);

  if (riscv_arm_conditional_block_active)
  {
    u32 restore_mask = riscv_arm_conditional_entry_valid_mask &
                       ~riscv_mapped_valid_mask;

    /* A helper in the conditional body may invalidate caller-saved mapped
     * guest registers.  The false path skips that helper and still has the
     * entry mappings live, so reconcile the true path before both paths
     * merge.  Otherwise the compiler-side valid mask claims registers such
     * as r0-r4 are live even though the C call left their a3-a7 mappings
     * clobbered. */
    if (restore_mask)
      riscv_emit_mapped_regs_reload_mask(ptr_ref, restore_mask);
    riscv_mapped_valid_mask |= riscv_arm_conditional_entry_valid_mask;
    riscv_mapped_dirty_mask |= riscv_arm_conditional_entry_dirty_mask;
  }
  riscv_arm_conditional_block_active = false;
  riscv_patch_unconditional_branch(branch_source, *ptr_ref);
}

void riscv_emit_arm_conditional_block_close(u8 **ptr_ref,
                                            u8 *branch_source)
{
  riscv_emit_arm_conditional_block_close_cycles(ptr_ref, branch_source, 0);
}

bool riscv_emit_cycle_update(u8 **translation_ptr_ref,
                             riscv_jit_block_meta *meta,
                             u32 pc,
                             u32 cycles)
{
  u8 *ptr;
  u8 *translation_ptr;
  u8 *continue_branch;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  riscv_emit_mapped_regs_flush_dirty(translation_ptr_ref);
  riscv_invalidate_mapped_regs();
  riscv_emit_adjust_cycles(translation_ptr_ref, cycles);
  ptr = *translation_ptr_ref;

  translation_ptr = ptr;
  continue_branch = translation_ptr;
  riscv_emit_b(0x4, riscv_reg_zero, RISCV_CYCLES_REG, 0);
  ptr = translation_ptr;
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_helper_call_no_flush(&ptr, meta);
  riscv_patch_local_branch(continue_branch, ptr);

  *translation_ptr_ref = ptr;
  return true;
}

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
#define RISCV_STORE_SLOW_ATTR __attribute__((used))

static void riscv_store_ram_regions(u32 address, u8 **data,
                                    const u8 **shadow, u32 *offset,
                                    u32 *mask)
{
  if (address & 0x01000000u)
  {
    *offset = address & 0x7fffu;
    *mask = 0x7fffu;
    *data = iwram + 0x8000u + *offset;
    *shadow = iwram + *offset;
  }
  else
  {
    *offset = address & 0x3ffffu;
    *mask = 0x3ffffu;
    *data = ewram + *offset;
    *shadow = ewram + 0x40000u + *offset;
  }
}

#else
#define RISCV_STORE_SLOW_ATTR
#endif

static u32 function_cc RISCV_STORE_SLOW_ATTR
riscv_store_u8(u32 address, u32 value)
{
  cpu_alert_type alert;

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
  if ((address >> 25) == 1u)
  {
    u8 *data;
    const u8 *shadow;
    u32 offset;
    u32 mask;

    riscv_store_ram_regions(address, &data, &shadow, &offset, &mask);
    *data = (u8)value;
    alert = *shadow ? CPU_ALERT_SMC : CPU_ALERT_NONE;
  }
  else
#endif
    alert = write_memory8(address, (u8)value);
  riscv_cpu_alert |= alert;
  /* Block stores issue several helper calls before they may leave the guest
   * instruction.  Return the accumulated alert state so the final transfer
   * can test a0 directly even when an earlier transfer raised the alert. */
  return riscv_cpu_alert;
}

static u32 function_cc RISCV_STORE_SLOW_ATTR
riscv_store_u16(u32 address, u32 value)
{
  cpu_alert_type alert;

  /* ARM7TDMI stores ignore the low address bit.  The interpreter applies
   * this alignment before entering write_memory16; keep the same contract
   * for both direct C calls and fast-store slow tails. */
  address &= ~1u;

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
  if ((address >> 25) == 1u)
  {
    u8 *data;
    const u8 *shadow;
    u32 offset;
    u32 mask;
    bool tagged;

    riscv_store_ram_regions(address, &data, &shadow, &offset, &mask);
    *((u16 *)data) = (u16)value;
    tagged = *((const u16 *)shadow) != 0;
    alert = tagged ? CPU_ALERT_SMC : CPU_ALERT_NONE;
  }
  else
#endif
    alert = write_memory16(address, (u16)value);
  riscv_cpu_alert |= alert;
  return riscv_cpu_alert;
}

static u32 function_cc RISCV_STORE_SLOW_ATTR
riscv_store_u32(u32 address, u32 value)
{
  cpu_alert_type alert;

  /* As in cpu.cc's fast_write_memory(32), a guest STR writes the aligned
   * word containing the effective address.  This must happen before RAM
   * pointer and SMC-shadow selection, not only in the non-RAM helper. */
  address &= ~3u;

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
  if ((address >> 25) == 1u)
  {
    u8 *data;
    const u8 *shadow;
    u32 offset;
    u32 mask;
    bool tagged;

    riscv_store_ram_regions(address, &data, &shadow, &offset, &mask);
    *((u32 *)data) = value;
    tagged = *((const u32 *)shadow) != 0;
    alert = tagged ? CPU_ALERT_SMC : CPU_ALERT_NONE;
  }
  else
#endif
    alert = write_memory32(address, value);
  riscv_cpu_alert |= alert;
  return riscv_cpu_alert;
}

#undef RISCV_STORE_SLOW_ATTR

#if defined(RISCV_RUNTIME_DISABLE_READ_HELPER_OPT) || \
    defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
static u32 function_cc __attribute__((used, noinline))
riscv_read_u8_pc(u32 address, u32 pc)
{
  reg[REG_PC] = pc;
  return read_memory8(address);
}

static u32 function_cc __attribute__((used, noinline))
riscv_read_u16_pc(u32 address, u32 pc)
{
  reg[REG_PC] = pc;
  return read_memory16(address);
}

static u32 function_cc __attribute__((used, noinline))
riscv_read_u32_pc(u32 address, u32 pc)
{
  reg[REG_PC] = pc;
  return read_memory32(address);
}

static u32 function_cc __attribute__((used, noinline))
riscv_read_s8_pc(u32 address, u32 pc)
{
  reg[REG_PC] = pc;
  return read_memory8s(address);
}

static u32 function_cc __attribute__((used, noinline))
riscv_read_s16_pc(u32 address, u32 pc)
{
  reg[REG_PC] = pc;
  return read_memory16s(address);
}
#endif

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
u32 riscv_fast_read_u8(u32 address, u32 pc);
u32 riscv_fast_read_s8(u32 address, u32 pc);
u32 riscv_fast_read_u16(u32 address, u32 pc);
u32 riscv_fast_read_s16(u32 address, u32 pc);
u32 riscv_fast_read_u32(u32 address, u32 pc);

/* Shared ordinary-RAM read stubs.  Fast EWRAM/IWRAM paths touch only standard
 * caller-saved scratch registers.  Non-RAM paths materialize the guest PC and
 * tail-call the complete C memory subsystem; the emitted call site already
 * spills and invalidates mapped caller-saved guest registers. */
__asm__(
  ".text\n"
  ".align 2\n"
  ".macro riscv_fast_ram_ptr slow\n"
  /* Bit 27 is clear in both work-RAM regions.  Most dynamic non-RAM reads in
   * the real frontend target ROM, so reject that half of the GBA map in two
   * instructions before falling through to the exact region test. */
  "  slli t1, a0, 4\n"
  "  bltz t1, \\slow\n"
  "  srli t1, a0, 25\n"
  "  addi t1, t1, -1\n"
  "  bnez t1, \\slow\n"
  "  slli t0, a0, 7\n"
  "  bltz t0, .Lfast_iwram_\\@\n"
  "  slli t0, a0, 14\n"
  "  srli t0, t0, 14\n"
  "  lla t1, ewram\n"
  "  j .Lfast_ram_done_\\@\n"
  ".Lfast_iwram_\\@:\n"
  "  slli t0, a0, 17\n"
  "  srli t0, t0, 17\n"
  "  lla t1, iwram+32768\n"
  ".Lfast_ram_done_\\@:\n"
  "  add t0, t1, t0\n"
  ".endm\n"

  ".globl riscv_fast_read_u8\n"
  ".type riscv_fast_read_u8, @function\n"
  "riscv_fast_read_u8:\n"
  "  riscv_fast_ram_ptr riscv_fast_read_u8_slow\n"
  "  lbu a0, 0(t0)\n"
  "  ret\n"
  ".size riscv_fast_read_u8, .-riscv_fast_read_u8\n"

  ".globl riscv_fast_read_s8\n"
  ".type riscv_fast_read_s8, @function\n"
  "riscv_fast_read_s8:\n"
  "  riscv_fast_ram_ptr riscv_fast_read_s8_slow\n"
  "  lb a0, 0(t0)\n"
  "  ret\n"
  ".size riscv_fast_read_s8, .-riscv_fast_read_s8\n"

  ".globl riscv_fast_read_u16\n"
  ".type riscv_fast_read_u16, @function\n"
  "riscv_fast_read_u16:\n"
  "  andi t2, a0, 1\n"
  "  riscv_fast_ram_ptr riscv_fast_read_u16_slow\n"
  "  sub t0, t0, t2\n"
  "  lhu a0, 0(t0)\n"
  "  beqz t2, 1f\n"
  "  srli t0, a0, 8\n"
  "  slli a0, a0, 24\n"
  "  or a0, a0, t0\n"
  "1:\n"
  "  ret\n"
  ".size riscv_fast_read_u16, .-riscv_fast_read_u16\n"

  ".globl riscv_fast_read_s16\n"
  ".type riscv_fast_read_s16, @function\n"
  "riscv_fast_read_s16:\n"
  "  andi t2, a0, 1\n"
  "  riscv_fast_ram_ptr riscv_fast_read_s16_slow\n"
  "  beqz t2, 1f\n"
  "  lb a0, 0(t0)\n"
  "  ret\n"
  "1:\n"
  "  lh a0, 0(t0)\n"
  "  ret\n"
  ".size riscv_fast_read_s16, .-riscv_fast_read_s16\n"

  ".globl riscv_fast_read_u32\n"
  ".type riscv_fast_read_u32, @function\n"
  "riscv_fast_read_u32:\n"
  "  andi t2, a0, 3\n"
  "  riscv_fast_ram_ptr riscv_fast_read_u32_slow\n"
  "  sub t0, t0, t2\n"
  "  lw a0, 0(t0)\n"
  "  beqz t2, 1f\n"
  "  slli t2, t2, 3\n"
  "  srl t0, a0, t2\n"
  "  li t1, 32\n"
  "  sub t1, t1, t2\n"
  "  sll a0, a0, t1\n"
  "  or a0, a0, t0\n"
  "1:\n"
  "  ret\n"
  ".size riscv_fast_read_u32, .-riscv_fast_read_u32\n"

  "riscv_fast_read_u8_slow:\n"
  "  sw a1, 60(s0)\n"
  "  tail read_memory8\n"
  "riscv_fast_read_s8_slow:\n"
  "  sw a1, 60(s0)\n"
  "  tail read_memory8s\n"
  "riscv_fast_read_u16_slow:\n"
  "  sw a1, 60(s0)\n"
  "  tail read_memory16\n"
  "riscv_fast_read_s16_slow:\n"
  "  sw a1, 60(s0)\n"
  "  tail read_memory16s\n"
  "riscv_fast_read_u32_slow:\n"
  "  sw a1, 60(s0)\n"
  "  tail read_memory32\n");
#endif

#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
u32 riscv_fast_store_u8(u32 address, u32 value, u32 pc);
u32 riscv_fast_store_u16(u32 address, u32 value, u32 pc);
u32 riscv_fast_store_u32(u32 address, u32 value, u32 pc);

/* Shared ordinary-RAM store stubs.  The leaf path preserves every mapped
 * guest host register.  Call sites flush dirty mappings before entry, so slow
 * C tails and SMC exits only need to publish the guest PC.  Slow tails reload
 * caller-saved guest mappings before returning. */
__asm__(
  ".text\n"
  ".align 2\n"
  ".macro riscv_fast_store_ptr slow\n"
  "  srli t1, a0, 25\n"
  "  addi t1, t1, -1\n"
  "  bnez t1, \\slow\n"
  "  slli t1, a0, 7\n"
  "  bltz t1, .Lfast_store_iwram_\\@\n"
  "  slli t2, a0, 14\n"
  "  srli t2, t2, 14\n"
  "  lla t0, ewram\n"
  "  add t0, t0, t2\n"
  "  lla t1, ewram+262144\n"
  "  add t1, t1, t2\n"
  "  j .Lfast_store_ptr_done_\\@\n"
  ".Lfast_store_iwram_\\@:\n"
  "  slli t2, a0, 17\n"
  "  srli t2, t2, 17\n"
  "  lla t0, iwram+32768\n"
  "  add t0, t0, t2\n"
  "  lla t1, iwram\n"
  "  add t1, t1, t2\n"
  ".Lfast_store_ptr_done_\\@:\n"
  ".endm\n"

  ".macro riscv_fast_store_slow target\n"
  "  sw ra, 104(sp)\n"
  "  sw a2, 60(s0)\n"
  "  call \\target\n"
  "  lla t0, riscv_cpu_alert\n"
  "  lbu t1, 0(t0)\n"
  "  or t1, t1, a0\n"
  "  sb t1, 0(t0)\n"
  "  lw a3, 0(s0)\n"
  "  lw a4, 4(s0)\n"
  "  lw a5, 8(s0)\n"
  "  lw a6, 12(s0)\n"
  "  lw a7, 16(s0)\n"
  "  lw ra, 104(sp)\n"
  "  ret\n"
  ".endm\n"

  ".globl riscv_fast_store_u8\n"
  ".type riscv_fast_store_u8, @function\n"
  "riscv_fast_store_u8:\n"
  "  riscv_fast_store_ptr riscv_fast_store_u8_slow\n"
  "  lbu t2, 0(t1)\n"
  "  sb a1, 0(t0)\n"
  "  bnez t2, .Lriscv_fast_store_smc\n"
  "  li a0, 0\n"
  "  ret\n"
  ".size riscv_fast_store_u8, .-riscv_fast_store_u8\n"

  ".globl riscv_fast_store_u16\n"
  ".type riscv_fast_store_u16, @function\n"
  "riscv_fast_store_u16:\n"
  "  andi t0, a0, 1\n"
  "  bnez t0, riscv_fast_store_u16_unaligned\n"
  "  riscv_fast_store_ptr riscv_fast_store_u16_slow\n"
  "  lhu t2, 0(t1)\n"
  "  sh a1, 0(t0)\n"
  "  bnez t2, .Lriscv_fast_store_smc\n"
  "  li a0, 0\n"
  "  ret\n"
  ".size riscv_fast_store_u16, .-riscv_fast_store_u16\n"

  ".globl riscv_fast_store_u32\n"
  ".type riscv_fast_store_u32, @function\n"
  "riscv_fast_store_u32:\n"
  "  andi t0, a0, 3\n"
  "  bnez t0, riscv_fast_store_u32_unaligned\n"
  "  riscv_fast_store_ptr riscv_fast_store_u32_slow\n"
  "  lw t2, 0(t1)\n"
  "  sw a1, 0(t0)\n"
  "  bnez t2, .Lriscv_fast_store_smc\n"
  "  li a0, 0\n"
  "  ret\n"
  ".size riscv_fast_store_u32, .-riscv_fast_store_u32\n"

  ".Lriscv_fast_store_smc:\n"
  "  sw a2, 60(s0)\n"
  "  lla t0, riscv_cpu_alert\n"
  "  lbu t1, 0(t0)\n"
  "  ori t1, t1, 2\n"
  "  sb t1, 0(t0)\n"
  "  li a0, 2\n"
  "  ret\n"

  "riscv_fast_store_u8_slow:\n"
  "  riscv_fast_store_slow write_memory8\n"
  "riscv_fast_store_u16_slow:\n"
  "  riscv_fast_store_slow write_memory16\n"
  "riscv_fast_store_u32_slow:\n"
  "  riscv_fast_store_slow write_memory32\n"
  "riscv_fast_store_u16_unaligned:\n"
  "  riscv_fast_store_slow riscv_store_u16\n"
  "riscv_fast_store_u32_unaligned:\n"
  "  riscv_fast_store_slow riscv_store_u32\n");
#endif

static u32 function_cc riscv_store_u8_pc(u32 address, u32 value, u32 pc)
{
  reg[REG_PC] = pc;
  return riscv_store_u8(address, value);
}

static u32 function_cc riscv_store_u16_pc(u32 address, u32 value, u32 pc)
{
  reg[REG_PC] = pc;
  return riscv_store_u16(address, value);
}

static u32 function_cc riscv_store_u32_pc(u32 address, u32 value, u32 pc)
{
  reg[REG_PC] = pc;
  return riscv_store_u32(address, value);
}

static void function_cc riscv_execute_swi_arm(u32 pc);
static void function_cc riscv_execute_swi_thumb(u32 pc);
static void function_cc riscv_execute_spsr_restore(void);
static void function_cc riscv_store_spsr(u32 source, u32 psr_pfield);
static void function_cc riscv_store_cpsr(u32 source, u32 psr_pfield);
static void function_cc riscv_hle_div(u32 divarm);
static u32 function_cc riscv_swap_u8(u32 address, u32 value, u32 pc);
static u32 function_cc riscv_swap_u32(u32 address, u32 value, u32 pc);
static void function_cc riscv_arm_block_memory(u32 opcode, u32 pc);

static void riscv_init_helper_table(void)
{
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  if (riscv_runtime_perf_disable_fast_ram_reads)
  {
    riscv_helper_table[RISCV_HELPER_READ32] = (uintptr_t)riscv_read_u32_pc;
    riscv_helper_table[RISCV_HELPER_READ8] = (uintptr_t)riscv_read_u8_pc;
    riscv_helper_table[RISCV_HELPER_READ16] = (uintptr_t)riscv_read_u16_pc;
    riscv_helper_table[RISCV_HELPER_READ8S] = (uintptr_t)riscv_read_s8_pc;
    riscv_helper_table[RISCV_HELPER_READ16S] = (uintptr_t)riscv_read_s16_pc;
  }
  else
#endif
  {
    riscv_helper_table[RISCV_HELPER_READ32] = (uintptr_t)riscv_fast_read_u32;
    riscv_helper_table[RISCV_HELPER_READ8] = (uintptr_t)riscv_fast_read_u8;
    riscv_helper_table[RISCV_HELPER_READ16] = (uintptr_t)riscv_fast_read_u16;
    riscv_helper_table[RISCV_HELPER_READ8S] = (uintptr_t)riscv_fast_read_s8;
    riscv_helper_table[RISCV_HELPER_READ16S] = (uintptr_t)riscv_fast_read_s16;
  }
#elif defined(RISCV_RUNTIME_DISABLE_READ_HELPER_OPT)
  riscv_helper_table[RISCV_HELPER_READ32] = (uintptr_t)riscv_read_u32_pc;
#else
  riscv_helper_table[RISCV_HELPER_READ32] = (uintptr_t)read_memory32;
#endif
#if defined(RISCV_RUNTIME_HAS_FAST_RAM_STORES)
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  if (riscv_runtime_perf_disable_fast_ram_stores)
  {
    riscv_helper_table[RISCV_HELPER_STORE32] =
      (uintptr_t)riscv_store_u32_pc;
    riscv_helper_table[RISCV_HELPER_STORE8] =
      (uintptr_t)riscv_store_u8_pc;
    riscv_helper_table[RISCV_HELPER_STORE16] =
      (uintptr_t)riscv_store_u16_pc;
  }
  else
#endif
  {
    riscv_helper_table[RISCV_HELPER_STORE32] =
      (uintptr_t)riscv_fast_store_u32;
    riscv_helper_table[RISCV_HELPER_STORE8] =
      (uintptr_t)riscv_fast_store_u8;
    riscv_helper_table[RISCV_HELPER_STORE16] =
      (uintptr_t)riscv_fast_store_u16;
  }
#else
  riscv_helper_table[RISCV_HELPER_STORE32] = (uintptr_t)riscv_store_u32_pc;
  riscv_helper_table[RISCV_HELPER_STORE8] = (uintptr_t)riscv_store_u8_pc;
  riscv_helper_table[RISCV_HELPER_STORE16] = (uintptr_t)riscv_store_u16_pc;
#endif
#if !defined(RISCV_RUNTIME_HAS_FAST_RAM_READS) && \
    defined(RISCV_RUNTIME_DISABLE_READ_HELPER_OPT)
  riscv_helper_table[RISCV_HELPER_READ8] = (uintptr_t)riscv_read_u8_pc;
#elif !defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
  riscv_helper_table[RISCV_HELPER_READ8] = (uintptr_t)read_memory8;
#endif
#if !defined(RISCV_RUNTIME_HAS_FAST_RAM_READS) && \
    defined(RISCV_RUNTIME_DISABLE_READ_HELPER_OPT)
  riscv_helper_table[RISCV_HELPER_READ16] = (uintptr_t)riscv_read_u16_pc;
#elif !defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
  riscv_helper_table[RISCV_HELPER_READ16] = (uintptr_t)read_memory16;
#endif
  riscv_helper_table[RISCV_HELPER_BLOCK_STORE32] = (uintptr_t)riscv_store_u32;
  riscv_helper_table[RISCV_HELPER_BLOCK_READ32] = (uintptr_t)read_memory32;
#if !defined(RISCV_RUNTIME_HAS_FAST_RAM_READS) && \
    defined(RISCV_RUNTIME_DISABLE_READ_HELPER_OPT)
  riscv_helper_table[RISCV_HELPER_READ8S] = (uintptr_t)riscv_read_s8_pc;
  riscv_helper_table[RISCV_HELPER_READ16S] = (uintptr_t)riscv_read_s16_pc;
#elif !defined(RISCV_RUNTIME_HAS_FAST_RAM_READS)
  riscv_helper_table[RISCV_HELPER_READ8S] = (uintptr_t)read_memory8s;
  riscv_helper_table[RISCV_HELPER_READ16S] = (uintptr_t)read_memory16s;
#endif
  riscv_helper_table[RISCV_HELPER_EXECUTE_SPSR_RESTORE] =
    (uintptr_t)riscv_execute_spsr_restore;
  riscv_helper_table[RISCV_HELPER_STORE_SPSR] =
    (uintptr_t)riscv_store_spsr;
  riscv_helper_table[RISCV_HELPER_STORE_CPSR] =
    (uintptr_t)riscv_store_cpsr;
  riscv_helper_table[RISCV_HELPER_EXECUTE_SWI_ARM] =
    (uintptr_t)riscv_execute_swi_arm;
  riscv_helper_table[RISCV_HELPER_EXECUTE_SWI_THUMB] =
    (uintptr_t)riscv_execute_swi_thumb;
  riscv_helper_table[RISCV_HELPER_HLE_DIV] = (uintptr_t)riscv_hle_div;
  riscv_helper_table[RISCV_HELPER_SWAP_U8] = (uintptr_t)riscv_swap_u8;
  riscv_helper_table[RISCV_HELPER_SWAP_U32] = (uintptr_t)riscv_swap_u32;
  riscv_helper_table[RISCV_HELPER_ARM_BLOCK_MEMORY] =
    (uintptr_t)riscv_arm_block_memory;
  riscv_helper_table[RISCV_HELPER_CYCLES_REMAINING] =
    (uintptr_t)&riscv_cycles_remaining;
}

static void riscv_init_helper_state(void)
{
  u32 helper_index;

  /* cpu.cc deliberately preserves REG_USERDEF across guest resets.  Keep
   * the immutable RV32 helper vector there so s0 can address it directly. */
  riscv_helper_table[RISCV_HELPER_THUMB_EXECUTE] =
    (uintptr_t)riscv_thumb_execute;
  for (helper_index = 0; helper_index < RISCV_HELPER_COUNT; helper_index++)
    reg[REG_USERDEF + helper_index] =
      (u32)riscv_helper_table[helper_index];
}

static u32 function_cc riscv_swap_u8(u32 address, u32 value, u32 pc)
{
  u32 old_value;

  reg[REG_PC] = pc;
  old_value = read_memory8(address);
  reg[REG_PC] = pc + 4u;
  riscv_store_u8(address, value);
  return old_value;
}

static u32 function_cc riscv_swap_u32(u32 address, u32 value, u32 pc)
{
  u32 old_value;

  reg[REG_PC] = pc;
  old_value = read_memory32(address);
  reg[REG_PC] = pc + 4u;
  riscv_store_u32(address, value);
  return old_value;
}

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

static void riscv_run_interpreter_remainder(void)
{
  if (riscv_cycles_remaining > 0)
  {
    /* ROM-page sticky bits protect interpreter fetches only.  Native entry
     * leaves them untouched, but every fallback starts a fresh interpreter
     * slice and must retain the interpreter's old clear-before-run contract. */
    if (riscv_entry_setup_optimized())
      clear_gamepak_stickybits();
    execute_arm((u32)riscv_cycles_remaining);
    riscv_cycles_remaining = 0;
  }
}

static void riscv_current_lookup_state(u32 *pc, u32 *thumb)
{
  *thumb = (reg[REG_CPSR] & 0x20u) != 0;
  *pc = *thumb ? (reg[REG_PC] & ~1u) : (reg[REG_PC] & ~0x03u);
}

static u32 riscv_lookup_result_from_entry(const u8 *entry)
{
  if (entry == RISCV_INVALID_BLOCK_ENTRY)
    return RISCV_RUNTIME_LOOKUP_INVALID;
  if (!entry)
    return RISCV_RUNTIME_LOOKUP_MISS;
  return 0;
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

typedef enum riscv_control_lookup_kind
{
  RISCV_CONTROL_LOOKUP_FALLTHROUGH = 0,
  RISCV_CONTROL_LOOKUP_INDIRECT
} riscv_control_lookup_kind;

static u8 *riscv_lookup_or_fallback(riscv_control_lookup_kind kind,
                                    s32 cycles)
{
  u8 *entry;
  u32 pc;
  u32 thumb;

  if (cycles <= 0)
    return NULL;

  entry = riscv_lookup_current_block(&pc, &thumb);
  if (!entry || entry == RISCV_INVALID_BLOCK_ENTRY)
  {
    if (kind == RISCV_CONTROL_LOOKUP_INDIRECT)
      RISCV_CONTROL_COUNT(riscv_control_indirect_lookup_misses);
    else
      RISCV_CONTROL_COUNT(riscv_control_fallthrough_lookup_misses);
    riscv_interpreter_fallbacks++;
    riscv_relookup_fallbacks++;
    if (pc < 0x00004000u)
      riscv_bios_interpreter_fallbacks++;
    /* The fast lookup stubs deliberately keep the scheduler budget resident
     * in s10. Publish it only when lookup really leaves native execution. */
    riscv_cycles_remaining = cycles;
    riscv_note_runtime_fallback(RISCV_RUNTIME_FALLBACK_RELOOKUP,
                                pc, thumb,
                                riscv_lookup_result_from_entry(entry),
                                (u32)cycles);
    riscv_run_interpreter_remainder();
    return NULL;
  }

  if (kind == RISCV_CONTROL_LOOKUP_INDIRECT)
    RISCV_CONTROL_COUNT(riscv_control_indirect_lookup_hits);
  else
    RISCV_CONTROL_COUNT(riscv_control_fallthrough_lookup_hits);

#if !defined(RISCV_RUNTIME_DISABLE_INDIRECT_LOOKUP_CACHE) || \
    defined(RISCV_RUNTIME_INDIRECT_LOOKUP_PROFILE_SWITCH)
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
#endif

#if defined(GPSP_ESP32S31_PSRAM_FAULT_TRACE) && \
    GPSP_ESP32S31_PSRAM_FAULT_TRACE
  esp32s31_psram_fault_trace_note_jit_lookup(
      pc | (thumb ? 1u : 0u), entry);
#endif

  return entry;
}

void riscv_invalidate_indirect_lookup_cache(void)
{
#if !defined(RISCV_RUNTIME_DISABLE_INDIRECT_LOOKUP_CACHE) || \
    defined(RISCV_RUNTIME_INDIRECT_LOOKUP_PROFILE_SWITCH)
  u32 generation = riscv_indirect_lookup_cache.generation + 1u;

  /* A generation avoids an O(cache-size) clear in cold translation windows.
   * The wrap path is practically unreachable but preserves correctness. */
  if (generation == 0u)
  {
    u32 i;

    for (i = 0; i < RISCV_INDIRECT_LOOKUP_CACHE_SIZE; i++)
      riscv_indirect_lookup_cache.entries[i].entry = 0u;
    generation = 1u;
  }
  riscv_indirect_lookup_cache.generation = generation;
#endif
}

#if defined(GPSP_ESP32S31_PSRAM_FAULT_TRACE) && \
    GPSP_ESP32S31_PSRAM_FAULT_TRACE
void IRAM_ATTR esp32s31_psram_fault_trace_get_indirect_snapshot(
    uint32_t key, esp32s31_psram_fault_indirect_snapshot_t *snapshot)
{
  const u32 index = (key >> 1) & RISCV_INDIRECT_LOOKUP_CACHE_MASK;
  volatile const riscv_indirect_lookup_cache_entry *entry =
      &riscv_indirect_lookup_cache.entries[index];

  if (snapshot == NULL)
    return;
  snapshot->requested_key = key;
  snapshot->global_generation = riscv_indirect_lookup_cache.generation;
  snapshot->slot_key = entry->key;
  snapshot->slot_entry = entry->entry;
  snapshot->slot_generation = entry->generation;
}
#endif

#if defined(RISCV_RUNTIME_STANDALONE_TEST) && \
    defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
void riscv_set_cpu_alert_for_test(cpu_alert_type alert)
{
  riscv_cpu_alert = alert;
}
#endif

static void function_cc riscv_execute_swi_arm(u32 pc)
{
  reg[REG_BUS_VALUE] = 0xe3a02004u;
  REG_MODE(MODE_SUPERVISOR)[6] = pc;
  REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];
  reg[REG_PC] = 0x00000008u;
  reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3fu) | MODE_SUPERVISOR | 0x80u;
  set_cpu_mode(MODE_SUPERVISOR);
}

static void function_cc riscv_execute_swi_thumb(u32 pc)
{
  reg[REG_BUS_VALUE] = 0xe3a02004u;
  REG_MODE(MODE_SUPERVISOR)[6] = pc + 2u;
  REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];
  reg[REG_PC] = 0x00000008u;
  reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3fu) | MODE_SUPERVISOR | 0x80u;
  set_cpu_mode(MODE_SUPERVISOR);
}

static u32 riscv_word_bit_count(u32 word);

enum
{
  RISCV_CPSR_N = 0x80000000u,
  RISCV_CPSR_Z = 0x40000000u,
  RISCV_CPSR_C = 0x20000000u,
  RISCV_CPSR_V = 0x10000000u,
  RISCV_CPSR_T = 0x00000020u,
  RISCV_CPSR_NZCV = 0xf0000000u
};

static u32 riscv_thumb_ror(u32 value, u32 shift)
{
  shift &= 31u;
  if (!shift)
    return value;
  return (value >> shift) | (value << (32u - shift));
}

static u32 riscv_thumb_c_flag(void)
{
  return (reg[REG_CPSR] >> 29) & 1u;
}

static u32 riscv_thumb_flag_bit(u32 bit)
{
  return (reg[REG_CPSR] & bit) ? 1u : 0u;
}

static void riscv_thumb_store_nzcv(u32 n, u32 z, u32 c, u32 v)
{
  reg[REG_CPSR] = (reg[REG_CPSR] & ~RISCV_CPSR_NZCV) |
                  (n ? RISCV_CPSR_N : 0u) |
                  (z ? RISCV_CPSR_Z : 0u) |
                  (c ? RISCV_CPSR_C : 0u) |
                  (v ? RISCV_CPSR_V : 0u);
}

static void riscv_thumb_store_nz_preserve_cv(u32 value)
{
  u32 cpsr = reg[REG_CPSR] & ~(RISCV_CPSR_N | RISCV_CPSR_Z);

  if (value & 0x80000000u)
    cpsr |= RISCV_CPSR_N;
  if (value == 0)
    cpsr |= RISCV_CPSR_Z;

  reg[REG_CPSR] = cpsr;
}

static void riscv_thumb_store_nzc_preserve_v(u32 value, u32 carry)
{
  u32 v = riscv_thumb_flag_bit(RISCV_CPSR_V);
  riscv_thumb_store_nzcv((value >> 31) & 1u, value == 0, carry, v);
}

static void riscv_thumb_store_add_flags(u32 value, u32 src_a, u32 src_b,
                                        u32 carry)
{
  u32 overflow = (~(src_a ^ src_b) & (src_a ^ value)) >> 31;
  riscv_thumb_store_nzcv((value >> 31) & 1u, value == 0, carry, overflow);
}

static void riscv_thumb_store_sub_flags(u32 value, u32 src_a, u32 src_b,
                                        u32 carry_in)
{
  u32 carry = carry_in ? (src_b <= src_a) : (src_b < src_a);
  u32 overflow = ((src_a ^ src_b) & (~src_b ^ value)) >> 31;
  riscv_thumb_store_nzcv((value >> 31) & 1u, value == 0, carry, overflow);
}

static u32 riscv_thumb_reg_value(u32 reg_index, u32 pc_value)
{
  return (reg_index == REG_PC) ? pc_value : reg[reg_index];
}

static void riscv_thumb_add(u32 rd, u32 src_a, u32 src_b, u32 carry_in)
{
  u32 value = src_a + src_b;
  u32 carry = value < src_b;

  value += carry_in;
  carry |= value < carry_in;
  riscv_thumb_store_add_flags(value, src_a, src_b, carry);
  reg[rd] = value;
}

static void riscv_thumb_sub(u32 rd, u32 src_a, u32 src_b, u32 carry_in)
{
  u32 value = src_a + ~src_b + carry_in;

  riscv_thumb_store_sub_flags(value, src_a, src_b, carry_in);
  reg[rd] = value;
}

static void riscv_thumb_test_add(u32 src_a, u32 src_b)
{
  u32 value = src_a + src_b;
  u32 carry = value < src_b;

  riscv_thumb_store_add_flags(value, src_a, src_b, carry);
}

static void riscv_thumb_test_sub(u32 src_a, u32 src_b)
{
  u32 value = src_a - src_b;

  riscv_thumb_store_sub_flags(value, src_a, src_b, 1u);
}

static void riscv_thumb_shift_lsl_imm(u32 rd, u32 rs, u32 imm)
{
  u32 value = reg[rs];
  u32 carry = riscv_thumb_c_flag();

  if (imm)
  {
    carry = (value >> (32u - imm)) & 1u;
    value <<= imm;
  }

  riscv_thumb_store_nzc_preserve_v(value, carry);
  reg[rd] = value;
}

static void riscv_thumb_shift_lsr_imm(u32 rd, u32 rs, u32 imm)
{
  u32 value;
  u32 carry;

  if (!imm)
  {
    value = 0;
    carry = reg[rs] >> 31;
  }
  else
  {
    value = reg[rs];
    carry = (value >> (imm - 1u)) & 1u;
    value >>= imm;
  }

  riscv_thumb_store_nzc_preserve_v(value, carry);
  reg[rd] = value;
}

static void riscv_thumb_shift_asr_imm(u32 rd, u32 rs, u32 imm)
{
  u32 value;
  u32 carry;

  if (!imm)
  {
    value = (u32)((s32)reg[rs] >> 31);
    carry = value & 1u;
  }
  else
  {
    value = reg[rs];
    carry = (value >> (imm - 1u)) & 1u;
    value = (u32)((s32)value >> imm);
  }

  riscv_thumb_store_nzc_preserve_v(value, carry);
  reg[rd] = value;
}

static void riscv_thumb_shift_lsl_reg(u32 rd, u32 rs)
{
  u32 shift = reg[rs];
  u32 value = reg[rd];
  u32 carry = riscv_thumb_c_flag();

  if (shift)
  {
    if (shift > 31u)
    {
      carry = (shift == 32u) ? (value & 1u) : 0u;
      value = 0;
    }
    else
    {
      carry = (value >> (32u - shift)) & 1u;
      value <<= shift;
    }
  }

  riscv_thumb_store_nzc_preserve_v(value, carry);
  reg[rd] = value;
}

static void riscv_thumb_shift_lsr_reg(u32 rd, u32 rs)
{
  u32 shift = reg[rs];
  u32 value = reg[rd];
  u32 carry = riscv_thumb_c_flag();

  if (shift)
  {
    if (shift > 31u)
    {
      carry = (shift == 32u) ? (value >> 31) : 0u;
      value = 0;
    }
    else
    {
      carry = (value >> (shift - 1u)) & 1u;
      value >>= shift;
    }
  }

  riscv_thumb_store_nzc_preserve_v(value, carry);
  reg[rd] = value;
}

static void riscv_thumb_shift_asr_reg(u32 rd, u32 rs)
{
  u32 shift = reg[rs];
  u32 value = reg[rd];
  u32 carry = riscv_thumb_c_flag();

  if (shift)
  {
    if (shift > 31u)
    {
      value = (u32)((s32)value >> 31);
      carry = value & 1u;
    }
    else
    {
      carry = (value >> (shift - 1u)) & 1u;
      value = (u32)((s32)value >> shift);
    }
  }

  riscv_thumb_store_nzc_preserve_v(value, carry);
  reg[rd] = value;
}

static void riscv_thumb_shift_ror_reg(u32 rd, u32 rs)
{
  u32 shift = reg[rs];
  u32 value = reg[rd];
  u32 carry = riscv_thumb_c_flag();

  if (shift)
  {
    carry = (value >> ((shift - 1u) & 31u)) & 1u;
    value = riscv_thumb_ror(value, shift);
  }

  riscv_thumb_store_nzc_preserve_v(value, carry);
  reg[rd] = value;
}

static void riscv_thumb_block_memory(u32 opcode, u32 pc)
{
  u32 hi = opcode >> 8;
  bool load = (hi == 0xbcu || hi == 0xbdu || (hi >= 0xc8u && hi <= 0xcfu));
  bool predec = (hi == 0xb4u || hi == 0xb5u);
  u32 rn = (hi >= 0xc0u && hi <= 0xcfu) ? (hi & 7u) : REG_SP;
  u32 reglist = opcode & 0xffu;
  u32 count;
  u32 base;
  s32 addr_off;
  u32 endaddr;
  u32 address;
  bool wrbck_base;
  bool base_first;
  bool writeback_first;
  u32 i;

  if (hi == 0xb5u)
    reglist |= 1u << REG_LR;
  else if (hi == 0xbdu)
    reglist |= 1u << REG_PC;

  count = riscv_word_bit_count(reglist & 0xffu);
  if (reglist & ((1u << REG_LR) | (1u << REG_PC)))
    count++;

  base = reg[rn];
  addr_off = predec ? -4 : 4;
  endaddr = base + (u32)(addr_off * (s32)count);
  address = predec ? endaddr : base;
  address &= ~3u;

  wrbck_base = ((1u << rn) & reglist) != 0;
  base_first = (((1u << rn) - 1u) & reglist) == 0;
  writeback_first = load || !(wrbck_base && base_first);

  if (writeback_first)
    reg[rn] = endaddr;

  reg[REG_PC] = pc + 2u;

  if (load)
  {
    for (i = 0; i < 8; i++)
    {
      if ((reglist >> i) & 1u)
      {
        reg[i] = read_memory32(address);
        address += 4u;
      }
    }

    if (reglist & (1u << REG_PC))
    {
      reg[REG_PC] = read_memory32(address) & ~1u;
    }
  }
  else
  {
    for (i = 0; i < 8; i++)
    {
      if ((reglist >> i) & 1u)
      {
        riscv_store_u32(address, reg[i]);
        address += 4u;
      }
    }

    if (reglist & (1u << REG_LR))
      riscv_store_u32(address, reg[REG_LR]);
  }

  if (!writeback_first)
    reg[rn] = endaddr;
}

static u32 riscv_thumb_block_memory_extra_cycles(u32 opcode)
{
  u32 hi = opcode >> 8;
  u32 reglist = opcode & 0xffu;
  u32 count = riscv_word_bit_count(reglist);

  if (hi == 0xb5u || hi == 0xbdu)
    count++;

  return count;
}

static bool riscv_thumb_condition_passed(u32 condition)
{
  u32 n = riscv_thumb_flag_bit(RISCV_CPSR_N);
  u32 z = riscv_thumb_flag_bit(RISCV_CPSR_Z);
  u32 c = riscv_thumb_flag_bit(RISCV_CPSR_C);
  u32 v = riscv_thumb_flag_bit(RISCV_CPSR_V);

  switch (condition & 0xfu)
  {
    case 0x0: return z != 0;
    case 0x1: return z == 0;
    case 0x2: return c != 0;
    case 0x3: return c == 0;
    case 0x4: return n != 0;
    case 0x5: return n == 0;
    case 0x6: return v != 0;
    case 0x7: return v == 0;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xa: return n == v;
    case 0xb: return n != v;
    case 0xc: return !z && (n == v);
    case 0xd: return z || (n != v);
    default: return false;
  }
}

static u32 riscv_thumb_memory_load(u32 address, u32 type)
{
  switch (type)
  {
    case 0: return read_memory32(address);
    case 1: return read_memory16(address);
    case 2: return read_memory8(address);
    case 3: return read_memory8s(address);
    default: return read_memory16s(address);
  }
}

static void riscv_thumb_memory_store(u32 address, u32 value, u32 type)
{
  if (type == 0)
    riscv_store_u32(address, value);
  else if (type == 1)
    riscv_store_u16(address, value);
  else
    riscv_store_u8(address, value);
}

static void function_cc riscv_thumb_execute_bl_pair(u32 first_opcode,
                                                    u32 second_opcode,
                                                    u32 pc)
{
  s32 high_offset = (s32)((first_opcode & 0x07ffu) << 21) >> 9;
  u32 low_offset = (second_opcode & 0x07ffu) * 2u;

  reg[REG_LR] = (pc + 2u) | 1u;
  reg[REG_PC] = pc + 2u + (u32)high_offset + low_offset;
}

static u32 function_cc riscv_thumb_execute(u32 opcode, u32 pc)
{
  u32 hi = opcode >> 8;
  u32 rd = opcode & 7u;
  u32 rs = (opcode >> 3) & 7u;
  u32 rn = (opcode >> 6) & 7u;
  u32 imm = opcode & 0xffu;
  u32 extra_cycles = 0;
  u32 value;
  u32 address;

  reg[REG_PC] = pc + 2u;

  switch (hi)
  {
    case 0x00 ... 0x07:
      riscv_thumb_shift_lsl_imm(rd, rs, (opcode >> 6) & 0x1fu);
      break;

    case 0x08 ... 0x0f:
      riscv_thumb_shift_lsr_imm(rd, rs, (opcode >> 6) & 0x1fu);
      break;

    case 0x10 ... 0x17:
      riscv_thumb_shift_asr_imm(rd, rs, (opcode >> 6) & 0x1fu);
      break;

    case 0x18 ... 0x19:
      riscv_thumb_add(rd, reg[rs], reg[rn], 0);
      break;

    case 0x1a ... 0x1b:
      riscv_thumb_sub(rd, reg[rs], reg[rn], 1);
      break;

    case 0x1c ... 0x1d:
      riscv_thumb_add(rd, reg[rs], rn, 0);
      break;

    case 0x1e ... 0x1f:
      riscv_thumb_sub(rd, reg[rs], rn, 1);
      break;

    case 0x20 ... 0x27:
      rd = hi & 7u;
      reg[rd] = imm;
      riscv_thumb_store_nz_preserve_cv(imm);
      break;

    case 0x28 ... 0x2f:
      rd = hi & 7u;
      riscv_thumb_test_sub(reg[rd], imm);
      break;

    case 0x30 ... 0x37:
      rd = hi & 7u;
      riscv_thumb_add(rd, reg[rd], imm, 0);
      break;

    case 0x38 ... 0x3f:
      rd = hi & 7u;
      riscv_thumb_sub(rd, reg[rd], imm, 1);
      break;

    case 0x40:
      switch ((opcode >> 6) & 3u)
      {
        case 0:
          value = reg[rd] & reg[rs];
          reg[rd] = value;
          riscv_thumb_store_nz_preserve_cv(value);
          break;
        case 1:
          value = reg[rd] ^ reg[rs];
          reg[rd] = value;
          riscv_thumb_store_nz_preserve_cv(value);
          break;
        case 2:
          riscv_thumb_shift_lsl_reg(rd, rs);
          break;
        default:
          riscv_thumb_shift_lsr_reg(rd, rs);
          break;
      }
      break;

    case 0x41:
      switch ((opcode >> 6) & 3u)
      {
        case 0:
          riscv_thumb_shift_asr_reg(rd, rs);
          break;
        case 1:
          riscv_thumb_add(rd, reg[rd], reg[rs], riscv_thumb_c_flag());
          break;
        case 2:
          riscv_thumb_sub(rd, reg[rd], reg[rs], riscv_thumb_c_flag());
          break;
        default:
          riscv_thumb_shift_ror_reg(rd, rs);
          break;
      }
      break;

    case 0x42:
      switch ((opcode >> 6) & 3u)
      {
        case 0:
          riscv_thumb_store_nz_preserve_cv(reg[rd] & reg[rs]);
          break;
        case 1:
          riscv_thumb_sub(rd, 0, reg[rs], 1);
          break;
        case 2:
          riscv_thumb_test_sub(reg[rd], reg[rs]);
          break;
        default:
          riscv_thumb_test_add(reg[rd], reg[rs]);
          break;
      }
      break;

    case 0x43:
      switch ((opcode >> 6) & 3u)
      {
        case 0:
          value = reg[rd] | reg[rs];
          reg[rd] = value;
          riscv_thumb_store_nz_preserve_cv(value);
          break;
        case 1:
          value = reg[rd] * reg[rs];
          reg[rd] = value;
          riscv_thumb_store_nz_preserve_cv(value);
          extra_cycles += 2u;
          break;
        case 2:
          value = reg[rd] & ~reg[rs];
          reg[rd] = value;
          riscv_thumb_store_nz_preserve_cv(value);
          break;
        default:
          value = ~reg[rs];
          reg[rd] = value;
          riscv_thumb_store_nz_preserve_cv(value);
          break;
      }
      break;

    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    {
      u32 hrs = (opcode >> 3) & 0x0fu;
      u32 hrd = ((opcode >> 4) & 0x08u) | (opcode & 0x07u);
      u32 src = riscv_thumb_reg_value(hrs, pc + 4u);
      u32 dst = riscv_thumb_reg_value(hrd, pc + 4u);

      if (hi == 0x44u)
      {
        value = dst + src;
        if (hrd == REG_PC)
          reg[REG_PC] = value & ~1u;
        else
          reg[hrd] = value;
      }
      else if (hi == 0x45u)
      {
        riscv_thumb_test_sub(dst, src);
      }
      else if (hi == 0x46u)
      {
        if (hrd == REG_PC)
          reg[REG_PC] = src & ~1u;
        else
          reg[hrd] = src;
      }
      else
      {
        if (src & 1u)
        {
          reg[REG_PC] = src - 1u;
        }
        else
        {
          reg[REG_PC] = src;
          reg[REG_CPSR] &= ~RISCV_CPSR_T;
        }
      }
      break;
    }

    case 0x48 ... 0x4f:
      rd = hi & 7u;
      address = (pc & ~2u) + 4u + ((opcode & 0xffu) * 4u);
      reg[rd] = read_memory32(address);
      extra_cycles += 2u;
      break;

    case 0x50 ... 0x5f:
    {
      u32 ro = (opcode >> 6) & 7u;
      u32 rb = (opcode >> 3) & 7u;
      u32 mem_type;
      bool load;

      rd = opcode & 7u;
      address = reg[rb] + reg[ro];

      switch ((opcode >> 9) & 7u)
      {
        case 0:
          load = false;
          mem_type = 0;
          break;
        case 1:
          load = false;
          mem_type = 1;
          break;
        case 2:
          load = false;
          mem_type = 2;
          break;
        case 3:
          load = true;
          mem_type = 3;
          break;
        case 4:
          load = true;
          mem_type = 0;
          break;
        case 5:
          load = true;
          mem_type = 1;
          break;
        case 6:
          load = true;
          mem_type = 2;
          break;
        default:
          load = true;
          mem_type = 4;
          break;
      }

      if (load)
      {
        reg[rd] = riscv_thumb_memory_load(address, mem_type);
        extra_cycles += 2u;
      }
      else
      {
        riscv_thumb_memory_store(address, reg[rd], mem_type);
        extra_cycles += 1u;
      }
      break;
    }

    case 0x60 ... 0x67:
    case 0x68 ... 0x6f:
    case 0x70 ... 0x77:
    case 0x78 ... 0x7f:
    case 0x80 ... 0x87:
    case 0x88 ... 0x8f:
    {
      u32 rb = (opcode >> 3) & 7u;
      u32 mem_imm = (opcode >> 6) & 0x1fu;
      bool load = (hi & 0x08u) != 0;
      u32 mem_type;

      rd = opcode & 7u;

      if (hi < 0x70u)
      {
        address = reg[rb] + mem_imm * 4u;
        mem_type = 0;
      }
      else if (hi < 0x80u)
      {
        address = reg[rb] + mem_imm;
        mem_type = 2;
      }
      else
      {
        address = reg[rb] + mem_imm * 2u;
        mem_type = 1;
      }

      if (load)
      {
        reg[rd] = riscv_thumb_memory_load(address, mem_type);
        extra_cycles += 2u;
      }
      else
      {
        riscv_thumb_memory_store(address, reg[rd], mem_type);
        extra_cycles += 1u;
      }
      break;
    }

    case 0x90 ... 0x97:
      rd = hi & 7u;
      riscv_thumb_memory_store(reg[REG_SP] + (imm * 4u), reg[rd], 0);
      extra_cycles += 1u;
      break;

    case 0x98 ... 0x9f:
      rd = hi & 7u;
      reg[rd] = read_memory32(reg[REG_SP] + (imm * 4u));
      extra_cycles += 2u;
      break;

    case 0xa0 ... 0xa7:
      rd = hi & 7u;
      reg[rd] = (pc & ~2u) + 4u + (imm * 4u);
      break;

    case 0xa8 ... 0xaf:
      rd = hi & 7u;
      reg[rd] = reg[REG_SP] + (imm * 4u);
      break;

    case 0xb0 ... 0xb3:
      imm = opcode & 0x7fu;
      if (opcode & 0x80u)
        reg[REG_SP] -= imm * 4u;
      else
        reg[REG_SP] += imm * 4u;
      break;

    case 0xb4:
    case 0xb5:
    case 0xbc:
    case 0xbd:
      riscv_thumb_block_memory(opcode, pc);
      extra_cycles += riscv_thumb_block_memory_extra_cycles(opcode);
      break;

    case 0xc0 ... 0xcf:
      riscv_thumb_block_memory(opcode, pc);
      extra_cycles += riscv_thumb_block_memory_extra_cycles(opcode);
      break;

    case 0xd0 ... 0xdd:
      if (riscv_thumb_condition_passed(hi & 0x0fu))
        reg[REG_PC] = pc + 4u + (u32)((s32)(s8)(opcode & 0xffu) * 2);
      break;

    case 0xdf:
      riscv_execute_swi_thumb(pc);
      break;

    case 0xe0 ... 0xe7:
      reg[REG_PC] = pc + 4u +
        (u32)((s32)((opcode & 0x07ffu) << 21) >> 20);
      break;

    case 0xf8 ... 0xff:
      value = (pc + 2u) | 1u;
      reg[REG_PC] = reg[REG_LR] + ((opcode & 0x07ffu) * 2u);
      reg[REG_LR] = value;
      break;

    default:
      break;
  }

  return extra_cycles;
}

static const u32 riscv_psr_cpsr_masks[4][2] =
{
  { 0x00000000u, 0x00000000u },
  { 0x00000020u, 0x000000EFu },
  { 0xF0000000u, 0xF0000000u },
  { 0xF0000020u, 0xF00000EFu }
};

static const u32 riscv_psr_spsr_masks[4] =
{
  0x00000000u, 0x000000EFu, 0xF0000000u, 0xF00000EFu
};

static const u32 riscv_psr_cpu_modes[16] =
{
  MODE_USER, MODE_FIQ, MODE_IRQ, MODE_SUPERVISOR,
  MODE_INVALID, MODE_INVALID, MODE_INVALID, MODE_ABORT,
  MODE_INVALID, MODE_INVALID, MODE_INVALID, MODE_UNDEFINED,
  MODE_INVALID, MODE_INVALID, MODE_INVALID, MODE_SYSTEM
};

static void function_cc riscv_store_cpsr(u32 source, u32 psr_pfield)
{
  u32 store_mask =
    riscv_psr_cpsr_masks[psr_pfield & 3u][PRIVMODE(reg[CPU_MODE])];
  u32 cpsr = (source & store_mask) | (reg[REG_CPSR] & ~store_mask);

  reg[REG_CPSR] = cpsr;
  if (store_mask & 0xffu)
  {
    set_cpu_mode(riscv_psr_cpu_modes[cpsr & 0xfu]);
    check_and_raise_interrupts();
  }
}

static void function_cc riscv_store_spsr(u32 source, u32 psr_pfield)
{
  u32 store_mask = riscv_psr_spsr_masks[psr_pfield & 3u];
  u32 mode = reg[CPU_MODE] & 0xfu;
  u32 old_spsr = spsr[mode];

  spsr[mode] = (source & store_mask) | (old_spsr & ~store_mask);
}

static void function_cc riscv_execute_spsr_restore(void)
{
  u32 mode = reg[CPU_MODE] & 0xfu;

  if (reg[CPU_MODE] == MODE_USER || reg[CPU_MODE] == MODE_SYSTEM)
    return;

  reg[REG_CPSR] = spsr[mode];
  set_cpu_mode(riscv_psr_cpu_modes[reg[REG_CPSR] & 0xfu]);
  check_and_raise_interrupts();
}

static void riscv_emit_arm_cpsr_flags_store_packed(u8 **ptr_ref,
                                                   riscv_reg_number source)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, REG_CPSR);
  translation_ptr = ptr;
  riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, 4);
  riscv_emit_srli(riscv_reg_t1, riscv_reg_t1, 4);
  if (source != riscv_reg_zero)
    riscv_emit_or(source, source, riscv_reg_t1);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR,
                           source == riscv_reg_zero ? riscv_reg_t1 : source);
  *ptr_ref = ptr;
}

static u32 riscv_arm_cpsr_flags_from_status(u32 flag_mask)
{
  u32 cpsr_flags = 0;

  if (flag_mask & 0x08u)
    cpsr_flags |= RISCV_CPSR_N;
  if (flag_mask & 0x04u)
    cpsr_flags |= RISCV_CPSR_Z;
  if (flag_mask & 0x02u)
    cpsr_flags |= RISCV_CPSR_C;
  if (flag_mask & 0x01u)
    cpsr_flags |= RISCV_CPSR_V;

  return cpsr_flags;
}

static void riscv_emit_arm_cpsr_flags_select(u8 **ptr_ref,
                                             riscv_reg_number source,
                                             u32 flag_mask)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr = ptr;

  switch (flag_mask & 0x0fu)
  {
    case 0x0f:
      riscv_emit_srli(source, source, 28);
      riscv_emit_slli(source, source, 28);
      break;
    case 0x0e:
      riscv_emit_srli(source, source, 29);
      riscv_emit_slli(source, source, 29);
      break;
    case 0x0c:
      riscv_emit_srli(source, source, 30);
      riscv_emit_slli(source, source, 30);
      break;
    case 0x08:
      riscv_emit_srli(source, source, 31);
      riscv_emit_slli(source, source, 31);
      break;
    default:
      riscv_emit_li(&ptr, riscv_reg_t5,
                    riscv_arm_cpsr_flags_from_status(flag_mask));
      translation_ptr = ptr;
      riscv_emit_and(source, source, riscv_reg_t5);
      break;
  }

  ptr = translation_ptr;
  *ptr_ref = ptr;
}

static void riscv_emit_arm_spsr_flags_store(u8 **ptr_ref,
                                            riscv_reg_number source)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, CPU_MODE);
  riscv_emit_li(&ptr, riscv_reg_t1, (u32)(uintptr_t)&spsr[0]);
  translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, 0xf);
  riscv_emit_slli(riscv_reg_t0, riscv_reg_t0, 2);
  riscv_emit_add(riscv_reg_t1, riscv_reg_t1, riscv_reg_t0);
  riscv_emit_lw(riscv_reg_t3, riscv_reg_t1, 0);
  riscv_emit_srli(source, source, 28);
  riscv_emit_slli(source, source, 28);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 4);
  riscv_emit_srli(riscv_reg_t3, riscv_reg_t3, 4);
  riscv_emit_or(source, source, riscv_reg_t3);
  riscv_emit_sw(source, riscv_reg_t1, 0);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_spsr_flags_store_packed(u8 **ptr_ref,
                                                   riscv_reg_number source)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, CPU_MODE);
  riscv_emit_li(&ptr, riscv_reg_t1, (u32)(uintptr_t)&spsr[0]);
  translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, 0xf);
  riscv_emit_slli(riscv_reg_t0, riscv_reg_t0, 2);
  riscv_emit_add(riscv_reg_t1, riscv_reg_t1, riscv_reg_t0);
  riscv_emit_lw(riscv_reg_t3, riscv_reg_t1, 0);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 4);
  riscv_emit_srli(riscv_reg_t3, riscv_reg_t3, 4);
  if (source != riscv_reg_zero)
    riscv_emit_or(source, source, riscv_reg_t3);
  riscv_emit_sw(source == riscv_reg_zero ? riscv_reg_t3 : source,
                riscv_reg_t1, 0);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static u32 riscv_word_bit_count(u32 word)
{
  u32 count = 0;

  while (word)
  {
    count += word & 1u;
    word >>= 1;
  }

  return count;
}

static void function_cc riscv_arm_block_memory(u32 opcode, u32 pc)
{
  u32 load = (opcode >> 20) & 1u;
  u32 writeback = (opcode >> 21) & 1u;
  u32 sbit = (opcode >> 22) & 1u;
  u32 up = (opcode >> 23) & 1u;
  u32 pre_index = (opcode >> 24) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 reglist = opcode & 0xffffu;
  u32 numops = riscv_word_bit_count(reglist);
  u32 base = reg[rn];
  s32 addr_off = up ? 4 : -4;
  u32 endaddr = base + (u32)(addr_off * (s32)numops);
  u32 address;
  u32 old_cpsr = reg[REG_CPSR];
  bool wrbck_base;
  bool base_first;
  bool writeback_first;
  u32 i;

  if (pre_index && up)
    address = base + 4u;
  else if (!pre_index && up)
    address = base;
  else if (pre_index)
    address = endaddr;
  else
    address = endaddr + 4u;
  address &= ~3u;

  if (sbit && (!load || rn != REG_PC))
    set_cpu_mode(MODE_USER);

  wrbck_base = ((1u << rn) & reglist) != 0;
  base_first = (((1u << rn) - 1u) & reglist) == 0;
  writeback_first = load || !(wrbck_base && base_first);

  if (writeback && writeback_first)
    reg[rn] = endaddr;

  reg[REG_PC] = pc + 4u;

  for (i = 0; i < 16; i++)
  {
    if ((reglist >> i) & 1u)
    {
      if (load)
      {
        reg[i] = read_memory32(address);
      }
      else
      {
        u32 value = (i == REG_PC) ? reg[REG_PC] + 4u : reg[i];
        riscv_store_u32(address, value);
      }
      address += 4u;
    }
  }

  if (writeback && !writeback_first)
    reg[rn] = endaddr;

  if (sbit && (!load || rn != REG_PC))
    set_cpu_mode(riscv_psr_cpu_modes[old_cpsr & 0xfu]);

  if (sbit && load && (reglist & (1u << REG_PC)))
    riscv_execute_spsr_restore();
}

static void function_cc riscv_hle_div(u32 divarm)
{
  s32 numerator = (s32)(divarm ? reg[1] : reg[0]);
  s32 denominator = (s32)(divarm ? reg[0] : reg[1]);
  s32 quotient;
  s32 remainder;
  u32 quotient_u32;

  if (denominator == 0)
  {
    quotient = 0;
    remainder = numerator;
  }
  else if (((u32)numerator == 0x80000000u) && (denominator == -1))
  {
    quotient = (s32)0x80000000u;
    remainder = 0;
  }
  else
  {
    quotient = numerator / denominator;
    remainder = numerator % denominator;
  }

  quotient_u32 = (u32)quotient;
  reg[0] = quotient_u32;
  reg[1] = (u32)remainder;
  reg[3] = (quotient_u32 & 0x80000000u) ?
    (0u - quotient_u32) : quotient_u32;
}

static u32 riscv_arm_expand_imm(u32 opcode)
{
  u32 imm = opcode & 0xffu;
  u32 rot = ((opcode >> 8) & 0xfu) * 2u;

  if (!rot)
    return imm;

  return (imm >> rot) | (imm << (32u - rot));
}

static s32 riscv_arm_branch_delta(u32 opcode)
{
  return ((s32)((opcode & 0x00ffffffu) << 8) >> 6) + 8;
}

static void riscv_emit_helper_call_no_flush(u8 **ptr,
                                            const riscv_jit_block_meta *meta)
{
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_terminal_call_sites++;
#endif
  riscv_emit_li(ptr, riscv_reg_a0, (u32)(uintptr_t)meta);
  riscv_emit_control_tail_jump(ptr, meta);
}

static void riscv_emit_helper_call(u8 **ptr, const riscv_jit_block_meta *meta)
{
  riscv_emit_mapped_regs_flush_dirty(ptr);
  riscv_emit_helper_call_no_flush(ptr, meta);
}

static void riscv_emit_terminal_helper_call(u8 **ptr,
                                            riscv_jit_block_meta *meta)
{
  u8 *start = *ptr;

  riscv_emit_helper_call(ptr, meta);
  riscv_terminal_helper_size = (u32)(*ptr - start);
  meta->flags |= RISCV_BLOCK_TERMINAL_EMITTED;
  /* Finalize overwrites this with the guest end-PC delta. */
  riscv_block_meta_set_terminal_offset(meta, (u32)(*ptr - (u8 *)meta));
}

static void riscv_emit_terminal_helper_call_no_flush(
  u8 **ptr, riscv_jit_block_meta *meta)
{
  riscv_emit_terminal_helper_call(ptr, meta);
}

static void riscv_count_control_stub_entry(void)
{
  RISCV_CONTROL_COUNT(riscv_control_stub_entries);
  riscv_blocks_executed++;
}

static void riscv_note_supported_block_exit(
  const riscv_jit_block_meta *meta)
{
  if (meta->start_pc < 0x00004000u)
    riscv_bios_native_blocks_executed++;
  riscv_note_runtime_block_execute(meta->start_pc,
                                   riscv_block_meta_end_pc(meta),
                                   riscv_block_meta_thumb(meta));
}

/* These helpers have a deliberately narrow contract: account for the source
 * block and perform one lookup. They do not run the scheduler and do not
 * publish the resident s10 cycle budget on a successful native transition. */
__attribute__((used, noinline))
u8 *riscv_jit_lookup_fallthrough(const riscv_jit_block_meta *meta,
                                 s32 cycles)
{
  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
  {
    riscv_cycles_remaining = cycles;
    return riscv_jit_control_slow(meta);
  }

  RISCV_CONTROL_COUNT(riscv_control_lookup_stub_entries);
  riscv_count_control_stub_entry();
  riscv_note_supported_block_exit(meta);
  return riscv_lookup_or_fallback(RISCV_CONTROL_LOOKUP_FALLTHROUGH, cycles);
}

__attribute__((used, noinline))
u8 *riscv_jit_lookup_indirect(const riscv_jit_block_meta *meta,
                              s32 cycles)
{
  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
  {
    riscv_cycles_remaining = cycles;
    return riscv_jit_control_slow(meta);
  }

  RISCV_CONTROL_COUNT(riscv_control_lookup_stub_entries);
  riscv_count_control_stub_entry();
  riscv_note_supported_block_exit(meta);
  return riscv_lookup_or_fallback(RISCV_CONTROL_LOOKUP_INDIRECT, cycles);
}

/* Slow control path only: generated code reaches this after the assembly
 * guards observe cycle exhaustion, alerts, HALT/idle state, or an unsupported
 * block. Normal fallthrough and indirect misses use the lookup helpers above. */
static u8 *riscv_jit_control_slow(const riscv_jit_block_meta *meta)
{
  u32 update_ret;
  cpu_alert_type alert = CPU_ALERT_NONE;

  RISCV_CONTROL_COUNT(riscv_control_slow_path_entries);
  riscv_count_control_stub_entry();

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
  {
    u32 pc;
    u32 thumb;

    if (meta)
    {
      pc = meta->start_pc;
      thumb = riscv_block_meta_thumb(meta);
    }
    else
    {
      riscv_current_lookup_state(&pc, &thumb);
    }

    riscv_interpreter_fallbacks++;
    riscv_unsupported_fallbacks++;
    if (pc < 0x00004000u)
      riscv_bios_interpreter_fallbacks++;
    riscv_note_runtime_fallback(RISCV_RUNTIME_FALLBACK_UNSUPPORTED,
                                pc, thumb,
                                RISCV_RUNTIME_LOOKUP_UNSUPPORTED,
                                (u32)riscv_cycles_remaining);
    riscv_run_interpreter_remainder();
    return NULL;
  }

  riscv_note_supported_block_exit(meta);

  if (riscv_cycles_remaining <= 0)
    RISCV_CONTROL_COUNT(riscv_control_cycle_exits);

  if (riscv_cpu_alert != CPU_ALERT_NONE)
    alert = riscv_handle_cpu_alert();

  if (reg[REG_PC] == idle_loop_target_pc && riscv_cycles_remaining > 0)
    riscv_cycles_remaining = 0;

  if ((alert & CPU_ALERT_HALT) || reg[CPU_HALT_STATE] != CPU_ACTIVE ||
      riscv_cycles_remaining <= 0)
  {
    RISCV_CONTROL_COUNT(riscv_control_scheduler_updates);
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
      RISCV_CONTROL_LOOKUP_INDIRECT : RISCV_CONTROL_LOOKUP_FALLTHROUGH,
    riscv_cycles_remaining);
}

void riscv_emit_block_prologue(u8 **translation_ptr_ref,
                               riscv_jit_block_meta **meta)
{
  u8 *ptr = riscv_align_ptr(*translation_ptr_ref);

  riscv_invalidate_mapped_regs();
  riscv_terminal_helper_size = 0;
  riscv_arm_conditional_block_active = false;
  riscv_arm_conditional_entry_dirty_mask = 0;
  riscv_arm_conditional_entry_valid_mask = 0;

  *meta = (riscv_jit_block_meta *)(void *)ptr;
  (*meta)->start_pc = 0;
  (*meta)->end_delta_thumb = 0;
  (*meta)->chain_units = 0;
  (*meta)->flags = (u16)RISCV_BLOCK_NATIVE_SUPPORTED;
  (*meta)->reserved = 0;
  riscv_block_meta_set_chain_offset(*meta, 0u);

  ptr += block_prologue_size;
  riscv_note_mapped_regs_reloaded();
  *translation_ptr_ref = ptr;
}

void riscv_record_block_start_pc(riscv_jit_block_meta *meta,
                                 u32 block_start_pc)
{
  if (meta)
    meta->start_pc = block_start_pc;
}

void riscv_emit_block_pc_base(u8 **translation_ptr_ref,
                              riscv_jit_block_meta *meta,
                              u32 block_start_pc)
{
  riscv_record_block_start_pc(meta, block_start_pc);
  (void)translation_ptr_ref;
}

void riscv_mark_block_unsupported(riscv_jit_block_meta *meta)
{
  if (meta)
    meta->flags &= ~RISCV_BLOCK_NATIVE_SUPPORTED;
  riscv_invalidate_mapped_regs();
}

void riscv_mark_block_no_fallthrough(riscv_jit_block_meta *meta)
{
  if (meta)
    meta->flags |= RISCV_BLOCK_NO_FALLTHROUGH;
}

bool riscv_block_is_native_supported(const riscv_jit_block_meta *meta)
{
  return meta != NULL && (meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED) != 0u;
}

static void riscv_emit_arm_cpsr_c_load(u8 **ptr_ref, riscv_reg_number rd);

static bool riscv_arm_operand2_preserves_carry(u32 opcode)
{
  u32 imm_op = (opcode >> 25) & 1u;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;

  if (imm_op)
    return ((opcode >> 8) & 0xfu) == 0;

  if ((opcode >> 4) & 1u)
    return false;

  return shift_type == 0 && shift == 0;
}

static void riscv_emit_arm_data_proc_const_reg_shift(u8 **ptr_ref,
                                                     u32 shift_type,
                                                     u32 shift,
                                                     bool update_carry)
{
  u8 *translation_ptr = *ptr_ref;

  shift &= 0xffu;

  switch (shift_type)
  {
    case 0:
      if (!shift)
        break;
      if (shift < 32u)
      {
        if (update_carry)
        {
          riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 32u - shift);
          riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        }
        riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, shift);
      }
      else
      {
        if (update_carry)
        {
          if (shift == 32u)
            riscv_emit_andi(riscv_reg_t3, riscv_reg_t1, 1);
          else
            riscv_emit_add(riscv_reg_t3, riscv_reg_zero, riscv_reg_zero);
        }
        riscv_emit_add(riscv_reg_t1, riscv_reg_zero, riscv_reg_zero);
      }
      break;
    case 1:
      if (!shift)
        break;
      if (shift < 32u)
      {
        if (update_carry)
        {
          riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, shift - 1u);
          riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        }
        riscv_emit_srli(riscv_reg_t1, riscv_reg_t1, shift);
      }
      else
      {
        if (update_carry)
        {
          if (shift == 32u)
            riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 31);
          else
            riscv_emit_add(riscv_reg_t3, riscv_reg_zero, riscv_reg_zero);
        }
        riscv_emit_add(riscv_reg_t1, riscv_reg_zero, riscv_reg_zero);
      }
      break;
    case 2:
      if (!shift)
        break;
      if (shift < 32u)
      {
        if (update_carry)
        {
          riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, shift - 1u);
          riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        }
        riscv_emit_srai(riscv_reg_t1, riscv_reg_t1, shift);
      }
      else
      {
        if (update_carry)
          riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 31);
        riscv_emit_srai(riscv_reg_t1, riscv_reg_t1, 31);
      }
      break;
    default:
      if (!shift)
        break;
      shift &= 31u;
      if (shift)
      {
        riscv_emit_srli(update_carry ? riscv_reg_t4 : riscv_reg_t3,
                        riscv_reg_t1, shift);
        riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, 32u - shift);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1,
                      update_carry ? riscv_reg_t4 : riscv_reg_t3);
      }
      if (update_carry)
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 31);
      break;
  }

  *ptr_ref = translation_ptr;
}

static bool riscv_emit_arm_data_proc_operand2(u8 **ptr_ref,
                                              riscv_jit_block_meta *meta,
                                              u32 opcode,
                                              u32 pc)
{
  u32 imm_op = (opcode >> 25) & 1u;
  u32 rm = opcode & 0xfu;
  u32 rs = (opcode >> 8) & 0xfu;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if (imm_op)
  {
    riscv_emit_li(&ptr, riscv_reg_t1, riscv_arm_expand_imm(opcode));
    *ptr_ref = ptr;
    return true;
  }

  if ((opcode >> 4) & 1u)
  {
    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, meta, rm, pc + 12u);

    if (rs == REG_PC)
    {
      translation_ptr = ptr;
      riscv_emit_arm_data_proc_const_reg_shift(&translation_ptr, shift_type,
                                               pc + 8u, false);
      *ptr_ref = translation_ptr;
      return true;
    }

    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t3, meta, rs, pc + 8u);
    translation_ptr = ptr;
    riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 0xff);

    switch (shift_type)
    {
      case 0:
        riscv_emit_sll(riscv_reg_t1, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t3, 32);
        riscv_emit_sub(riscv_reg_t4, riscv_reg_zero, riscv_reg_t4);
        riscv_emit_and(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
        break;
      case 1:
        riscv_emit_srl(riscv_reg_t1, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t3, 32);
        riscv_emit_sub(riscv_reg_t4, riscv_reg_zero, riscv_reg_t4);
        riscv_emit_and(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
        break;
      case 2:
        riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t3, 32);
        riscv_emit_addi(riscv_reg_t4, riscv_reg_t4, -1);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t3, riscv_reg_t4);
        riscv_emit_sra(riscv_reg_t1, riscv_reg_t1, riscv_reg_t3);
        break;
      default:
        riscv_emit_sub(riscv_reg_t4, riscv_reg_zero, riscv_reg_t3);
        riscv_emit_srl(riscv_reg_t5, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_sll(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t5);
        break;
    }

    *ptr_ref = translation_ptr;
    return true;
  }

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, meta, rm, pc + 8u);
  translation_ptr = ptr;

  switch (shift_type)
  {
    case 0:
      if (shift)
        riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, shift);
      break;
    case 1:
      if (shift)
        riscv_emit_srli(riscv_reg_t1, riscv_reg_t1, shift);
      else
        riscv_emit_add(riscv_reg_t1, riscv_reg_zero, riscv_reg_zero);
      break;
    case 2:
      riscv_emit_srai(riscv_reg_t1, riscv_reg_t1, shift ? shift : 31u);
      break;
    default:
      if (shift)
      {
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, shift);
        riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, 32u - shift);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t3);
      }
      else
      {
        ptr = translation_ptr;
        riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
        translation_ptr = ptr;
        riscv_emit_srli(riscv_reg_t1, riscv_reg_t1, 1);
        riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 31);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t3);
      }
      break;
  }

  *ptr_ref = translation_ptr;
  return true;
}

static void riscv_emit_reg_lsl_with_carry(u8 **ptr_ref,
                                          riscv_reg_number result_reg,
                                          riscv_reg_number value_reg,
                                          riscv_reg_number shift_reg,
                                          riscv_reg_number carry_reg,
                                          riscv_reg_number temp_reg)
{
  u8 *translation_ptr = *ptr_ref;
  u8 *shift_done;

  if (result_reg != value_reg)
    riscv_emit_add(result_reg, value_reg, riscv_reg_zero);

  riscv_emit_branch_with_source(&translation_ptr, &shift_done, 0x0,
                                shift_reg, riscv_reg_zero, 0);
  riscv_emit_addi(temp_reg, shift_reg, -1);
  riscv_emit_sll(result_reg, value_reg, temp_reg);
  riscv_emit_srli(carry_reg, result_reg, 31);
  riscv_emit_sltiu(temp_reg, shift_reg, 33);
  riscv_emit_slli(result_reg, result_reg, 1);
  riscv_emit_sub(temp_reg, riscv_reg_zero, temp_reg);
  riscv_emit_and(carry_reg, carry_reg, temp_reg);
  riscv_emit_and(result_reg, result_reg, temp_reg);
  riscv_patch_local_branch(shift_done, translation_ptr);

  *ptr_ref = translation_ptr;
}

static void riscv_emit_reg_lsr_with_carry(u8 **ptr_ref,
                                          riscv_reg_number result_reg,
                                          riscv_reg_number value_reg,
                                          riscv_reg_number shift_reg,
                                          riscv_reg_number carry_reg,
                                          riscv_reg_number temp_reg)
{
  u8 *translation_ptr = *ptr_ref;
  u8 *shift_done;

  if (result_reg != value_reg)
    riscv_emit_add(result_reg, value_reg, riscv_reg_zero);

  riscv_emit_branch_with_source(&translation_ptr, &shift_done, 0x0,
                                shift_reg, riscv_reg_zero, 0);
  riscv_emit_addi(temp_reg, shift_reg, -1);
  riscv_emit_srl(result_reg, value_reg, temp_reg);
  riscv_emit_andi(carry_reg, result_reg, 1);
  riscv_emit_sltiu(temp_reg, shift_reg, 33);
  riscv_emit_srli(result_reg, result_reg, 1);
  riscv_emit_sub(temp_reg, riscv_reg_zero, temp_reg);
  riscv_emit_and(carry_reg, carry_reg, temp_reg);
  riscv_emit_and(result_reg, result_reg, temp_reg);
  riscv_patch_local_branch(shift_done, translation_ptr);

  *ptr_ref = translation_ptr;
}

static void riscv_emit_reg_asr_with_carry(u8 **ptr_ref,
                                          riscv_reg_number result_reg,
                                          riscv_reg_number value_reg,
                                          riscv_reg_number shift_reg,
                                          riscv_reg_number carry_reg,
                                          riscv_reg_number temp_reg)
{
  u8 *translation_ptr = *ptr_ref;
  u8 *shift_done;

  if (result_reg != value_reg)
    riscv_emit_add(result_reg, value_reg, riscv_reg_zero);

  riscv_emit_branch_with_source(&translation_ptr, &shift_done, 0x0,
                                shift_reg, riscv_reg_zero, 0);
  riscv_emit_addi(temp_reg, shift_reg, -1);
  riscv_emit_sltiu(carry_reg, temp_reg, 31);
  riscv_emit_addi(carry_reg, carry_reg, -1);
  riscv_emit_or(temp_reg, temp_reg, carry_reg);
  riscv_emit_srl(carry_reg, value_reg, temp_reg);
  riscv_emit_andi(carry_reg, carry_reg, 1);
  riscv_patch_local_branch(shift_done, translation_ptr);

  riscv_emit_sltiu(temp_reg, shift_reg, 32);
  riscv_emit_addi(temp_reg, temp_reg, -1);
  riscv_emit_or(temp_reg, shift_reg, temp_reg);
  riscv_emit_sra(result_reg, value_reg, temp_reg);

  *ptr_ref = translation_ptr;
}

static void riscv_emit_arm_cpsr_c_load(u8 **ptr_ref, riscv_reg_number rd)
{
  riscv_emit_live_nzcv_bit_load(ptr_ref, rd, 1u, 1u);
}

static bool riscv_emit_arm_data_proc_operand2_with_carry(u8 **ptr_ref,
                                                         riscv_jit_block_meta *meta,
                                                         u32 opcode,
                                                         u32 pc)
{
  u32 imm_op = (opcode >> 25) & 1u;
  u32 rm = opcode & 0xfu;
  u32 rs = (opcode >> 8) & 0xfu;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if (imm_op)
  {
    riscv_emit_li(&ptr, riscv_reg_t1, riscv_arm_expand_imm(opcode));

    if ((opcode >> 8) & 0xfu)
    {
      translation_ptr = ptr;
      riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 31);
      ptr = translation_ptr;
    }
    else
    {
      riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
    }

    *ptr_ref = ptr;
    return true;
  }

  if ((opcode >> 4) & 1u)
  {
    u8 *carry_done;

    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, meta, rm, pc + 12u);

    if (rs == REG_PC)
    {
      u32 shift_amount = (pc + 8u) & 0xffu;

      if (!shift_amount)
        riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
      translation_ptr = ptr;
      riscv_emit_arm_data_proc_const_reg_shift(&translation_ptr, shift_type,
                                               shift_amount, true);
      *ptr_ref = translation_ptr;
      return true;
    }

    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t4, meta, rs, pc + 8u);
    riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
    translation_ptr = ptr;
    riscv_emit_andi(riscv_reg_t4, riscv_reg_t4, 0xff);

    switch (shift_type)
    {
      case 0:
        riscv_emit_reg_lsl_with_carry(&translation_ptr, riscv_reg_t1,
                                      riscv_reg_t1, riscv_reg_t4,
                                      riscv_reg_t3, riscv_reg_t5);
        break;
      case 1:
        riscv_emit_reg_lsr_with_carry(&translation_ptr, riscv_reg_t1,
                                      riscv_reg_t1, riscv_reg_t4,
                                      riscv_reg_t3, riscv_reg_t5);
        break;
      case 2:
        riscv_emit_reg_asr_with_carry(&translation_ptr, riscv_reg_t1,
                                      riscv_reg_t1, riscv_reg_t4,
                                      riscv_reg_t3, riscv_reg_t5);
        break;
      default:
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t4);
        riscv_emit_srl(riscv_reg_t2, riscv_reg_t1, riscv_reg_t4);
        riscv_emit_sll(riscv_reg_t1, riscv_reg_t1, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t2);
        riscv_emit_branch_with_source(&translation_ptr, &carry_done, 0x0,
                                      riscv_reg_t4, riscv_reg_zero, 0);
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 31);
        riscv_patch_local_branch(carry_done, translation_ptr);
        break;
    }

    *ptr_ref = translation_ptr;
    return true;
  }

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, meta, rm, pc + 8u);
  translation_ptr = ptr;

  switch (shift_type)
  {
    case 0:
      if (shift)
      {
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 32u - shift);
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, shift);
      }
      else
      {
        ptr = translation_ptr;
        riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
        translation_ptr = ptr;
      }
      break;
    case 1:
      if (shift)
      {
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, shift - 1u);
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        riscv_emit_srli(riscv_reg_t1, riscv_reg_t1, shift);
      }
      else
      {
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 31);
        riscv_emit_add(riscv_reg_t1, riscv_reg_zero, riscv_reg_zero);
      }
      break;
    case 2:
      if (shift)
      {
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, shift - 1u);
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        riscv_emit_srai(riscv_reg_t1, riscv_reg_t1, shift);
      }
      else
      {
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 31);
        riscv_emit_srai(riscv_reg_t1, riscv_reg_t1, 31);
      }
      break;
    default:
      if (shift)
      {
        riscv_emit_srli(riscv_reg_t4, riscv_reg_t1, shift);
        riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, 32u - shift);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, 31);
      }
      else
      {
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t1, 1);
        ptr = translation_ptr;
        riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t4);
        translation_ptr = ptr;
        riscv_emit_srli(riscv_reg_t1, riscv_reg_t1, 1);
        riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 31);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
      }
      break;
  }

  *ptr_ref = translation_ptr;
  return true;
}

static void riscv_emit_arm_cpsr_store_nzcv(u8 **ptr_ref,
                                           riscv_reg_number result_reg)
{
  riscv_emit_live_nzcv_update_begin(ptr_ref, 0x0fu, false);
  riscv_emit_live_nzcv_or_result_nz(ptr_ref, 0x0cu, result_reg);
  riscv_emit_live_nzcv_or_carry(ptr_ref, riscv_reg_t3);
  riscv_emit_live_nzcv_or_overflow(ptr_ref, riscv_reg_t4);
}

static void riscv_emit_arm_cpsr_store_nzc_preserve_v_ex(
  u8 **ptr_ref, riscv_reg_number result_reg, bool carry_is_const, u32 carry)
{
  u8 *translation_ptr;

  riscv_emit_live_nzcv_update_begin(ptr_ref, 0x0eu, true);
  riscv_emit_live_nzcv_or_result_nz(ptr_ref, 0x0cu, result_reg);
  if (carry_is_const)
  {
    if (carry)
    {
      translation_ptr = *ptr_ref;
      riscv_emit_ori(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                     riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 0x02u);
      *ptr_ref = translation_ptr;
    }
  }
  else
  {
    riscv_emit_live_nzcv_or_carry(ptr_ref, riscv_reg_t3);
  }
}

static void riscv_emit_arm_cpsr_store_nzc_preserve_v(
  u8 **ptr_ref, riscv_reg_number result_reg)
{
  riscv_emit_arm_cpsr_store_nzc_preserve_v_ex(ptr_ref, result_reg, false, 0);
}

static void riscv_emit_arm_cpsr_store_nzc_const_c_preserve_v(
  u8 **ptr_ref, riscv_reg_number result_reg, u32 carry)
{
  riscv_emit_arm_cpsr_store_nzc_preserve_v_ex(ptr_ref, result_reg, true,
                                             carry);
}

static void riscv_emit_arm_cpsr_store_selected_nzc_const_c_preserve_v(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg, u32 carry)
{
  u8 *translation_ptr;

  flag_mask &= 0x0eu;
  if (!flag_mask)
    return;

  if (flag_mask == 0x0eu)
  {
    riscv_emit_arm_cpsr_store_nzc_const_c_preserve_v(ptr_ref, result_reg,
                                                    carry);
    return;
  }

  riscv_emit_live_nzcv_update_begin(ptr_ref, flag_mask, true);
  riscv_emit_live_nzcv_or_result_nz(ptr_ref, flag_mask, result_reg);
  if ((flag_mask & 0x02u) && carry)
  {
    translation_ptr = *ptr_ref;
    riscv_emit_ori(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                   riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 0x02u);
    *ptr_ref = translation_ptr;
  }
}

static void riscv_emit_arm_cpsr_store_nz_preserve_cv(
  u8 **ptr_ref, riscv_reg_number result_reg)
{
  riscv_emit_live_nzcv_update_begin(ptr_ref, 0x0cu, true);
  riscv_emit_live_nzcv_or_result_nz(ptr_ref, 0x0cu, result_reg);
}

static void riscv_emit_arm_cpsr_store_zero_selected_nzcv(u8 **ptr_ref,
                                                         u32 flag_mask)
{
  u8 *translation_ptr;

  flag_mask &= 0x0eu;
  if (!flag_mask)
    return;

  riscv_emit_live_nzcv_update_begin(ptr_ref, flag_mask, true);

  if (flag_mask & 0x04u)
  {
    translation_ptr = *ptr_ref;
    riscv_emit_ori(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                   riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 0x04u);
    *ptr_ref = translation_ptr;
  }
  if (flag_mask & 0x02u)
    riscv_emit_live_nzcv_or_carry(ptr_ref, riscv_reg_t3);
}

static void riscv_emit_arm_cpsr_store_selected_nzcv(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg)
{
  flag_mask &= 0x0fu;
  if (!flag_mask)
    return;

  if (result_reg == riscv_reg_zero && !(flag_mask & 0x01u))
  {
    riscv_emit_arm_cpsr_store_zero_selected_nzcv(ptr_ref, flag_mask);
    return;
  }

  if (flag_mask == 0x0fu)
  {
    riscv_emit_arm_cpsr_store_nzcv(ptr_ref, result_reg);
    return;
  }

  if (flag_mask == 0x0eu)
  {
    riscv_emit_arm_cpsr_store_nzc_preserve_v(ptr_ref, result_reg);
    return;
  }

  if (flag_mask == 0x0cu)
  {
    riscv_emit_arm_cpsr_store_nz_preserve_cv(ptr_ref, result_reg);
    return;
  }

  riscv_emit_live_nzcv_update_begin(ptr_ref, flag_mask, true);
  riscv_emit_live_nzcv_or_result_nz(ptr_ref, flag_mask, result_reg);
  if (flag_mask & 0x02u)
    riscv_emit_live_nzcv_or_carry(ptr_ref, riscv_reg_t3);
  if (flag_mask & 0x01u)
    riscv_emit_live_nzcv_or_overflow(ptr_ref, riscv_reg_t4);
}

static void riscv_emit_arm_cpsr_store_arithmetic_selected_nzcv(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg)
{
  flag_mask &= 0x0fu;
  if (!flag_mask)
    return;

  if (flag_mask == 0x0fu)
  {
    riscv_emit_arm_cpsr_store_nzcv(ptr_ref, result_reg);
    return;
  }

  riscv_emit_live_nzcv_update_begin(ptr_ref, flag_mask, false);
  riscv_emit_live_nzcv_or_result_nz(ptr_ref, flag_mask, result_reg);
  if (flag_mask & 0x02u)
    riscv_emit_live_nzcv_or_carry(ptr_ref, riscv_reg_t3);
  if (flag_mask & 0x01u)
    riscv_emit_live_nzcv_or_overflow(ptr_ref, riscv_reg_t4);
}

static void riscv_emit_arm_cpsr_store_const_selected_nzcv(
  u8 **ptr_ref, u32 flag_mask, u32 flags, bool clobber_dead_flags)
{
  u8 *translation_ptr;

  flag_mask &= 0x0fu;
  flags &= flag_mask;
  if (!flag_mask)
    return;

  riscv_emit_live_nzcv_update_begin(ptr_ref, flag_mask, !clobber_dead_flags);
  if (flags)
  {
    translation_ptr = *ptr_ref;
    riscv_emit_ori(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                   riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], flags);
    *ptr_ref = translation_ptr;
  }
}

static void riscv_emit_arm_cpsr_store_addsub_zero_test(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg, bool subtract)
{
  u8 *translation_ptr;

  flag_mask &= 0x0fu;
  if (!flag_mask)
    return;

  riscv_emit_live_nzcv_update_begin(ptr_ref, flag_mask, true);
  riscv_emit_live_nzcv_or_result_nz(ptr_ref, flag_mask, result_reg);
  if ((flag_mask & 0x02u) && subtract)
  {
    translation_ptr = *ptr_ref;
    riscv_emit_ori(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                   riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 0x02u);
    *ptr_ref = translation_ptr;
  }
}

static void riscv_emit_arm_cpsr_store_addsub_zero_test_dead_flags(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg, bool subtract)
{
  u8 *translation_ptr;

  flag_mask &= 0x0fu;
  if (!flag_mask)
    return;

  riscv_emit_live_nzcv_update_begin(ptr_ref, flag_mask, false);
  riscv_emit_live_nzcv_or_result_nz(ptr_ref, flag_mask, result_reg);
  if ((flag_mask & 0x02u) && subtract)
  {
    translation_ptr = *ptr_ref;
    riscv_emit_ori(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                   riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT], 0x02u);
    *ptr_ref = translation_ptr;
  }
}

static void riscv_emit_arm_cpsr_store_long_nzcv(u8 **ptr_ref)
{
  u8 *translation_ptr;

  riscv_emit_live_nzcv_update_begin(ptr_ref, 0x0cu, true);

  translation_ptr = *ptr_ref;
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t3, 31);
  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 3);
  riscv_emit_or(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                riscv_reg_t4);
  riscv_emit_or(riscv_reg_t5, riscv_reg_t2, riscv_reg_t3);
  riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t5, 1);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 2);
  riscv_emit_or(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                riscv_reg_t5);
  *ptr_ref = translation_ptr;
}

static void riscv_emit_arm_cpsr_store_long_selected_nz(
  u8 **ptr_ref, u32 flag_mask, bool clobber_dead_flags)
{
  u8 *translation_ptr;

  flag_mask &= 0x0cu;
  if (!flag_mask)
    return;

  if (!clobber_dead_flags && flag_mask == 0x0cu)
  {
    riscv_emit_arm_cpsr_store_long_nzcv(ptr_ref);
    return;
  }

  riscv_emit_live_nzcv_update_begin(ptr_ref, flag_mask, !clobber_dead_flags);
  translation_ptr = *ptr_ref;

  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, riscv_reg_t3, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 3);
    riscv_emit_or(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_or(riscv_reg_t5, riscv_reg_t2, riscv_reg_t3);
    riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t5, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 2);
    riscv_emit_or(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
                  riscv_reg_t5);
  }
  *ptr_ref = translation_ptr;
}

static bool riscv_emit_arm_data_proc_immediate_result(u8 **ptr_ref,
                                                      u32 op,
                                                      u32 immediate)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;
  u32 not_immediate;
  u32 neg_immediate;

  switch (op)
  {
    case 0x0:
      if (!riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_andi(riscv_reg_t2, riscv_reg_t0, (s32)immediate);
      ptr = translation_ptr;
      break;
    case 0x1:
      if (!riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t2, riscv_reg_t0, (s32)immediate);
      ptr = translation_ptr;
      break;
    case 0x2:
      neg_immediate = 0u - immediate;
      if (!riscv_i12_fits(neg_immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)neg_immediate);
      ptr = translation_ptr;
      break;
    case 0x3:
      if (immediate != 0)
        return false;
      translation_ptr = ptr;
      riscv_emit_sub(riscv_reg_t2, riscv_reg_zero, riscv_reg_t0);
      ptr = translation_ptr;
      break;
    case 0x4:
      if (!riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)immediate);
      ptr = translation_ptr;
      break;
    case 0xc:
      if (!riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_ori(riscv_reg_t2, riscv_reg_t0, (s32)immediate);
      ptr = translation_ptr;
      break;
    case 0xd:
      riscv_emit_li(&ptr, riscv_reg_t2, immediate);
      break;
    case 0xe:
      not_immediate = ~immediate;
      if (!riscv_i12_fits(not_immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_andi(riscv_reg_t2, riscv_reg_t0, (s32)not_immediate);
      ptr = translation_ptr;
      break;
    case 0xf:
      riscv_emit_li(&ptr, riscv_reg_t2, ~immediate);
      break;
    default:
      return false;
  }

  *ptr_ref = ptr;
  return true;
}

static bool riscv_arm_data_proc_immediate_zero_result(u32 op, u32 immediate)
{
  switch (op)
  {
    case 0x0:
    case 0xd:
      return immediate == 0;
    case 0xe:
    case 0xf:
      return immediate == 0xffffffffu;
    default:
      return false;
  }
}

static bool riscv_arm_data_proc_pc_immediate_result(u32 op,
                                                    u32 pc_value,
                                                    u32 immediate,
                                                    u32 *result)
{
  switch (op)
  {
    case 0x0:
      *result = pc_value & immediate;
      return true;
    case 0x1:
      *result = pc_value ^ immediate;
      return true;
    case 0x2:
      *result = pc_value - immediate;
      return true;
    case 0x3:
      *result = immediate - pc_value;
      return true;
    case 0x4:
      *result = pc_value + immediate;
      return true;
    case 0xc:
      *result = pc_value | immediate;
      return true;
    case 0xe:
      *result = pc_value & ~immediate;
      return true;
    default:
      return false;
  }
}

static void riscv_emit_arm_immediate_shifter_carry(u8 **ptr_ref,
                                                   u32 opcode,
                                                   u32 immediate,
                                                   u32 flag_mask)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if (!(flag_mask & 0x02u))
    return;

  if ((opcode >> 8) & 0xfu)
  {
    translation_ptr = ptr;
    riscv_emit_addi(riscv_reg_t3, riscv_reg_zero,
                    (immediate >> 31) & 1u);
    ptr = translation_ptr;
  }
  else
  {
    riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
  }

  *ptr_ref = ptr;
}

static bool riscv_emit_arm_data_proc_logical_immediate_result(u8 **ptr_ref,
                                                              u32 op,
                                                              u32 opcode,
                                                              u32 immediate,
                                                              u32 flag_mask)
{
  if (!riscv_emit_arm_data_proc_immediate_result(ptr_ref, op, immediate))
    return false;

  riscv_emit_arm_immediate_shifter_carry(ptr_ref, opcode, immediate,
                                         flag_mask);
  return true;
}

/* Overflow flag code consumes only bit 31 of these expressions. */
static riscv_reg_number riscv_emit_arm_overflow_xor_const_sign(
  u8 **ptr_ref, riscv_reg_number rd, riscv_reg_number rs, u32 value)
{
  u8 *translation_ptr;

  if (!(value & 0x80000000u))
    return rs;

  translation_ptr = *ptr_ref;
  riscv_emit_xori(rd, rs, -1);
  *ptr_ref = translation_ptr;
  return rd;
}

static riscv_reg_number riscv_emit_arm_overflow_xnor_const_sign(
  u8 **ptr_ref, riscv_reg_number rd, riscv_reg_number rs, u32 value)
{
  u8 *translation_ptr;

  if (value & 0x80000000u)
    return rs;

  translation_ptr = *ptr_ref;
  riscv_emit_xori(rd, rs, -1);
  *ptr_ref = translation_ptr;
  return rd;
}

static bool riscv_emit_arm_data_proc_arithmetic_immediate_flags(
  u8 **ptr_ref, u32 op, u32 immediate, u32 flag_mask)
{
  bool need_c = (flag_mask & 0x02u) != 0;
  bool need_v = (flag_mask & 0x01u) != 0;
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;
  u32 neg_immediate;
  u32 not_immediate;
  u32 next_immediate;
  riscv_reg_number overflow_lhs = riscv_reg_t4;
  riscv_reg_number overflow_rhs = riscv_reg_t6;

  if (immediate == 0 && (op == 0x2 || op == 0x4))
  {
    translation_ptr = ptr;
    riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, 0);
    if (need_c)
    {
      if (op == 0x2)
        riscv_emit_addi(riscv_reg_t3, riscv_reg_zero, 1);
      else
        riscv_emit_add(riscv_reg_t3, riscv_reg_zero, riscv_reg_zero);
    }
    if (need_v)
      riscv_emit_add(riscv_reg_t4, riscv_reg_zero, riscv_reg_zero);
    ptr = translation_ptr;
    *ptr_ref = ptr;
    return true;
  }

  switch (op)
  {
    case 0x2:
      neg_immediate = 0u - immediate;
      if (!riscv_i12_fits(neg_immediate))
        return false;
      if ((need_c || need_v) && !riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)neg_immediate);
      if (need_c)
      {
        riscv_emit_sltiu(riscv_reg_t3, riscv_reg_t0, (s32)immediate);
        riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
      }
      if (need_v)
      {
        overflow_lhs =
          riscv_emit_arm_overflow_xor_const_sign(&translation_ptr,
                                                 riscv_reg_t4,
                                                 riscv_reg_t0,
                                                 immediate);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
        overflow_rhs = riscv_reg_t6;
      }
      break;

    case 0x3:
      if (immediate && !riscv_i12_fits(immediate))
        return false;
      if (need_c)
      {
        if (immediate == 0xffffffffu)
        {
          next_immediate = 0;
        }
        else
        {
          next_immediate = immediate + 1u;
          if (!riscv_i12_fits(next_immediate))
            return false;
        }
      }
      translation_ptr = ptr;
      riscv_emit_sub(riscv_reg_t2, riscv_reg_zero, riscv_reg_t0);
      if (immediate)
        riscv_emit_addi(riscv_reg_t2, riscv_reg_t2, (s32)immediate);
      if (need_c)
      {
        if (immediate == 0xffffffffu)
          riscv_emit_addi(riscv_reg_t3, riscv_reg_zero, 1);
        else
          riscv_emit_sltiu(riscv_reg_t3, riscv_reg_t0,
                           (s32)next_immediate);
      }
      if (need_v)
      {
        overflow_lhs =
          riscv_emit_arm_overflow_xor_const_sign(&translation_ptr,
                                                 riscv_reg_t4,
                                                 riscv_reg_t0,
                                                 immediate);
        overflow_rhs =
          riscv_emit_arm_overflow_xor_const_sign(&translation_ptr,
                                                 riscv_reg_t6,
                                                 riscv_reg_t2,
                                                 immediate);
      }
      break;

    case 0x4:
      if (!riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)immediate);
      if (need_c)
        riscv_emit_sltu(riscv_reg_t3, riscv_reg_t2, riscv_reg_t0);
      if (need_v)
      {
        overflow_lhs =
          riscv_emit_arm_overflow_xnor_const_sign(&translation_ptr,
                                                  riscv_reg_t4,
                                                  riscv_reg_t0,
                                                  immediate);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
        overflow_rhs = riscv_reg_t6;
      }
      break;

    case 0x5:
      if (!riscv_i12_fits(immediate))
        return false;
      riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t5, riscv_reg_t0, (s32)immediate);
      riscv_emit_add(riscv_reg_t2, riscv_reg_t5, riscv_reg_t3);
      if (need_c)
      {
        riscv_emit_sltu(riscv_reg_t4, riscv_reg_t5, riscv_reg_t0);
        riscv_emit_sltu(riscv_reg_t6, riscv_reg_t2, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t4, riscv_reg_t6);
      }
      if (need_v)
      {
        overflow_lhs =
          riscv_emit_arm_overflow_xnor_const_sign(&translation_ptr,
                                                  riscv_reg_t4,
                                                  riscv_reg_t0,
                                                  immediate);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
        overflow_rhs = riscv_reg_t6;
      }
      break;

    case 0x6:
      not_immediate = ~immediate;
      if (!riscv_i12_fits(not_immediate))
        return false;
      if ((need_c || need_v) && !riscv_i12_fits(immediate))
        return false;
      riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)not_immediate);
      riscv_emit_add(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
      if (need_c)
      {
        if (immediate == 0xffffffffu)
        {
          riscv_emit_xori(riscv_reg_t4, riscv_reg_t0, -1);
          riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t4, 1);
          riscv_emit_and(riscv_reg_t3, riscv_reg_t3, riscv_reg_t4);
        }
        else
        {
          riscv_emit_xori(riscv_reg_t4, riscv_reg_t3, 1);
          riscv_emit_addi(riscv_reg_t4, riscv_reg_t4, (s32)immediate);
          riscv_emit_sltu(riscv_reg_t3, riscv_reg_t0, riscv_reg_t4);
          riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
        }
      }
      if (need_v)
      {
        overflow_lhs =
          riscv_emit_arm_overflow_xor_const_sign(&translation_ptr,
                                                 riscv_reg_t4,
                                                 riscv_reg_t0,
                                                 immediate);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
        overflow_rhs = riscv_reg_t6;
      }
      break;

    case 0x7:
      if (!riscv_i12_fits(immediate))
        return false;
      riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t2, riscv_reg_t0, -1);
      if (immediate)
        riscv_emit_addi(riscv_reg_t2, riscv_reg_t2, (s32)immediate);
      riscv_emit_add(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
      if (need_c)
      {
        if (immediate == 0xffffffffu)
        {
          riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t0,
                           (s32)immediate);
          riscv_emit_or(riscv_reg_t3, riscv_reg_t3, riscv_reg_t4);
        }
        else
        {
          riscv_emit_addi(riscv_reg_t3, riscv_reg_t3,
                          (s32)immediate);
          riscv_emit_sltu(riscv_reg_t3, riscv_reg_t0,
                          riscv_reg_t3);
        }
      }
      if (need_v)
      {
        overflow_lhs =
          riscv_emit_arm_overflow_xor_const_sign(&translation_ptr,
                                                 riscv_reg_t4,
                                                 riscv_reg_t0,
                                                 immediate);
        overflow_rhs =
          riscv_emit_arm_overflow_xor_const_sign(&translation_ptr,
                                                 riscv_reg_t6,
                                                 riscv_reg_t2,
                                                 immediate);
      }
      break;

    default:
      return false;
  }

  if (need_v)
  {
    riscv_emit_and(riscv_reg_t4, overflow_lhs, overflow_rhs);
    riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 31);
  }

  ptr = translation_ptr;
  *ptr_ref = ptr;
  return true;
}

static bool riscv_emit_arm_data_test_immediate_result(u8 **ptr_ref,
                                                      u32 op,
                                                      u32 immediate)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;
  u32 neg_immediate;

  switch (op)
  {
    case 0x8:
      if (!riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_andi(riscv_reg_t2, riscv_reg_t0, (s32)immediate);
      ptr = translation_ptr;
      break;
    case 0x9:
      if (!riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t2, riscv_reg_t0, (s32)immediate);
      ptr = translation_ptr;
      break;
    case 0xa:
      neg_immediate = 0u - immediate;
      if (!riscv_i12_fits(neg_immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)neg_immediate);
      ptr = translation_ptr;
      break;
    case 0xb:
      if (!riscv_i12_fits(immediate))
        return false;
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)immediate);
      ptr = translation_ptr;
      break;
    default:
      return false;
  }

  *ptr_ref = ptr;
  return true;
}

static bool riscv_emit_arm_data_test_logical_immediate_result(u8 **ptr_ref,
                                                              u32 op,
                                                              u32 opcode,
                                                              u32 immediate,
                                                              u32 flag_mask)
{
  if (!riscv_emit_arm_data_test_immediate_result(ptr_ref, op, immediate))
    return false;

  riscv_emit_arm_immediate_shifter_carry(ptr_ref, opcode, immediate,
                                         flag_mask);
  return true;
}

static bool riscv_emit_arm_data_test_arithmetic_immediate_flags(
  u8 **ptr_ref, u32 op, u32 immediate, u32 flag_mask)
{
  switch (op)
  {
    case 0xa:
      return riscv_emit_arm_data_proc_arithmetic_immediate_flags(
        ptr_ref, 0x2, immediate, flag_mask);
    case 0xb:
      return riscv_emit_arm_data_proc_arithmetic_immediate_flags(
        ptr_ref, 0x4, immediate, flag_mask);
    default:
      return false;
  }
}

static bool riscv_emit_native_arm_data_proc_with_pc_ex2(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted,
  bool clobber_dead_arithmetic_flags,
  u32 known_flag_mask,
  u32 known_flags)
{
  u32 condition = opcode >> 28;
  u32 op = (opcode >> 21) & 0xfu;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 imm_op = (opcode >> 25) & 1u;
  bool arithmetic_op =
    op == 0x2 || op == 0x3 || op == 0x4 ||
    op == 0x5 || op == 0x6 || op == 0x7;
  bool arithmetic_flags = set_flags && arithmetic_op;
  bool logical_flags = set_flags &&
    (op == 0x0 || op == 0x1 || op == 0xc ||
     op == 0xd || op == 0xe || op == 0xf);
  u32 live_flag_mask = set_flags ? (flag_status & 0x0fu) : 0;
  u32 generated_flag_mask = arithmetic_flags ? live_flag_mask :
    (logical_flags ? (live_flag_mask & 0x0eu) : 0);
  bool live_flags;
  u8 *ptr = *translation_ptr_ref;
  bool result_emitted = false;
  bool addsub_zero_flags = false;
  bool logical_const_c = false;
  u32 logical_carry = 0;
  riscv_reg_number result_reg = riscv_reg_t2;
  bool writes_pc;
  bool known_generated_flags;
  u32 immediate = 0;

  if (cycles_emitted)
    *cycles_emitted = false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe)
    return false;

  if (set_flags && !arithmetic_flags && !logical_flags)
    return false;

  if (op != 0x0 && op != 0x1 && op != 0x2 &&
      op != 0x3 && op != 0x4 && op != 0x5 &&
      op != 0x6 && op != 0x7 && op != 0xc &&
      op != 0xd && op != 0xe && op != 0xf)
  {
    return false;
  }

  if (riscv_arm_data_proc_is_noop(opcode))
  {
    if (emit_cycles)
    {
      riscv_emit_adjust_cycles(&ptr, cycles);
      if (cycles_emitted)
        *cycles_emitted = true;
    }

    *translation_ptr_ref = ptr;
    riscv_native_data_proc_insns++;
    return true;
  }

#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  if (!riscv_runtime_perf_disable_mapped_alu_fastpath &&
      riscv_emit_arm_data_proc_mapped_reg_alu(&ptr, opcode))
#elif !defined(RISCV_RUNTIME_DISABLE_MAPPED_ALU_FASTPATH)
  if (riscv_emit_arm_data_proc_mapped_reg_alu(&ptr, opcode))
#endif
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH) || \
    !defined(RISCV_RUNTIME_DISABLE_MAPPED_ALU_FASTPATH)
  {
    if (emit_cycles)
    {
      riscv_emit_adjust_cycles(&ptr, cycles);
      if (cycles_emitted)
        *cycles_emitted = true;
    }

    *translation_ptr_ref = ptr;
    riscv_native_data_proc_insns++;
    return true;
  }
#endif

  if (logical_flags &&
      (generated_flag_mask & 0x02u) &&
      riscv_arm_operand2_preserves_carry(opcode))
  {
    generated_flag_mask &= ~0x02u;
  }
  live_flags = generated_flag_mask != 0;
  known_flag_mask &= 0x0fu;
  known_flags &= known_flag_mask;
  known_generated_flags = live_flags &&
    ((known_flag_mask & generated_flag_mask) == generated_flag_mask);

  if (imm_op)
  {
    immediate = riscv_arm_expand_imm(opcode);
    if (logical_flags && (generated_flag_mask & 0x0eu) &&
        ((opcode >> 8) & 0xfu))
    {
      logical_const_c = true;
      logical_carry = (immediate >> 31) & 1u;
    }

    if (riscv_arm_data_proc_immediate_zero_result(op, immediate) &&
        (!live_flags || logical_flags))
    {
      if (live_flags)
        riscv_emit_arm_immediate_shifter_carry(
          &ptr, opcode, immediate,
          logical_const_c ? (generated_flag_mask & ~0x02u) :
                            generated_flag_mask);
      result_emitted = true;
      result_reg = riscv_reg_zero;
    }

    if (!result_emitted && rn == REG_PC && !set_flags)
    {
      u32 const_result;

      if (riscv_arm_data_proc_pc_immediate_result(
            op, pc + 8u, immediate, &const_result))
      {
        if (const_result)
          riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t2, const_result);
        else
          result_reg = riscv_reg_zero;
        result_emitted = true;
      }
    }
  }

  if (!result_emitted && op != 0xd && op != 0xf)
    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, rn, pc + 8u);

  /* A PC-relative ADD/SUB may already have been constant-folded above.
   * Do not replace that result with t0: t0 is intentionally not initialized
   * on the folded path and commonly still contains the previous helper
   * target (for example OpenBIOS's push; ADR lr; bx sequence). */
  if (imm_op && !result_emitted && immediate == 0 &&
      (op == 0x2 || op == 0x4))
  {
    result_emitted = true;
    result_reg = riscv_reg_t0;
    addsub_zero_flags = live_flags;
  }

  if (imm_op && !result_emitted)
  {
    if (logical_flags)
    {
      result_emitted =
        riscv_emit_arm_data_proc_logical_immediate_result(
          &ptr, op, opcode, immediate,
          logical_const_c ? (generated_flag_mask & ~0x02u) :
                            generated_flag_mask);
    }
    else if (arithmetic_op)
    {
      result_emitted =
        riscv_emit_arm_data_proc_arithmetic_immediate_flags(
          &ptr, op, immediate, generated_flag_mask);
      if (!result_emitted && !(generated_flag_mask & 0x03u))
      {
        result_emitted =
          riscv_emit_arm_data_proc_immediate_result(&ptr, op, immediate);
      }
    }
    else if (!(generated_flag_mask & 0x03u))
    {
      result_emitted =
        riscv_emit_arm_data_proc_immediate_result(&ptr, op, immediate);
    }
  }

  if (!result_emitted && !known_generated_flags && logical_flags &&
      (generated_flag_mask & 0x02u) && !logical_const_c)
  {
    if (!riscv_emit_arm_data_proc_operand2_with_carry(&ptr, meta, opcode, pc))
      return false;
  }
  else if (!result_emitted &&
           !riscv_emit_arm_data_proc_operand2(&ptr, meta, opcode, pc))
  {
    return false;
  }

  if (!result_emitted && (op == 0x5 || op == 0x6 || op == 0x7))
  {
    u8 *translation_ptr;

    riscv_emit_arm_reg_load(&ptr, riscv_reg_t3, REG_CPSR);
    translation_ptr = ptr;
    riscv_emit_srli(riscv_reg_t3, riscv_reg_t3, 29);
    riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
    ptr = translation_ptr;
  }

  if (!result_emitted)
  {
    u8 *translation_ptr = ptr;

    switch (op)
    {
      case 0x0:
        riscv_emit_and(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 0x1:
        riscv_emit_xor(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 0x2:
        riscv_emit_sub(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 0x3:
        riscv_emit_sub(riscv_reg_t2, riscv_reg_t1, riscv_reg_t0);
        break;
      case 0x4:
        riscv_emit_add(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 0x5:
        riscv_emit_add(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_add(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
        break;
      case 0x6:
        riscv_emit_sub(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_add(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
        riscv_emit_addi(riscv_reg_t2, riscv_reg_t2, -1);
        break;
      case 0x7:
        riscv_emit_sub(riscv_reg_t2, riscv_reg_t1, riscv_reg_t0);
        riscv_emit_add(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
        riscv_emit_addi(riscv_reg_t2, riscv_reg_t2, -1);
        break;
      case 0xc:
        riscv_emit_or(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 0xd:
        result_reg = riscv_reg_t1;
        break;
      case 0xe:
        riscv_emit_xori(riscv_reg_t1, riscv_reg_t1, -1);
        riscv_emit_and(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 0xf:
        riscv_emit_xori(riscv_reg_t2, riscv_reg_t1, -1);
        break;
      default:
        return false;
    }

    ptr = translation_ptr;
  }

  if (!result_emitted && arithmetic_flags && live_flags &&
      !known_generated_flags)
  {
    u8 *translation_ptr = ptr;
    bool need_c = (generated_flag_mask & 0x02u) != 0;
    bool need_v = (generated_flag_mask & 0x01u) != 0;

    if (op == 0x2)
    {
      if (need_c)
      {
        riscv_emit_sltu(riscv_reg_t3, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
      }
      if (need_v)
      {
        riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
      }
    }
    else if (op == 0x3)
    {
      if (need_c)
      {
        riscv_emit_sltu(riscv_reg_t3, riscv_reg_t1, riscv_reg_t0);
        riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
      }
      if (need_v)
      {
        riscv_emit_xor(riscv_reg_t4, riscv_reg_t1, riscv_reg_t0);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t1, riscv_reg_t2);
      }
    }
    else if (op == 0x4)
    {
      if (need_c)
        riscv_emit_sltu(riscv_reg_t3, riscv_reg_t2, riscv_reg_t0);
      if (need_v)
      {
        riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
      }
    }
    else if (op == 0x5)
    {
      if (need_c)
      {
        riscv_emit_add(riscv_reg_t5, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_sltu(riscv_reg_t4, riscv_reg_t5, riscv_reg_t0);
        riscv_emit_sltu(riscv_reg_t6, riscv_reg_t2, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t4, riscv_reg_t6);
      }
      if (need_v)
      {
        riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
      }
    }
    else if (op == 0x6)
    {
      if (need_c)
      {
        riscv_emit_sltu(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, 1);
        riscv_emit_sltu(riscv_reg_t5, riscv_reg_t1, riscv_reg_t0);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t4, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t3);
        riscv_emit_xor(riscv_reg_t3, riscv_reg_t5, riscv_reg_t6);
      }
      if (need_v)
      {
        riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
      }
    }
    else
    {
      if (need_c)
      {
        riscv_emit_add(riscv_reg_t5, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_sltu(riscv_reg_t6, riscv_reg_t5, riscv_reg_t1);
        riscv_emit_sltu(riscv_reg_t3, riscv_reg_t0, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t3, riscv_reg_t6);
      }
      if (need_v)
      {
        riscv_emit_xor(riscv_reg_t4, riscv_reg_t1, riscv_reg_t0);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t1, riscv_reg_t2);
      }
    }

    if (need_v)
    {
      riscv_emit_and(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
      riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 31);
    }
    ptr = translation_ptr;
  }

  writes_pc = rd == REG_PC;
  if (result_reg != riscv_reg_t0 || rd != rn || writes_pc)
    riscv_emit_arm_reg_store(&ptr, rd, result_reg);
  if (writes_pc)
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  if (known_generated_flags)
  {
    riscv_emit_arm_cpsr_store_const_selected_nzcv(
      &ptr, generated_flag_mask, known_flags,
      arithmetic_flags ? clobber_dead_arithmetic_flags : false);
  }
  else if (addsub_zero_flags)
  {
    if (clobber_dead_arithmetic_flags)
      riscv_emit_arm_cpsr_store_addsub_zero_test_dead_flags(
        &ptr, generated_flag_mask, result_reg, op == 0x2);
    else
      riscv_emit_arm_cpsr_store_addsub_zero_test(
        &ptr, generated_flag_mask, result_reg, op == 0x2);
  }
  else if (arithmetic_flags && live_flags)
  {
    if (clobber_dead_arithmetic_flags)
      riscv_emit_arm_cpsr_store_arithmetic_selected_nzcv(
        &ptr, generated_flag_mask, result_reg);
    else
    {
      if (clobber_dead_arithmetic_flags)
        riscv_emit_arm_cpsr_store_arithmetic_selected_nzcv(
          &ptr, generated_flag_mask, result_reg);
      else
        riscv_emit_arm_cpsr_store_selected_nzcv(
          &ptr, generated_flag_mask, result_reg);
    }
  }
  else if (logical_flags && live_flags)
  {
    if (logical_const_c)
    {
        riscv_emit_arm_cpsr_store_selected_nzc_const_c_preserve_v(
          &ptr, generated_flag_mask, result_reg, logical_carry);
    }
    else
      riscv_emit_arm_cpsr_store_selected_nzcv(
        &ptr, generated_flag_mask, result_reg);
  }
  if (writes_pc && set_flags)
  {
    riscv_emit_stateful_c_call_stack(
      &ptr, RISCV_STACK_HELPER_EXECUTE_SPSR_RESTORE, false);
    if (emit_cycles || writes_pc)
    {
      riscv_emit_adjust_cycles(&ptr, cycles);
      if (cycles_emitted)
        *cycles_emitted = true;
    }
    riscv_emit_terminal_helper_call_no_flush(&ptr, meta);

    *translation_ptr_ref = ptr;
    riscv_native_data_proc_insns++;
    return true;
  }
  if (emit_cycles || writes_pc)
  {
    riscv_emit_adjust_cycles(&ptr, cycles);
    if (cycles_emitted)
      *cycles_emitted = true;
  }
  if (writes_pc)
  {
    /* scan_block() may retain instructions after a conditional PC writer, or
     * after an unconditional writer that a forward edge jumps over.  The
     * taken path must leave at this instruction instead of falling through
     * until the block finalizer happens to observe PC_WRITTEN. */
    riscv_emit_terminal_helper_call(&ptr, meta);
  }

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_arm_data_proc_with_pc_ex(u8 **translation_ptr_ref,
                                                riscv_jit_block_meta *meta,
                                                u32 opcode,
                                                u32 pc,
                                                u32 cycles,
                                                u32 flag_status,
                                                bool emit_cycles,
                                                bool *cycles_emitted)
{
  return riscv_emit_native_arm_data_proc_with_pc_ex2(
    translation_ptr_ref, meta, opcode, pc, cycles, flag_status, emit_cycles,
    cycles_emitted, false, 0, 0);
}

bool riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted)
{
  return riscv_emit_native_arm_data_proc_with_pc_ex2(
    translation_ptr_ref, meta, opcode, pc, cycles, flag_status, emit_cycles,
    cycles_emitted, true, 0, 0);
}

bool riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags_known(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted,
  u32 known_flag_mask,
  u32 known_flags)
{
  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_DATA_PROC)
    return false;

  return riscv_emit_native_arm_data_proc_with_pc_ex2(
    translation_ptr_ref, meta, opcode, pc, cycles, flag_status, emit_cycles,
    cycles_emitted, true, known_flag_mask, known_flags);
}

bool riscv_emit_native_arm_data_proc_with_pc(u8 **translation_ptr_ref,
                                             riscv_jit_block_meta *meta,
                                             u32 opcode,
                                             u32 pc,
                                             u32 cycles)
{
  return riscv_emit_native_arm_data_proc_with_pc_ex(translation_ptr_ref, meta,
                                                   opcode, pc, cycles,
                                                   0x0fu, true, NULL);
}

bool riscv_emit_native_arm_data_proc(u8 **translation_ptr_ref,
                                     riscv_jit_block_meta *meta,
                                     u32 opcode,
                                     u32 cycles)
{
  return riscv_emit_native_arm_data_proc_with_pc(translation_ptr_ref, meta,
                                                opcode, 0, cycles);
}

static bool riscv_emit_native_arm_data_proc_test_with_pc_ex2(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted,
  bool clobber_dead_arithmetic_flags,
  u32 known_flag_mask,
  u32 known_flags)
{
  u32 condition = opcode >> 28;
  u32 op = (opcode >> 21) & 0xfu;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 imm_op = (opcode >> 25) & 1u;
  u32 logical_test = (op == 0x8 || op == 0x9);
  u32 live_flag_mask = set_flags ? (flag_status & 0x0fu) : 0;
  u32 generated_flag_mask = logical_test ?
    (live_flag_mask & 0x0eu) : live_flag_mask;
  bool live_flags;
  u8 *ptr = *translation_ptr_ref;
  bool result_emitted = false;
  bool flags_stored = false;
  bool logical_const_c = false;
  u32 logical_carry = 0;
  bool known_generated_flags;

  if (cycles_emitted)
    *cycles_emitted = false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || !set_flags)
    return false;

  if (!logical_test && op != 0xa && op != 0xb)
    return false;

  if (logical_test &&
      (generated_flag_mask & 0x02u) &&
      riscv_arm_operand2_preserves_carry(opcode))
  {
    generated_flag_mask &= ~0x02u;
  }
  live_flags = generated_flag_mask != 0;
  known_flag_mask &= 0x0fu;
  known_flags &= known_flag_mask;
  known_generated_flags = live_flags &&
    ((known_flag_mask & generated_flag_mask) == generated_flag_mask);

  if (!live_flags)
  {
    if (emit_cycles)
    {
      riscv_emit_adjust_cycles(&ptr, cycles);
      if (cycles_emitted)
        *cycles_emitted = true;
    }
    *translation_ptr_ref = ptr;
    riscv_native_data_proc_insns++;
    return true;
  }

  if (known_generated_flags)
  {
    riscv_emit_arm_cpsr_store_const_selected_nzcv(
      &ptr, generated_flag_mask, known_flags,
      logical_test ? false : clobber_dead_arithmetic_flags);
    if (emit_cycles)
    {
      riscv_emit_adjust_cycles(&ptr, cycles);
      if (cycles_emitted)
        *cycles_emitted = true;
    }
    *translation_ptr_ref = ptr;
    riscv_native_data_proc_insns++;
    return true;
  }

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, rn, pc + 8u);

  if (imm_op)
  {
    u32 immediate = riscv_arm_expand_imm(opcode);

    if (logical_test && (generated_flag_mask & 0x0eu) &&
        ((opcode >> 8) & 0xfu))
    {
      logical_const_c = true;
      logical_carry = (immediate >> 31) & 1u;
    }

    if (logical_test)
    {
      result_emitted =
        riscv_emit_arm_data_test_logical_immediate_result(
          &ptr, op, opcode, immediate,
          logical_const_c ? (generated_flag_mask & ~0x02u) :
                            generated_flag_mask);
    }
    else if (immediate == 0 && (op == 0xa || op == 0xb))
    {
      if (clobber_dead_arithmetic_flags)
        riscv_emit_arm_cpsr_store_addsub_zero_test_dead_flags(
          &ptr, generated_flag_mask, riscv_reg_t0, op == 0xa);
      else
        riscv_emit_arm_cpsr_store_addsub_zero_test(
          &ptr, generated_flag_mask, riscv_reg_t0, op == 0xa);
      result_emitted = true;
      flags_stored = true;
    }
    else if (generated_flag_mask & 0x03u)
    {
      result_emitted =
        riscv_emit_arm_data_test_arithmetic_immediate_flags(
          &ptr, op, immediate, generated_flag_mask);
    }
    else
    {
      result_emitted =
        riscv_emit_arm_data_test_immediate_result(&ptr, op, immediate);
    }
  }

  if (!result_emitted && logical_test && (generated_flag_mask & 0x02u) &&
      !logical_const_c)
  {
    if (!riscv_emit_arm_data_proc_operand2_with_carry(&ptr, meta, opcode, pc))
      return false;
  }
  else if (!result_emitted &&
           !riscv_emit_arm_data_proc_operand2(&ptr, meta, opcode, pc))
  {
    return false;
  }

  if (!result_emitted)
  {
    u8 *translation_ptr = ptr;

    if (op == 0x8)
    {
      riscv_emit_and(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
    }
    else if (op == 0x9)
    {
      riscv_emit_xor(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
    }
    else if (op == 0xa)
    {
      bool need_c = (generated_flag_mask & 0x02u) != 0;
      bool need_v = (generated_flag_mask & 0x01u) != 0;

      riscv_emit_sub(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      if (need_c)
      {
        riscv_emit_sltu(riscv_reg_t3, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
      }
      if (need_v)
      {
        riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
      }
    }
    else
    {
      bool need_c = (generated_flag_mask & 0x02u) != 0;
      bool need_v = (generated_flag_mask & 0x01u) != 0;

      riscv_emit_add(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      if (need_c)
      {
        riscv_emit_sltu(riscv_reg_t3, riscv_reg_t2, riscv_reg_t0);
      }
      if (need_v)
      {
        riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
      }
    }

    if (!logical_test && (generated_flag_mask & 0x01u))
    {
      riscv_emit_and(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
      riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 31);
    }

    ptr = translation_ptr;
  }

  if (!flags_stored)
  {
    if (logical_test)
    {
      if (logical_const_c)
      {
        riscv_emit_arm_cpsr_store_selected_nzc_const_c_preserve_v(
          &ptr, generated_flag_mask, riscv_reg_t2, logical_carry);
      }
      else
      {
        riscv_emit_arm_cpsr_store_selected_nzcv(
          &ptr, generated_flag_mask, riscv_reg_t2);
      }
    }
    else if (clobber_dead_arithmetic_flags)
      riscv_emit_arm_cpsr_store_arithmetic_selected_nzcv(
        &ptr, generated_flag_mask, riscv_reg_t2);
    else
      riscv_emit_arm_cpsr_store_selected_nzcv(
        &ptr, generated_flag_mask, riscv_reg_t2);
  }
  if (emit_cycles)
  {
    riscv_emit_adjust_cycles(&ptr, cycles);
    if (cycles_emitted)
      *cycles_emitted = true;
  }

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_arm_data_proc_test_with_pc_ex(u8 **translation_ptr_ref,
                                                     riscv_jit_block_meta *meta,
                                                     u32 opcode,
                                                     u32 pc,
                                                     u32 cycles,
                                                     u32 flag_status,
                                                     bool emit_cycles,
                                                     bool *cycles_emitted)
{
  return riscv_emit_native_arm_data_proc_test_with_pc_ex2(
    translation_ptr_ref, meta, opcode, pc, cycles, flag_status, emit_cycles,
    cycles_emitted, false, 0, 0);
}

bool riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted)
{
  return riscv_emit_native_arm_data_proc_test_with_pc_ex2(
    translation_ptr_ref, meta, opcode, pc, cycles, flag_status, emit_cycles,
    cycles_emitted, true, 0, 0);
}

bool riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags_known(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted,
  u32 known_flag_mask,
  u32 known_flags)
{
  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_TEST)
    return false;

  return riscv_emit_native_arm_data_proc_test_with_pc_ex2(
    translation_ptr_ref, meta, opcode, pc, cycles, flag_status, emit_cycles,
    cycles_emitted, true, known_flag_mask, known_flags);
}

bool riscv_emit_native_arm_data_proc_test_with_pc(u8 **translation_ptr_ref,
                                                  riscv_jit_block_meta *meta,
                                                  u32 opcode,
                                                  u32 pc,
                                                  u32 cycles)
{
  return riscv_emit_native_arm_data_proc_test_with_pc_ex(translation_ptr_ref,
                                                        meta, opcode, pc,
                                                        cycles, 0x0fu, true,
                                                        NULL);
}

bool riscv_emit_native_arm_data_proc_test(u8 **translation_ptr_ref,
                                          riscv_jit_block_meta *meta,
                                          u32 opcode,
                                          u32 cycles)
{
  return riscv_emit_native_arm_data_proc_test_with_pc(translation_ptr_ref,
                                                     meta, opcode, 0,
                                                     cycles);
}

static bool riscv_emit_native_arm_multiply2(u8 **translation_ptr_ref,
                                            riscv_jit_block_meta *meta,
                                            u32 opcode,
                                            u32 cycles,
                                            u32 flag_status,
                                            bool clobber_dead_flags)
{
  u32 condition = opcode >> 28;
  u32 accumulate = (opcode >> 21) & 1u;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 live_flag_mask = set_flags ? (flag_status & 0x0cu) : 0;
  u32 rd = (opcode >> 16) & 0xfu;
  u32 rn = (opcode >> 12) & 0xfu;
  u32 rs = (opcode >> 8) & 0xfu;
  u32 rm = opcode & 0xfu;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  /* MUL writes N/Z only; C/V remain architectural state even when their
   * producers are outside this block. */
  (void)clobber_dead_flags;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0fc000f0u) != 0x00000090u ||
      rd == REG_PC || rm == REG_PC || rs == REG_PC ||
      (accumulate && rn == REG_PC))
  {
    return false;
  }

#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  if (!riscv_runtime_perf_disable_mapped_alu_fastpath &&
      !accumulate && !set_flags)
#elif !defined(RISCV_RUNTIME_DISABLE_MAPPED_ALU_FASTPATH)
  if (!accumulate && !set_flags)
#endif
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH) || \
    !defined(RISCV_RUNTIME_DISABLE_MAPPED_ALU_FASTPATH)
  {
    riscv_reg_number rd_host;
    riscv_reg_number rm_host;
    riscv_reg_number rs_host;
    u32 rd_mask;
    u32 rm_mask;
    u32 rs_mask;

    if (riscv_arm_reg_mapped(rd, &rd_host, &rd_mask) &&
        riscv_arm_reg_mapped(rm, &rm_host, &rm_mask) &&
        riscv_arm_reg_mapped(rs, &rs_host, &rs_mask))
    {
      riscv_emit_mapped_regs_reload_mask(
        &ptr, (rm_mask | rs_mask) & ~riscv_mapped_valid_mask);
      translation_ptr = ptr;
      riscv_emit_mul(rd_host, rm_host, rs_host);
      ptr = translation_ptr;
      riscv_mapped_valid_mask |= rd_mask;
      riscv_mapped_dirty_mask |= rd_mask;
      riscv_emit_adjust_cycles(&ptr, cycles);

      *translation_ptr_ref = ptr;
      riscv_native_data_proc_insns++;
      return true;
    }
  }
#endif

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rm);
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);
  translation_ptr = ptr;
  riscv_emit_mul(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
  ptr = translation_ptr;

  if (accumulate)
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t3, rn);
    translation_ptr = ptr;
    riscv_emit_add(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
    ptr = translation_ptr;
  }

  riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);
  if (live_flag_mask)
    riscv_emit_arm_cpsr_store_selected_nzcv(
      &ptr, live_flag_mask, riscv_reg_t2);
  riscv_emit_adjust_cycles(&ptr, cycles);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_arm_multiply(u8 **translation_ptr_ref,
                                    riscv_jit_block_meta *meta,
                                    u32 opcode,
                                    u32 cycles)
{
  return riscv_emit_native_arm_multiply2(translation_ptr_ref, meta, opcode,
                                        cycles, 0x0cu, false);
}

bool riscv_emit_native_arm_multiply_dead_flags(u8 **translation_ptr_ref,
                                               riscv_jit_block_meta *meta,
                                               u32 opcode,
                                               u32 cycles,
                                               u32 flag_status)
{
  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_MULTIPLY)
    return false;

  return riscv_emit_native_arm_multiply2(translation_ptr_ref, meta, opcode,
                                        cycles, flag_status, true);
}

static bool riscv_emit_native_arm_multiply_long2(u8 **translation_ptr_ref,
                                                 riscv_jit_block_meta *meta,
                                                 u32 opcode,
                                                 u32 cycles,
                                                 u32 flag_status,
                                                 bool clobber_dead_flags)
{
  u32 condition = opcode >> 28;
  u32 signed_multiply = (opcode >> 22) & 1u;
  u32 accumulate = (opcode >> 21) & 1u;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 live_flag_mask = set_flags ? (flag_status & 0x0cu) : 0;
  u32 rdhi = (opcode >> 16) & 0xfu;
  u32 rdlo = (opcode >> 12) & 0xfu;
  u32 rs = (opcode >> 8) & 0xfu;
  u32 rm = opcode & 0xfu;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  /* Long multiply has the same N/Z-only flag contract as MUL. */
  (void)clobber_dead_flags;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0f8000f0u) != 0x00800090u ||
      rdhi == REG_PC || rdlo == REG_PC ||
      rm == REG_PC || rs == REG_PC)
  {
    return false;
  }

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rm);
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);
  translation_ptr = ptr;
  riscv_emit_mul(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
  if (signed_multiply)
    riscv_emit_mulh(riscv_reg_t3, riscv_reg_t0, riscv_reg_t1);
  else
    riscv_emit_mulhu(riscv_reg_t3, riscv_reg_t0, riscv_reg_t1);
  ptr = translation_ptr;

  if (accumulate)
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t4, rdlo);
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t5, rdhi);
    translation_ptr = ptr;
    riscv_emit_add(riscv_reg_t2, riscv_reg_t2, riscv_reg_t4);
    riscv_emit_sltu(riscv_reg_t4, riscv_reg_t2, riscv_reg_t4);
    riscv_emit_add(riscv_reg_t3, riscv_reg_t3, riscv_reg_t5);
    riscv_emit_add(riscv_reg_t3, riscv_reg_t3, riscv_reg_t4);
    ptr = translation_ptr;
  }

  riscv_emit_arm_reg_store(&ptr, rdlo, riscv_reg_t2);
  riscv_emit_arm_reg_store(&ptr, rdhi, riscv_reg_t3);
  if (live_flag_mask)
    riscv_emit_arm_cpsr_store_long_selected_nz(
      &ptr, live_flag_mask, false);
  riscv_emit_adjust_cycles(&ptr, cycles);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_arm_multiply_long(u8 **translation_ptr_ref,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 cycles)
{
  return riscv_emit_native_arm_multiply_long2(translation_ptr_ref, meta,
                                             opcode, cycles, 0x0cu, false);
}

bool riscv_emit_native_arm_multiply_long_dead_flags(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 cycles,
  u32 flag_status)
{
  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_MULTIPLY_LONG)
    return false;

  return riscv_emit_native_arm_multiply_long2(translation_ptr_ref, meta,
                                             opcode, cycles, flag_status,
                                             true);
}

bool riscv_emit_native_arm_psr_with_pc_ex(u8 **translation_ptr_ref,
                                          riscv_jit_block_meta *meta,
                                          u32 opcode,
                                          u32 pc,
                                          u32 cycles,
                                          u32 flag_status)
{
  u32 condition = opcode >> 28;
  u32 op_class = (opcode >> 20) & 0xffu;
  u32 use_spsr = (opcode >> 22) & 1u;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 rm = opcode & 0xfu;
  u32 psr_pfield = ((opcode >> 16) & 1u) | ((opcode >> 18) & 2u);
  u32 cpsr_live_flags =
    (!use_spsr && psr_pfield == 2u) ? (flag_status & 0x0fu) : 0x0fu;
  u8 *ptr = *translation_ptr_ref;

  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_PSR)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe)
    return false;

  if ((opcode & 0x0fbf0fffu) == 0x010f0000u)
  {
    if (rd == REG_PC)
      return false;

    if (use_spsr)
    {
      u8 *translation_ptr;

      riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, CPU_MODE);
      riscv_emit_li(&ptr, riscv_reg_t1, (u32)(uintptr_t)&spsr[0]);
      translation_ptr = ptr;
      riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, 0xf);
      riscv_emit_slli(riscv_reg_t0, riscv_reg_t0, 2);
      riscv_emit_add(riscv_reg_t1, riscv_reg_t1, riscv_reg_t0);
      riscv_emit_lw(riscv_reg_t0, riscv_reg_t1, 0);
      ptr = translation_ptr;
    }
    else
    {
      riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, REG_CPSR);
    }

    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t0);
    riscv_emit_adjust_cycles(&ptr, cycles);

    *translation_ptr_ref = ptr;
    riscv_native_psr_insns++;
    return true;
  }

  if (op_class != 0x12u && op_class != 0x16u &&
      op_class != 0x32u && op_class != 0x36u)
  {
    return false;
  }

  if (rd != 0xfu)
    return false;

  if ((op_class == 0x12u || op_class == 0x16u) &&
      ((opcode & 0x00000ff0u) || rm == REG_PC))
  {
    return false;
  }

  if (op_class == 0x32u || op_class == 0x36u)
  {
    u32 flags = riscv_arm_expand_imm(opcode);

    if (psr_pfield == 2u)
    {
      if (!cpsr_live_flags)
      {
        riscv_emit_adjust_cycles(&ptr, cycles);

        *translation_ptr_ref = ptr;
        riscv_native_psr_insns++;
        return true;
      }

      flags &= use_spsr ? RISCV_CPSR_NZCV :
        riscv_arm_cpsr_flags_from_status(cpsr_live_flags);
      if (flags)
        riscv_emit_li(&ptr, riscv_reg_a0, flags);
      if (use_spsr)
        riscv_emit_arm_spsr_flags_store_packed(
          &ptr, flags ? riscv_reg_a0 : riscv_reg_zero);
      else
        riscv_emit_arm_cpsr_flags_store_packed(
          &ptr, flags ? riscv_reg_a0 : riscv_reg_zero);
      riscv_emit_adjust_cycles(&ptr, cycles);

      *translation_ptr_ref = ptr;
      riscv_native_psr_insns++;
      return true;
    }

    riscv_emit_li(&ptr, riscv_reg_a0, riscv_arm_expand_imm(opcode));
  }
  else
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rm);

  if (psr_pfield == 2u)
  {
    if (!cpsr_live_flags)
    {
      riscv_emit_adjust_cycles(&ptr, cycles);

      *translation_ptr_ref = ptr;
      riscv_native_psr_insns++;
      return true;
    }

    if (use_spsr)
      riscv_emit_arm_spsr_flags_store(&ptr, riscv_reg_a0);
    else
    {
      riscv_emit_arm_cpsr_flags_select(&ptr, riscv_reg_a0,
                                       cpsr_live_flags);
      riscv_emit_arm_cpsr_flags_store_packed(&ptr, riscv_reg_a0);
    }
    riscv_emit_adjust_cycles(&ptr, cycles);

    *translation_ptr_ref = ptr;
    riscv_native_psr_insns++;
    return true;
  }

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 4u);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_li(&ptr, riscv_reg_a1, psr_pfield);
  riscv_emit_stateful_c_call_stack(
    &ptr, use_spsr ? RISCV_STACK_HELPER_STORE_SPSR :
                     RISCV_STACK_HELPER_STORE_CPSR,
    false);
  riscv_emit_adjust_cycles(&ptr, cycles);
  riscv_emit_terminal_helper_call_no_flush(&ptr, meta);

  *translation_ptr_ref = ptr;
  riscv_native_psr_insns++;
  return true;
}

bool riscv_emit_native_arm_psr_with_pc(u8 **translation_ptr_ref,
                                       riscv_jit_block_meta *meta,
                                       u32 opcode,
                                       u32 pc,
                                       u32 cycles)
{
  return riscv_emit_native_arm_psr_with_pc_ex(
    translation_ptr_ref, meta, opcode, pc, cycles, 0x0fu);
}

bool riscv_emit_native_arm_psr(u8 **translation_ptr_ref,
                               riscv_jit_block_meta *meta,
                               u32 opcode,
                               u32 cycles)
{
  return riscv_emit_native_arm_psr_with_pc(translation_ptr_ref, meta,
                                          opcode, 0, cycles);
}

static bool riscv_emit_native_arm_direct_branch(u8 **translation_ptr_ref,
                                                riscv_jit_block_meta *meta,
                                                u8 **branch_source,
                                                u32 opcode,
                                                u32 pc,
                                                u32 cycles,
                                                bool link,
                                                bool patchable,
                                                bool short_patch_site,
                                                bool flush_before_patch_site)
{
  u32 condition = opcode >> 28;
  u32 target_pc = pc + (u32)riscv_arm_branch_delta(opcode);
  u8 *ptr = *translation_ptr_ref;

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe)
    return false;

  if (link)
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 4u);
    riscv_emit_arm_reg_store(&ptr, REG_LR, riscv_reg_t0);
  }

  if (!patchable)
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, target_pc);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  }
  riscv_emit_adjust_cycles(&ptr, cycles);

  if (patchable)
  {
    /* Keep the scheduler checkpoint on the patched path. A direct chain can
       otherwise run forever without reaching the scheduler slow stub. */
    if (target_pc == idle_loop_target_pc)
      riscv_emit_cycles_set(&ptr, riscv_reg_zero);

    if (branch_source)
      *branch_source = riscv_emit_branch_patch_site_with_cycle_exit(
        &ptr, meta, target_pc, short_patch_site, flush_before_patch_site);
    else
      (void)riscv_emit_branch_patch_site_with_cycle_exit(
        &ptr, meta, target_pc, short_patch_site, flush_before_patch_site);
  }
  else
  {
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    /* The non-patchable API still describes a terminal guest branch.  Emit
     * its scheduler/lookup tail here so block finalization can reserve its
     * generic tail exclusively for paths that really fall through. */
    riscv_emit_terminal_helper_call(&ptr, meta);
  }

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}

bool riscv_emit_native_arm_b(u8 **translation_ptr_ref,
                             riscv_jit_block_meta *meta,
                             u32 opcode,
                             u32 pc,
                             u32 cycles)
{
  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            NULL, opcode, pc, cycles,
                                            false, false, false, true);
}

bool riscv_emit_native_arm_bl(u8 **translation_ptr_ref,
                              riscv_jit_block_meta *meta,
                              u32 opcode,
                              u32 pc,
                              u32 cycles)
{
  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            NULL, opcode, pc, cycles,
                                            true, false, false, true);
}

bool riscv_emit_native_arm_b_patchable(u8 **translation_ptr_ref,
                                       riscv_jit_block_meta *meta,
                                       u8 **branch_source,
                                       u32 opcode,
                                       u32 pc,
                                       u32 cycles,
                                       bool short_patch_site,
                                       bool flush_before_patch_site)
{
  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_B)
    return false;

  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            branch_source, opcode, pc,
                                            cycles, false, true,
                                            short_patch_site,
                                            flush_before_patch_site);
}

bool riscv_emit_native_arm_bl_patchable(u8 **translation_ptr_ref,
                                        riscv_jit_block_meta *meta,
                                        u8 **branch_source,
                                        u32 opcode,
                                        u32 pc,
                                        u32 cycles,
                                        bool short_patch_site,
                                        bool flush_before_patch_site)
{
  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_BL)
    return false;

  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            branch_source, opcode, pc,
                                            cycles, true, true,
                                            short_patch_site,
                                            flush_before_patch_site);
}

bool riscv_emit_native_arm_bx(u8 **translation_ptr_ref,
                              riscv_jit_block_meta *meta,
                              u32 opcode,
                              u32 pc,
                              u32 cycles)
{
  u32 condition = opcode >> 28;
  u32 rn = opcode & 0xfu;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_BX)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe)
    return false;

  if (rn == REG_PC)
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 8u);
  else
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);

  translation_ptr = ptr;
  /* REG_PC publication flushes dirty mapped state and may use t0/t1 while
   * packing NZCV.  Preserve the raw target's state bit before that flush. */
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

static bool riscv_emit_native_arm_swi_common(u8 **translation_ptr_ref,
                                             riscv_jit_block_meta *meta,
                                             u8 **branch_source,
                                             u32 opcode,
                                             u32 pc,
                                             u32 cycles,
                                             bool patchable,
                                             bool short_patch_site)
{
  u32 condition = opcode >> 28;
  u32 swinum = (opcode >> 16) & 0xffu;
  u8 *ptr = *translation_ptr_ref;

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0f000000u) != 0x0f000000u ||
      (swinum & 0xfeu) == 0x06u)
  {
    return false;
  }

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc + 4u);
  riscv_emit_stateful_c_call_stack(
    &ptr, RISCV_STACK_HELPER_EXECUTE_SWI_ARM, patchable);
  riscv_emit_adjust_cycles(&ptr, cycles);

  if (patchable)
  {
    if (idle_loop_target_pc == 0x00000008u)
      riscv_emit_cycles_set(&ptr, riscv_reg_zero);

    if (branch_source)
      *branch_source = riscv_emit_branch_patch_site_with_cycle_exit(
        &ptr, meta, 0x00000008u, short_patch_site, true);
    else
      (void)riscv_emit_branch_patch_site_with_cycle_exit(
        &ptr, meta, 0x00000008u, short_patch_site, true);
  }
  else
  {
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    riscv_emit_terminal_helper_call_no_flush(&ptr, meta);
  }

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}

bool riscv_emit_native_arm_swi(u8 **translation_ptr_ref,
                               riscv_jit_block_meta *meta,
                               u32 opcode,
                               u32 pc,
                               u32 cycles)
{
  return riscv_emit_native_arm_swi_common(translation_ptr_ref, meta,
                                         NULL, opcode, pc, cycles, false,
                                         false);
}

bool riscv_emit_native_arm_swi_patchable(u8 **translation_ptr_ref,
                                         riscv_jit_block_meta *meta,
                                         u8 **branch_source,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles,
                                         bool short_patch_site)
{
  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_SWI)
    return false;

  return riscv_emit_native_arm_swi_common(translation_ptr_ref, meta,
                                         branch_source, opcode, pc,
                                         cycles, true, short_patch_site);
}

bool riscv_emit_native_arm_hle_div(u8 **translation_ptr_ref,
                                   riscv_jit_block_meta *meta,
                                   bool divarm,
                                   u32 cycles)
{
  u8 *ptr = *translation_ptr_ref;

  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_HLE_DIV)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  riscv_emit_li(&ptr, riscv_reg_a0, divarm ? 1u : 0u);
  riscv_emit_stateful_c_call_stack(&ptr, RISCV_STACK_HELPER_HLE_DIV, true);
  riscv_emit_adjust_cycles(&ptr, cycles);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_arm_swap(u8 **translation_ptr_ref,
                                riscv_jit_block_meta *meta,
                                u32 opcode,
                                u32 pc,
                                u32 cycles)
{
  const u32 swp_mask = ((0x1fu << 23) | (3u << 20) | (0xffu << 4));
  const u32 swp_tag = ((2u << 23) | (9u << 4));
  u32 condition = opcode >> 28;
  u32 byte = (opcode >> 22) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 rm = opcode & 0xfu;
  u8 *ptr = *translation_ptr_ref;

  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_SWAP)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & swp_mask) != swp_tag ||
      rn == REG_PC || rd == REG_PC || rm == REG_PC)
  {
    return false;
  }

  riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);
  riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rm);
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a2, pc);
  riscv_emit_c_call_stack(&ptr, byte ? RISCV_STACK_HELPER_SWAP_U8
                                     : RISCV_STACK_HELPER_SWAP_U32);
  riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
  riscv_emit_adjust_cycles(&ptr, cycles + 3u);
  riscv_emit_terminal_helper_call(&ptr, meta);

  *translation_ptr_ref = ptr;
  riscv_native_store_insns++;
  return true;
}

static void riscv_emit_arm_block_writeback(u8 **ptr_ref,
                                           u32 rn,
                                           s32 end_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);
  translation_ptr = ptr;
  if (end_offset)
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, end_offset);
  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, rn, riscv_reg_t0);

  *ptr_ref = ptr;
}

static s32 riscv_arm_block_origin_offset(bool pre_index,
                                         bool up,
                                         u32 count,
                                         bool from_writeback)
{
  s32 byte_count = (s32)(count * 4u);

  if (from_writeback)
  {
    if (up)
      return pre_index ? (-byte_count + 4) : -byte_count;
    return pre_index ? 0 : 4;
  }

  if (up)
    return pre_index ? 4 : 0;
  return pre_index ? -byte_count : (-byte_count + 4);
}

static void riscv_emit_arm_block_transfer_address(u8 **ptr_ref,
                                                  u32 rn,
                                                  s32 origin_offset,
                                                  u32 transfer_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);

  translation_ptr = ptr;
  if (origin_offset)
    riscv_emit_addi(riscv_reg_a0, riscv_reg_a0, origin_offset);
  riscv_emit_andi(riscv_reg_a0, riscv_reg_a0, -4);
  if (transfer_offset)
    riscv_emit_addi(riscv_reg_a0, riscv_reg_a0, (s32)transfer_offset);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_cursor_init(u8 **ptr_ref,
                                             u32 rn,
                                             s32 origin_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_block_transfer_address(&ptr, rn, origin_offset, 0);
  translation_ptr = ptr;
  riscv_emit_sw(riscv_reg_a0, riscv_reg_sp, 0);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_cursor_load(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr = ptr;

  riscv_emit_lw(riscv_reg_a0, riscv_reg_sp, 0);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_cursor_advance(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr = ptr;

  riscv_emit_lw(riscv_reg_t0, riscv_reg_sp, 0);
  riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, 4);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_sp, 0);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_s2_cursor_init(u8 **ptr_ref,
                                                riscv_jit_block_meta *meta,
                                                u32 rn,
                                                s32 origin_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if (meta)
    meta->reserved = 1;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);

  translation_ptr = ptr;
  if (origin_offset)
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, origin_offset);
  riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, -4);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_sp, 0);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_s2_writeback_cursor_init(
  u8 **ptr_ref, riscv_jit_block_meta *meta, u32 rn, s32 end_offset,
  s32 origin_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if (meta)
    meta->reserved = 1;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);

  translation_ptr = ptr;
  if (origin_offset == -end_offset)
  {
    riscv_emit_addi(riscv_reg_t1, riscv_reg_t0, end_offset);
    ptr = translation_ptr;
    riscv_emit_arm_reg_store(&ptr, rn, riscv_reg_t1);

    translation_ptr = ptr;
    riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, -4);
    riscv_emit_sw(riscv_reg_t0, riscv_reg_sp, 0);
    ptr = translation_ptr;

    *ptr_ref = ptr;
    return;
  }

  if (end_offset)
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, end_offset);
  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, rn, riscv_reg_t0);

  translation_ptr = ptr;
  if (origin_offset)
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, origin_offset);
  riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, -4);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_sp, 0);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_s2_cursor_load(u8 **ptr_ref)
{
  riscv_emit_arm_block_cursor_load(ptr_ref);
}

static void riscv_emit_arm_block_s2_cursor_advance(u8 **ptr_ref)
{
  riscv_emit_arm_block_cursor_advance(ptr_ref);
}

static void riscv_emit_block_pc_base_restore(u8 **ptr_ref,
                                             const riscv_jit_block_meta *meta)
{
  (void)ptr_ref;
  (void)meta;
}

bool riscv_emit_native_arm_block_memory(u8 **translation_ptr_ref,
                                        riscv_jit_block_meta *meta,
                                        u32 opcode,
                                        u32 pc,
                                        u32 cycles)
{
  u32 condition = opcode >> 28;
  bool pre_index = ((opcode >> 24) & 1u) != 0;
  bool up = ((opcode >> 23) & 1u) != 0;
  bool sbit = ((opcode >> 22) & 1u) != 0;
  bool writeback = ((opcode >> 21) & 1u) != 0;
  u32 reglist = opcode & 0xffffu;
  bool load = ((opcode >> 20) & 1u) != 0;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 count = riscv_word_bit_count(reglist);
  s32 end_offset = up ? (s32)(count * 4u) : -(s32)(count * 4u);
  bool base_in_list;
  bool base_first;
  bool load_base_in_list;
  bool writeback_first;
  bool address_from_writeback;
  bool load_pc;
  bool store_pc;
  bool use_s2_cursor;
  s32 origin_offset;
  u8 *ptr = *translation_ptr_ref;
  u32 offset = 0;
  u32 i;

  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_BLOCK_MEMORY)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0e000000u) != 0x08000000u ||
      reglist == 0)
  {
    return false;
  }

  if (sbit || rn == REG_PC)
  {
    riscv_emit_li(&ptr, riscv_reg_a0, opcode);
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a1, pc);
    riscv_emit_stateful_c_call_stack(
      &ptr, RISCV_STACK_HELPER_ARM_BLOCK_MEMORY, false);
    riscv_emit_adjust_cycles(&ptr, cycles + count);
    riscv_emit_terminal_helper_call_no_flush(&ptr, meta);

    if (load && (reglist & (1u << REG_PC)))
      meta->flags |= RISCV_BLOCK_PC_WRITTEN;

    *translation_ptr_ref = ptr;
    if (load)
      riscv_native_load_insns++;
    else
      riscv_native_store_insns++;
    return true;
  }

  base_in_list = ((reglist >> rn) & 1u) != 0;
  load_base_in_list = load && base_in_list;
  base_first = (((1u << rn) - 1u) & reglist) == 0;
  writeback_first = load || !(base_in_list && base_first);
  address_from_writeback = writeback && writeback_first && !load_base_in_list;
  origin_offset =
    riscv_arm_block_origin_offset(pre_index, up, count,
                                  address_from_writeback);
  load_pc = load && ((reglist & (1u << REG_PC)) != 0);
  store_pc = !load && ((reglist & (1u << REG_PC)) != 0);
  use_s2_cursor = !store_pc &&
    (load_base_in_list || load_pc ||
     (origin_offset ? count >= 2u : count >= 5u));

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 4u);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);

  if (address_from_writeback && use_s2_cursor)
    riscv_emit_arm_block_s2_writeback_cursor_init(
      &ptr, meta, rn, end_offset, origin_offset);
  else
  {
    if (address_from_writeback)
      riscv_emit_arm_block_writeback(&ptr, rn, end_offset);
    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_init(&ptr, meta, rn, origin_offset);
    else if (load_base_in_list)
      riscv_emit_arm_block_cursor_init(&ptr, rn, origin_offset);
  }

  for (i = 0; i < 16u; i++)
  {
    if (!((reglist >> i) & 1u))
      continue;

    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_load(&ptr);
    else if (load_base_in_list)
      riscv_emit_arm_block_cursor_load(&ptr);
    else
      riscv_emit_arm_block_transfer_address(&ptr, rn, origin_offset, offset);

    if (load)
    {
      riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_BLOCK_READ32);
      riscv_emit_arm_reg_store(&ptr, i, riscv_reg_a0);
      if (use_s2_cursor && (offset + 4u) < (count * 4u))
        riscv_emit_arm_block_s2_cursor_advance(&ptr);
      else if (load_base_in_list && (offset + 4u) < (count * 4u))
        riscv_emit_arm_block_cursor_advance(&ptr);
    }
    else
    {
      if (i == REG_PC)
        riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a1, pc + 8u);
      else
        riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, i);
      riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_BLOCK_STORE32);
      if (use_s2_cursor && (offset + 4u) < (count * 4u))
        riscv_emit_arm_block_s2_cursor_advance(&ptr);
    }

    offset += 4u;
  }

  if (use_s2_cursor && !load_pc)
    riscv_emit_block_pc_base_restore(&ptr, meta);

  if (writeback && !writeback_first && !load_base_in_list)
    riscv_emit_arm_block_writeback(&ptr, rn, end_offset);

  riscv_emit_adjust_cycles(&ptr, cycles + count);

  if (load && (reglist & (1u << REG_PC)))
  {
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    riscv_emit_terminal_helper_call(&ptr, meta);
  }
  else if (!load)
  {
    /* riscv_store_u32 returns the accumulated alert state.  In particular,
     * the last transfer still reports an alert raised by an earlier transfer
     * in this STM, so a0 can feed the normal store-alert branch directly. */
    riscv_emit_store_alert_branch(&ptr, meta);
  }

  *translation_ptr_ref = ptr;
  if (load)
    riscv_native_load_insns++;
  else
    riscv_native_store_insns++;
  return true;
}

static bool riscv_emit_native_arm_extra_memory(u8 **translation_ptr_ref,
                                               riscv_jit_block_meta *meta,
                                               u32 opcode,
                                               u32 pc,
                                               u32 cycles,
                                               bool emit_cycles,
                                               bool *cycles_emitted,
                                               u32 const_mask,
                                               const u32 *const_values)
{
  u32 condition = opcode >> 28;
  u32 pre_index = (opcode >> 24) & 1u;
  u32 up = (opcode >> 23) & 1u;
  u32 immediate_offset = (opcode >> 22) & 1u;
  u32 writeback = (opcode >> 21) & 1u;
  u32 load = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 mem_type = (opcode >> 5) & 0x3u;
  u32 rm = opcode & 0xfu;
  u32 offset = immediate_offset ? (((opcode >> 4) & 0xf0u) | rm) : rm;
  u8 *ptr = *translation_ptr_ref;
  bool writeback_address = writeback || !pre_index;
  bool pc_base = rn == REG_PC;
  riscv_reg_number writeback_reg = riscv_reg_a0;
  u32 const_register_offset = 0;
  bool const_register_offset_valid =
    !immediate_offset &&
    riscv_arm_const_reg_value(rm, pc + 8u, const_mask, const_values,
                              &const_register_offset);
  bool writeback_same_as_base =
    (immediate_offset && offset == 0) ||
    (const_register_offset_valid && const_register_offset == 0);
  bool writeback_overwritten_by_load = load && rd == rn;
  bool dead_post_index_writeback = writeback_overwritten_by_load && !pre_index;
  bool known_nonram = false;
  bool known_ram = false;

  if ((load && riscv_fast_ram_reads_enabled()) ||
      (!load && riscv_fast_ram_stores_enabled()))
  {
    u32 const_base_address = 0;
    u32 const_effective_address = 0;
    bool const_base_address_valid = riscv_arm_const_reg_value(
      rn, pc + 8u, const_mask, const_values, &const_base_address);
    bool const_effective_address_valid = false;

    if (const_base_address_valid)
    {
      if (!pre_index)
      {
        const_effective_address = const_base_address;
        const_effective_address_valid = true;
      }
      else if (immediate_offset)
      {
        const_effective_address = up ? const_base_address + offset :
                                       const_base_address - offset;
        const_effective_address_valid = true;
      }
      else if (const_register_offset_valid)
      {
        const_effective_address = up ?
          const_base_address + const_register_offset :
          const_base_address - const_register_offset;
        const_effective_address_valid = true;
      }
    }
    if (const_effective_address_valid)
    {
      u32 region = const_effective_address >> 24;
      known_nonram = region != 0x02u && region != 0x03u;
      known_ram = !known_nonram;
    }
  }

  if (cycles_emitted)
    *cycles_emitted = false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0e000090u) != 0x00000090u ||
      (pc_base && writeback_address) ||
      mem_type == 0 ||
      (load && rd == REG_PC &&
       ((mem_type != 1 && mem_type != 2 && mem_type != 3) ||
        writeback_address || pc_base)) ||
      (!load && mem_type != 1) ||
      (!immediate_offset && ((opcode >> 8) & 0xfu) != 0))
  {
    return false;
  }

  if (pc_base && immediate_offset)
  {
    u32 pc_base_value = pc + 8u;
    u32 pc_addr = up ? (pc_base_value + offset) : (pc_base_value - offset);
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc_addr);
  }
  else if (pc_base && const_register_offset_valid && pre_index)
  {
    u32 pc_base_value = pc + 8u;
    u32 pc_addr = up ? (pc_base_value + const_register_offset) :
                       (pc_base_value - const_register_offset);
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc_addr);
  }
  else if (pc_base)
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc + 8u);
  }
  else
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);
  }

  if (immediate_offset && !pc_base && !dead_post_index_writeback)
  {
    riscv_emit_arm_memory_const_offset(&ptr, pre_index, up, offset,
                                       &writeback_reg);
  }
  else if (const_register_offset_valid && !pc_base &&
           !dead_post_index_writeback)
  {
    riscv_emit_arm_memory_const_offset(&ptr, pre_index, up,
                                       const_register_offset,
                                       &writeback_reg);
  }
  else if (!immediate_offset && !const_register_offset_valid &&
           !dead_post_index_writeback)
  {
    u8 *translation_ptr;

    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, rm, pc + 8u);
    translation_ptr = ptr;
    if (!pre_index)
    {
      if (up)
        riscv_emit_add(riscv_reg_t2, riscv_reg_a0, riscv_reg_t0);
      else
        riscv_emit_sub(riscv_reg_t2, riscv_reg_a0, riscv_reg_t0);
      writeback_reg = riscv_reg_t2;
    }
    else if (up)
    {
      riscv_emit_add(riscv_reg_a0, riscv_reg_a0, riscv_reg_t0);
    }
    else
    {
      riscv_emit_sub(riscv_reg_a0, riscv_reg_a0, riscv_reg_t0);
    }
    ptr = translation_ptr;
  }

  if (!load && rd != REG_PC)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);

  if (writeback_address && !writeback_same_as_base &&
      !writeback_overwritten_by_load)
    riscv_emit_arm_reg_store(&ptr, rn, writeback_reg);

  if (load)
  {
    u32 read_helper_stack_offset;

    switch (mem_type)
    {
      case 1:
        read_helper_stack_offset = 0;
        break;
      case 2:
        read_helper_stack_offset = RISCV_STACK_HELPER_READ8S;
        break;
      default:
        read_helper_stack_offset = RISCV_STACK_HELPER_READ16S;
        break;
    }

    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a1, pc);
    if (read_helper_stack_offset)
    {
      uintptr_t direct_target = mem_type == 2 ?
        (uintptr_t)read_memory8s : (uintptr_t)read_memory16s;
      riscv_emit_memory_read_call_stack_known(
        &ptr, read_helper_stack_offset, direct_target, known_nonram);
    }
    else
    {
      riscv_emit_memory_read_call_stack_known(
        &ptr, RISCV_STACK_HELPER_READ16, (uintptr_t)read_memory16,
        known_nonram);
    }
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    if (rd == REG_PC)
      meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    if (emit_cycles || rd == REG_PC)
    {
      riscv_emit_adjust_cycles(&ptr, cycles + 2u);
      if (cycles_emitted)
        *cycles_emitted = true;
    }
    if (rd == REG_PC)
      riscv_emit_terminal_helper_call(&ptr, meta);
    riscv_native_load_insns++;
  }
  else
  {
    u8 *translation_ptr;

    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a2, pc + 4u);
    if (rd == REG_PC)
    {
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_a1, riscv_reg_a2, 8);
      ptr = translation_ptr;
    }
    riscv_emit_memory_store_call_stack_known(
      &ptr, RISCV_STACK_HELPER_STORE16,
      (uintptr_t)riscv_store_u16_pc, known_ram);
    riscv_emit_adjust_cycles(&ptr, cycles + 1u);
    if (cycles_emitted)
      *cycles_emitted = true;
    riscv_emit_store_alert_branch(&ptr, meta);
    riscv_native_store_insns++;
  }

  *translation_ptr_ref = ptr;
  return true;
}

static void riscv_emit_arm_memory_imm_offset(u8 **ptr_ref,
                                             riscv_reg_number rd,
                                             riscv_reg_number rs,
                                             u32 offset,
                                             bool up)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if (!offset)
  {
    if (rd != rs)
    {
      translation_ptr = ptr;
      riscv_emit_add(rd, rs, riscv_reg_zero);
      ptr = translation_ptr;
    }
    *ptr_ref = ptr;
    return;
  }

  if (up && offset <= 2047u)
  {
    translation_ptr = ptr;
    riscv_emit_addi(rd, rs, offset);
    ptr = translation_ptr;
  }
  else if (!up && offset <= 2048u)
  {
    translation_ptr = ptr;
    riscv_emit_addi(rd, rs, -(int)offset);
    ptr = translation_ptr;
  }
  else
  {
    riscv_emit_li(&ptr, riscv_reg_t0, offset);
    translation_ptr = ptr;
    if (up)
      riscv_emit_add(rd, rs, riscv_reg_t0);
    else
      riscv_emit_sub(rd, rs, riscv_reg_t0);
    ptr = translation_ptr;
  }

  *ptr_ref = ptr;
}

static bool riscv_arm_memory_reg_offset_const(u32 opcode,
                                              u32 pc,
                                              u32 const_mask,
                                              const u32 *const_values,
                                              u32 *offset_out)
{
  u32 rm = opcode & 0xfu;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;
  u32 value;

  if ((opcode >> 4) & 1u)
    return false;

  if (shift_type == 1u && shift == 0)
  {
    *offset_out = 0;
    return true;
  }

  if (!riscv_arm_const_reg_value(rm, pc + 8u, const_mask, const_values,
                                 &value))
    return false;

  return riscv_arm_const_imm_shift(value, shift_type, shift, offset_out);
}

static void riscv_emit_arm_memory_const_offset(u8 **ptr_ref,
                                               bool pre_index,
                                               bool up,
                                               u32 const_offset,
                                               riscv_reg_number *writeback_reg)
{
  if (!pre_index)
  {
    if (const_offset)
    {
      riscv_emit_arm_memory_imm_offset(ptr_ref, riscv_reg_t2, riscv_reg_a0,
                                       const_offset, up);
      *writeback_reg = riscv_reg_t2;
    }
  }
  else if (const_offset)
  {
    riscv_emit_arm_memory_imm_offset(ptr_ref, riscv_reg_a0, riscv_reg_a0,
                                     const_offset, up);
  }
}

static bool riscv_emit_arm_memory_reg_offset(u8 **ptr_ref,
                                             riscv_jit_block_meta *meta,
                                             u32 opcode,
                                             u32 pc)
{
  u32 rm = opcode & 0xfu;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if ((opcode >> 4) & 1u)
    return false;

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, rm, pc + 8u);
  translation_ptr = ptr;

  switch (shift_type)
  {
    case 0:
      if (shift)
        riscv_emit_slli(riscv_reg_t0, riscv_reg_t0, shift);
      break;
    case 1:
      if (shift)
        riscv_emit_srli(riscv_reg_t0, riscv_reg_t0, shift);
      else
        riscv_emit_add(riscv_reg_t0, riscv_reg_zero, riscv_reg_zero);
      break;
    case 2:
      riscv_emit_srai(riscv_reg_t0, riscv_reg_t0, shift ? shift : 31u);
      break;
    default:
      if (shift)
      {
        riscv_emit_srli(riscv_reg_t1, riscv_reg_t0, shift);
        riscv_emit_slli(riscv_reg_t0, riscv_reg_t0, 32u - shift);
        riscv_emit_or(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
      }
      else
      {
        ptr = translation_ptr;
        riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t1);
        translation_ptr = ptr;
        riscv_emit_srli(riscv_reg_t0, riscv_reg_t0, 1);
        riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, 31);
        riscv_emit_or(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
      }
      break;
  }

  *ptr_ref = translation_ptr;
  return true;
}

bool riscv_emit_native_arm_access_memory_ex_const(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  bool emit_cycles,
  bool *cycles_emitted,
  u32 const_mask,
  const u32 *const_values)
{
  u32 condition = opcode >> 28;
  u32 register_offset = (opcode >> 25) & 1u;
  u32 pre_index = (opcode >> 24) & 1u;
  u32 up = (opcode >> 23) & 1u;
  u32 byte = (opcode >> 22) & 1u;
  u32 writeback = (opcode >> 21) & 1u;
  u32 load = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 offset = opcode & 0xfffu;
  u8 *ptr = *translation_ptr_ref;
  bool writeback_address = writeback || !pre_index;
  bool pc_base = rn == REG_PC;
  riscv_reg_number writeback_reg = riscv_reg_a0;
  u32 const_register_offset = 0;
  bool const_register_offset_valid =
    register_offset &&
    riscv_arm_memory_reg_offset_const(opcode, pc, const_mask, const_values,
                                      &const_register_offset);
  bool writeback_same_as_base =
    (!register_offset && offset == 0) ||
    (const_register_offset_valid && const_register_offset == 0);
  bool writeback_overwritten_by_load = load && rd == rn;
  bool dead_post_index_writeback = writeback_overwritten_by_load && !pre_index;
  bool known_nonram = false;
  bool known_ram = false;

  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_MEMORY)
    return false;

  if ((load && riscv_fast_ram_reads_enabled()) ||
      (!load && riscv_fast_ram_stores_enabled()))
  {
    u32 const_base_address = 0;
    u32 const_effective_address = 0;
    bool const_base_address_valid = riscv_arm_const_reg_value(
      rn, pc + 8u, const_mask, const_values, &const_base_address);
    bool const_effective_address_valid = false;

    if (const_base_address_valid)
    {
      if (!pre_index)
      {
        const_effective_address = const_base_address;
        const_effective_address_valid = true;
      }
      else if (!register_offset)
      {
        const_effective_address = up ? const_base_address + offset :
                                       const_base_address - offset;
        const_effective_address_valid = true;
      }
      else if (const_register_offset_valid)
      {
        const_effective_address = up ?
          const_base_address + const_register_offset :
          const_base_address - const_register_offset;
        const_effective_address_valid = true;
      }
    }
    if (const_effective_address_valid)
    {
      u32 region = const_effective_address >> 24;
      known_nonram = region != 0x02u && region != 0x03u;
      known_ram = !known_nonram;
    }
  }

  if (cycles_emitted)
    *cycles_emitted = false;

  if ((opcode & 0x0c000000u) != 0x04000000u)
  {
    return riscv_emit_native_arm_extra_memory(translation_ptr_ref, meta,
                                             opcode, pc, cycles,
                                             emit_cycles, cycles_emitted,
                                             const_mask, const_values);
  }

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (pc_base && writeback_address) ||
      (load && rd == REG_PC && byte &&
       (writeback_address || register_offset)))
  {
    return false;
  }

  if (pc_base && !register_offset)
  {
    u32 pc_base_value = pc + 8u;
    u32 pc_addr = up ? (pc_base_value + offset) : (pc_base_value - offset);
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc_addr);
  }
  else if (pc_base && const_register_offset_valid && pre_index)
  {
    u32 pc_base_value = pc + 8u;
    u32 pc_addr = up ? (pc_base_value + const_register_offset) :
                       (pc_base_value - const_register_offset);
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc_addr);
  }
  else if (pc_base)
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc + 8u);
  }
  else
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);
  }

  if (register_offset && !dead_post_index_writeback)
  {
    if (const_register_offset_valid && !pc_base)
    {
      riscv_emit_arm_memory_const_offset(&ptr, pre_index, up,
                                         const_register_offset,
                                         &writeback_reg);
    }
    else if (!const_register_offset_valid)
    {
      u8 *translation_ptr;

      if (!riscv_emit_arm_memory_reg_offset(&ptr, meta, opcode, pc))
        return false;

      translation_ptr = ptr;
      if (!pre_index)
      {
        if (up)
          riscv_emit_add(riscv_reg_t2, riscv_reg_a0, riscv_reg_t0);
        else
          riscv_emit_sub(riscv_reg_t2, riscv_reg_a0, riscv_reg_t0);
        writeback_reg = riscv_reg_t2;
      }
      else if (up)
      {
        riscv_emit_add(riscv_reg_a0, riscv_reg_a0, riscv_reg_t0);
      }
      else
      {
        riscv_emit_sub(riscv_reg_a0, riscv_reg_a0, riscv_reg_t0);
      }
      ptr = translation_ptr;
    }
  }
  else if (!pc_base && !pre_index && offset && !dead_post_index_writeback)
  {
    riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_t2, riscv_reg_a0,
                                     offset, up);
    writeback_reg = riscv_reg_t2;
  }
  else if (!pc_base && pre_index && offset)
  {
    riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_a0, riscv_reg_a0,
                                     offset, up);
  }

  if (!load && rd != REG_PC)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);

  if (writeback_address && !writeback_same_as_base &&
      !writeback_overwritten_by_load)
    riscv_emit_arm_reg_store(&ptr, rn, writeback_reg);

  if (load)
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a1, pc);
    if (byte)
      riscv_emit_memory_read_call_stack_known(
        &ptr, RISCV_STACK_HELPER_READ8, (uintptr_t)read_memory8,
        known_nonram);
    else
      riscv_emit_memory_read_call_stack_known(
        &ptr, RISCV_STACK_HELPER_READ32, (uintptr_t)read_memory32,
        known_nonram);
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    if (rd == REG_PC)
      meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    if (emit_cycles || rd == REG_PC)
    {
      riscv_emit_adjust_cycles(&ptr, cycles + 2u);
      if (cycles_emitted)
        *cycles_emitted = true;
    }
    if (rd == REG_PC)
      riscv_emit_terminal_helper_call(&ptr, meta);
    riscv_native_load_insns++;
  }
  else
  {
    u8 *translation_ptr;

    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a2, pc + 4u);
    if (rd == REG_PC)
    {
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_a1, riscv_reg_a2, 8);
      ptr = translation_ptr;
    }
    if (byte)
      riscv_emit_memory_store_call_stack_known(
        &ptr, RISCV_STACK_HELPER_STORE8,
        (uintptr_t)riscv_store_u8_pc, known_ram);
    else
      riscv_emit_memory_store_call_stack_known(
        &ptr, RISCV_STACK_HELPER_STORE32,
        (uintptr_t)riscv_store_u32_pc, known_ram);
    riscv_emit_adjust_cycles(&ptr, cycles + 1u);
    if (cycles_emitted)
      *cycles_emitted = true;
    riscv_emit_store_alert_branch(&ptr, meta);
    riscv_native_store_insns++;
  }

  *translation_ptr_ref = ptr;
  return true;
}

bool riscv_emit_native_arm_access_memory_ex(u8 **translation_ptr_ref,
                                            riscv_jit_block_meta *meta,
                                            u32 opcode,
                                            u32 pc,
                                            u32 cycles,
                                            bool emit_cycles,
                                            bool *cycles_emitted)
{
  return riscv_emit_native_arm_access_memory_ex_const(
    translation_ptr_ref, meta, opcode, pc, cycles, emit_cycles,
    cycles_emitted, 0, NULL);
}

bool riscv_emit_native_arm_access_memory(u8 **translation_ptr_ref,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles)
{
  return riscv_emit_native_arm_access_memory_ex(translation_ptr_ref, meta,
                                               opcode, pc, cycles,
                                               true, NULL);
}

bool riscv_emit_native_arm_load_pc_pool_const(u8 **translation_ptr_ref,
                                              riscv_jit_block_meta *meta,
                                              u32 rd,
                                              u32 value,
                                              u32 cycles,
                                              bool emit_cycles,
                                              bool *cycles_emitted)
{
  u8 *ptr = *translation_ptr_ref;

  if (riscv_debug_disable_arm_native & RISCV_DEBUG_DISABLE_ARM_PC_POOL)
    return false;

  if (cycles_emitted)
    *cycles_emitted = false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED) ||
      rd >= REG_PC)
  {
    return false;
  }

  if (value)
  {
    riscv_reg_number mapped_rd;

    if (riscv_arm_reg_mapped(rd, &mapped_rd, NULL))
    {
      riscv_emit_guest_pc_load(&ptr, meta, mapped_rd, value);
      riscv_emit_arm_reg_store(&ptr, rd, mapped_rd);
    }
    else
    {
      riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t2, value);
      riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);
    }
  }
  else
  {
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_zero);
  }

  if (emit_cycles)
  {
    riscv_emit_adjust_cycles(&ptr, cycles + 2u);
    if (cycles_emitted)
      *cycles_emitted = true;
  }

  *translation_ptr_ref = ptr;
  riscv_native_load_insns++;
  return true;
}

static void riscv_note_thumb_native_stat(u32 opcode)
{
  u32 hi = opcode >> 8;

  if ((hi >= 0x48u && hi <= 0x9fu) ||
      hi == 0xbcu || hi == 0xbdu ||
      (hi >= 0xc8u && hi <= 0xcfu))
  {
    riscv_native_load_insns++;
  }
  else if ((hi >= 0x50u && hi <= 0x97u) ||
           hi == 0xb4u || hi == 0xb5u ||
           (hi >= 0xc0u && hi <= 0xc7u))
  {
    riscv_native_store_insns++;
  }
  else if ((hi >= 0xd0u && hi <= 0xdfu) ||
           (hi >= 0xe0u && hi <= 0xe7u) ||
           (hi >= 0xf8u && hi <= 0xffu) ||
           hi == 0x47u)
  {
    riscv_native_branch_insns++;
  }
  else
  {
    riscv_native_data_proc_insns++;
  }
}

static void riscv_emit_thumb_cpsr_store_mov_imm_nz(u8 **ptr_ref,
                                                   bool zero_result)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t4, REG_CPSR);
  translation_ptr = ptr;
  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 2);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 2);
  if (zero_result)
  {
    riscv_emit_lui(riscv_reg_t6, 0x40000u);
    riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

static void riscv_emit_thumb_cpsr_store_nz_preserve_cv(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t4, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 2);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 2);
  riscv_emit_srli(riscv_reg_t5, riscv_reg_t2, 31);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t2, 1);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

static void riscv_emit_thumb_cpsr_store_nzc_preserve_v(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t4, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 3);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 3);
  riscv_emit_srli(riscv_reg_t5, riscv_reg_t2, 31);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t2, 1);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 29);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t3);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

static void riscv_emit_thumb_cpsr_store_nzc_preserve_v_loaded(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 3);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 3);
  riscv_emit_srli(riscv_reg_t5, riscv_reg_t2, 31);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t2, 1);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 29);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t3);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

static void riscv_emit_thumb_cpsr_store_selected_nzc_preserve_v(
  u8 **ptr_ref, u32 flag_mask)
{
  u8 *ptr;
  u8 *translation_ptr;

  flag_mask &= 0x0eu;
  if (!flag_mask)
    return;

  if (flag_mask == 0x0eu)
  {
    riscv_emit_thumb_cpsr_store_nzc_preserve_v(ptr_ref);
    return;
  }

  if (flag_mask == 0x0cu)
  {
    riscv_emit_thumb_cpsr_store_nz_preserve_cv(ptr_ref);
    return;
  }

  if (!(flag_mask & 0x02u))
  {
    riscv_emit_arm_cpsr_store_selected_nzcv(ptr_ref, flag_mask,
                                           riscv_reg_t2);
    return;
  }

  ptr = *ptr_ref;
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t4, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 3);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 3);
  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, riscv_reg_t2, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
    riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t2, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
    riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  }
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t3, 29);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

static void riscv_emit_thumb_cpsr_store_selected_nzc_preserve_v_loaded(
  u8 **ptr_ref, u32 flag_mask)
{
  u8 *ptr;
  u8 *translation_ptr;

  flag_mask &= 0x0eu;
  if (!flag_mask)
    return;

  if (flag_mask == 0x0eu)
  {
    riscv_emit_thumb_cpsr_store_nzc_preserve_v_loaded(ptr_ref);
    return;
  }

  ptr = *ptr_ref;
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 3);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 3);
  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, riscv_reg_t2, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
    riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t2, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
    riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  }
  if (flag_mask & 0x02u)
  {
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t3, 29);
    riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

static bool riscv_emit_native_thumb_reg_shift_alu(u8 **translation_ptr_ref,
                                                  u32 opcode,
                                                  u32 flag_status);

bool riscv_emit_native_thumb_shift(u8 **translation_ptr_ref,
                                   riscv_jit_block_meta *meta,
                                   u32 opcode,
                                   u32 flag_status)
{
  u32 hi = opcode >> 8;
  u32 op = hi >> 3;
  u32 rd = opcode & 7u;
  u32 rs = (opcode >> 3) & 7u;
  u32 imm = (opcode >> 6) & 0x1fu;
  bool need_c = (flag_status & 0x02u) != 0;
  u32 flag_mask = flag_status & 0x0eu;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_SHIFT)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (riscv_emit_native_thumb_reg_shift_alu(translation_ptr_ref, opcode,
                                            flag_status))
    return true;

  if (hi > 0x17u)
    return false;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t2, rs);
  translation_ptr = ptr;

  switch (op)
  {
    case 0:
      if (need_c)
      {
        if (imm)
        {
          riscv_emit_srli(riscv_reg_t3, riscv_reg_t2, 32u - imm);
          riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        }
        else
        {
          ptr = translation_ptr;
          riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
          translation_ptr = ptr;
        }
      }
      if (imm)
        riscv_emit_slli(riscv_reg_t2, riscv_reg_t2, imm);
      break;

    case 1:
      if (imm)
      {
        if (need_c)
        {
          riscv_emit_srli(riscv_reg_t3, riscv_reg_t2, imm - 1u);
          riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        }
        riscv_emit_srli(riscv_reg_t2, riscv_reg_t2, imm);
      }
      else
      {
        if (need_c)
          riscv_emit_srli(riscv_reg_t3, riscv_reg_t2, 31);
        riscv_emit_add(riscv_reg_t2, riscv_reg_zero, riscv_reg_zero);
      }
      break;

    default:
      if (imm)
      {
        if (need_c)
        {
          riscv_emit_srli(riscv_reg_t3, riscv_reg_t2, imm - 1u);
          riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        }
        riscv_emit_srai(riscv_reg_t2, riscv_reg_t2, imm);
      }
      else
      {
        riscv_emit_srai(riscv_reg_t2, riscv_reg_t2, 31);
        if (need_c)
          riscv_emit_andi(riscv_reg_t3, riscv_reg_t2, 1);
      }
      break;
  }

  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);

  if (flag_mask)
    riscv_emit_thumb_cpsr_store_selected_nzc_preserve_v(&ptr, flag_mask);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

static bool riscv_emit_native_thumb_alu_flags(u8 **translation_ptr_ref,
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
  u32 arm_opcode;
  u32 generated_flag_mask = 0;
  u32 generated_flags = 0;
  bool generated_flags_known = false;

  if (hi == 0x43u && alu_op == 1u)
  {
    bool need_nz = (flag_status & 0x0cu) != 0;
    u8 *ptr = *translation_ptr_ref;
    u8 *translation_ptr;

    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rd);
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);
    translation_ptr = ptr;
    riscv_emit_mul(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
    ptr = translation_ptr;

    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);
    if (need_nz)
      riscv_emit_thumb_cpsr_store_selected_nzc_preserve_v(
        &ptr, flag_status & 0x0cu);

    *translation_ptr_ref = ptr;
    riscv_native_data_proc_insns++;
    return true;
  }

  if (hi >= 0x18u && hi <= 0x1fu)
  {
    bool subtract = (hi & 0x02u) != 0;

    arm_op = subtract ? 0x2u : 0x4u;
    arm_rn = rs;
    if (hi & 0x04u)
    {
      immediate = true;
      arm_operand2 = rn;
    }
    else
    {
      arm_operand2 = rn;
    }
  }
  else if (hi >= 0x20u && hi <= 0x27u)
  {
    arm_op = 0xdu;
    arm_rd = hi & 7u;
    immediate = true;
    arm_operand2 = imm;
  }
  else if (hi >= 0x28u && hi <= 0x2fu)
  {
    arm_op = 0xau;
    arm_rn = hi & 7u;
    immediate = true;
    arm_operand2 = imm;
    test_op = true;
  }
  else if (hi >= 0x30u && hi <= 0x3fu)
  {
    arm_op = (hi & 0x08u) ? 0x2u : 0x4u;
    arm_rn = hi & 7u;
    arm_rd = arm_rn;
    immediate = true;
    arm_operand2 = imm;
  }
  else if (hi == 0x40u)
  {
    if (alu_op > 1u)
      return false;
    arm_op = alu_op;
    arm_rn = rd;
    arm_operand2 = rs;
  }
  else if (hi == 0x41u)
  {
    if (alu_op != 1u && alu_op != 2u)
      return false;
    arm_op = alu_op == 1u ? 0x5u : 0x6u;
    arm_rn = rd;
    arm_operand2 = rs;
  }
  else if (hi == 0x42u)
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
  else if (hi == 0x43u)
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
        return false;
    }
  }
  else
  {
    return false;
  }

  arm_opcode = (0xeu << 28) | (immediate ? (1u << 25) : 0u) |
               (arm_op << 21) | (1u << 20) | (arm_rn << 16) |
               (arm_rd << 12) | arm_operand2;

  if (const_values && (flag_status & 0x0fu))
  {
    generated_flags_known = test_op ?
      riscv_arm_const_data_proc_test_flags(
        arm_opcode, 0, const_mask, const_values, &generated_flag_mask,
        &generated_flags) :
      riscv_arm_const_data_proc_flags(
        arm_opcode, 0, const_mask, const_values, known_flag_mask,
        known_flags, &generated_flag_mask, &generated_flags);
    if (generated_flags_known)
    {
      generated_flag_mask &= flag_status & 0x0fu;
      generated_flags &= generated_flag_mask;
      generated_flags_known = generated_flag_mask != 0;
    }
  }

  if (test_op)
  {
    if (clobber_dead_arithmetic_flags)
    {
      if (generated_flags_known)
        return riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags_known(
          translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false,
          NULL, generated_flag_mask, generated_flags);

      return riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags(
        translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false,
        NULL);
    }

    return riscv_emit_native_arm_data_proc_test_with_pc_ex(
      translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false,
      NULL);
  }

  if (clobber_dead_arithmetic_flags)
  {
    if (generated_flags_known)
      return riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags_known(
        translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false,
        NULL, generated_flag_mask, generated_flags);

    return riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags(
      translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false, NULL);
  }

  return riscv_emit_native_arm_data_proc_with_pc_ex(
    translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false, NULL);
}

static bool riscv_emit_native_thumb_reg_shift_alu(u8 **translation_ptr_ref,
                                                  u32 opcode,
                                                  u32 flag_status)
{
  u32 hi = opcode >> 8;
  u32 alu_op = (opcode >> 6) & 3u;
  u32 rd = opcode & 7u;
  u32 rs = (opcode >> 3) & 7u;
  bool need_c = (flag_status & 0x02u) != 0;
  u32 flag_mask = flag_status & 0x0eu;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;
  riscv_reg_number temp_reg = need_c ? riscv_reg_t5 : riscv_reg_t4;

  if (!((hi == 0x40u && alu_op >= 2u) ||
        (hi == 0x41u && (alu_op == 0u || alu_op == 3u))))
  {
    return false;
  }

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t2, rd);
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);
  if (need_c)
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t4, REG_CPSR);
    translation_ptr = ptr;
    riscv_emit_srli(riscv_reg_t3, riscv_reg_t4, 29);
    riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
    ptr = translation_ptr;
  }

  translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t1, riscv_reg_t1, 0xff);
  if (hi == 0x40u && alu_op == 2u)
  {
    if (need_c)
    {
      riscv_emit_reg_lsl_with_carry(&translation_ptr, riscv_reg_t2,
                                    riscv_reg_t2, riscv_reg_t1,
                                    riscv_reg_t3, temp_reg);
    }
    else
    {
      riscv_emit_sll(riscv_reg_t2, riscv_reg_t2, riscv_reg_t1);
      riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t1, 32);
      riscv_emit_sub(riscv_reg_t4, riscv_reg_zero, riscv_reg_t4);
      riscv_emit_and(riscv_reg_t2, riscv_reg_t2, riscv_reg_t4);
    }
  }
  else if (hi == 0x40u)
  {
    if (need_c)
    {
      riscv_emit_reg_lsr_with_carry(&translation_ptr, riscv_reg_t2,
                                    riscv_reg_t2, riscv_reg_t1,
                                    riscv_reg_t3, temp_reg);
    }
    else
    {
      riscv_emit_srl(riscv_reg_t2, riscv_reg_t2, riscv_reg_t1);
      riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t1, 32);
      riscv_emit_sub(riscv_reg_t4, riscv_reg_zero, riscv_reg_t4);
      riscv_emit_and(riscv_reg_t2, riscv_reg_t2, riscv_reg_t4);
    }
  }
  else if (alu_op == 0u)
  {
    if (need_c)
    {
      riscv_emit_reg_asr_with_carry(&translation_ptr, riscv_reg_t2,
                                    riscv_reg_t2, riscv_reg_t1,
                                    riscv_reg_t3, temp_reg);
    }
    else
    {
      riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t1, 32);
      riscv_emit_addi(riscv_reg_t4, riscv_reg_t4, -1);
      riscv_emit_or(riscv_reg_t4, riscv_reg_t1, riscv_reg_t4);
      riscv_emit_sra(riscv_reg_t2, riscv_reg_t2, riscv_reg_t4);
    }
  }
  else
  {
    riscv_reg_number neg_shift_reg = need_c ? riscv_reg_t5 : riscv_reg_t4;
    riscv_reg_number ror_temp_reg = need_c ? riscv_reg_t6 : riscv_reg_t5;

    if (need_c)
    {
      u8 *carry_done;

      riscv_emit_branch_with_source(&translation_ptr, &carry_done, 0x0,
                                    riscv_reg_t1, riscv_reg_zero, 0);
      riscv_emit_addi(ror_temp_reg, riscv_reg_t1, -1);
      riscv_emit_srl(riscv_reg_t3, riscv_reg_t2, ror_temp_reg);
      riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
      riscv_patch_local_branch(carry_done, translation_ptr);
    }

    riscv_emit_sub(neg_shift_reg, riscv_reg_zero, riscv_reg_t1);
    riscv_emit_srl(ror_temp_reg, riscv_reg_t2, riscv_reg_t1);
    riscv_emit_sll(riscv_reg_t2, riscv_reg_t2, neg_shift_reg);
    riscv_emit_or(riscv_reg_t2, riscv_reg_t2, ror_temp_reg);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);

  if (flag_mask)
  {
    if (need_c)
      riscv_emit_thumb_cpsr_store_selected_nzc_preserve_v_loaded(
        &ptr, flag_mask);
    else
      riscv_emit_thumb_cpsr_store_selected_nzc_preserve_v(&ptr, flag_mask);
  }

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

static bool riscv_emit_native_thumb_alu2(u8 **translation_ptr_ref,
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
  bool need_flags = (flag_status & 0x0fu) != 0;
  bool load_rd = true;
  riscv_reg_number result_reg = riscv_reg_t2;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (riscv_emit_native_thumb_reg_shift_alu(translation_ptr_ref, opcode,
                                            flag_status))
    return true;

  if (need_flags)
    return riscv_emit_native_thumb_alu_flags(translation_ptr_ref, meta,
                                            opcode, flag_status,
                                            clobber_dead_arithmetic_flags,
                                            const_mask, const_values,
                                            known_flag_mask, known_flags);

  if ((hi >= 0x28u && hi <= 0x2fu) ||
      (hi == 0x42u && alu_op != 1u))
  {
    *translation_ptr_ref = ptr;
    riscv_native_data_proc_insns++;
    return true;
  }

  if (hi >= 0x18u && hi <= 0x1fu)
  {
    load_rd = false;
  }
  else if (hi >= 0x20u && hi <= 0x27u)
  {
    rd = hi & 7u;
    load_rd = false;
  }
  else if (hi >= 0x30u && hi <= 0x3fu)
  {
    rd = hi & 7u;
  }
  else if (hi == 0x40u)
  {
    if (alu_op > 1u)
      return false;
  }
  else if (hi == 0x43u)
  {
    load_rd = alu_op != 3u;
  }
  else if (hi == 0x42u)
  {
    load_rd = false;
  }
  else
  {
    return false;
  }

  if ((hi >= 0x18u && hi <= 0x1fu) || hi == 0x42u)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);
  else if (hi < 0x20u || hi >= 0x40u)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);

  if (!riscv_arm_reg_mapped(rd, &result_reg, NULL))
    result_reg = riscv_reg_t2;

  if (load_rd)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rd);

  translation_ptr = ptr;
  if (hi >= 0x18u && hi <= 0x1fu)
  {
    bool subtract = (hi & 0x02u) != 0;

    if (hi & 0x04u)
    {
      if (subtract)
        riscv_emit_addi(result_reg, riscv_reg_t1, -(s32)rn);
      else
        riscv_emit_addi(result_reg, riscv_reg_t1, rn);
    }
    else
    {
      riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);
      translation_ptr = ptr;
      if (subtract)
        riscv_emit_sub(result_reg, riscv_reg_t1, riscv_reg_t0);
      else
        riscv_emit_add(result_reg, riscv_reg_t1, riscv_reg_t0);
    }
  }
  else if (hi >= 0x20u && hi <= 0x27u)
  {
    if (imm)
      riscv_emit_addi(result_reg, riscv_reg_zero, imm);
    else
      result_reg = riscv_reg_zero;
  }
  else if (hi >= 0x30u && hi <= 0x3fu)
  {
    if (hi & 0x08u)
      riscv_emit_addi(result_reg, riscv_reg_t0, -(s32)imm);
    else
      riscv_emit_addi(result_reg, riscv_reg_t0, imm);
  }
  else if (hi == 0x40u)
  {
    if (alu_op == 0u)
      riscv_emit_and(result_reg, riscv_reg_t0, riscv_reg_t1);
    else
      riscv_emit_xor(result_reg, riscv_reg_t0, riscv_reg_t1);
  }
  else if (hi == 0x42u)
  {
    riscv_emit_sub(result_reg, riscv_reg_zero, riscv_reg_t1);
  }
  else
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
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_thumb_alu(u8 **translation_ptr_ref,
                                 riscv_jit_block_meta *meta,
                                 u32 opcode,
                                 u32 flag_status)
{
  return riscv_emit_native_thumb_alu2(translation_ptr_ref, meta, opcode,
                                     flag_status, false, 0, NULL, 0, 0);
}

bool riscv_emit_native_thumb_alu_dead_flags(u8 **translation_ptr_ref,
                                            riscv_jit_block_meta *meta,
                                            u32 opcode,
                                            u32 flag_status)
{
  return riscv_emit_native_thumb_alu2(translation_ptr_ref, meta, opcode,
                                     flag_status, true, 0, NULL, 0, 0);
}

bool riscv_emit_native_thumb_alu_dead_flags_known(
  u8 **translation_ptr_ref,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 flag_status,
  u32 const_mask,
  const u32 *const_values,
  u32 known_flag_mask,
  u32 known_flags)
{
  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_ALU)
    return false;

  return riscv_emit_native_thumb_alu2(
    translation_ptr_ref, meta, opcode, flag_status, true, const_mask,
    const_values, known_flag_mask, known_flags);
}

static bool riscv_emit_native_thumb_simple_data(u8 **translation_ptr_ref,
                                                riscv_jit_block_meta *meta,
                                                u32 opcode,
                                                u32 pc,
                                                u32 cycles,
                                                bool exits)
{
  u32 hi = opcode >> 8;
  u32 rd = hi & 7u;
  u32 imm = opcode & 0xffu;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (exits)
    return false;
  if (!((hi >= 0x20u && hi <= 0x27u) ||
        (hi >= 0xa0u && hi <= 0xafu) ||
        (hi >= 0xb0u && hi <= 0xb3u)))
    return false;

  if (hi >= 0x20u && hi <= 0x27u)
  {
    riscv_reg_number mapped_rd;

    if (imm)
    {
      if (riscv_arm_reg_mapped(rd, &mapped_rd, NULL))
      {
        riscv_emit_li(&ptr, mapped_rd, imm);
        riscv_emit_arm_reg_store(&ptr, rd, mapped_rd);
      }
      else
      {
        riscv_emit_li(&ptr, riscv_reg_t2, imm);
        riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);
      }
    }
    else
    {
      if (riscv_arm_reg_mapped(rd, &mapped_rd, NULL))
      {
        riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_zero);
      }
      else
      {
        riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_zero);
      }
    }
    riscv_emit_thumb_cpsr_store_mov_imm_nz(&ptr, imm == 0);
  }
  else if (hi >= 0xa0u && hi <= 0xafu)
  {
    riscv_reg_number mapped_rd;

    if (hi < 0xa8u)
    {
      if (riscv_arm_reg_mapped(rd, &mapped_rd, NULL))
      {
        riscv_emit_guest_pc_load(&ptr, meta, mapped_rd,
                                 (pc & ~2u) + 4u + (imm * 4u));
        riscv_emit_arm_reg_store(&ptr, rd, mapped_rd);
        *translation_ptr_ref = ptr;
        return true;
      }

      riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t2,
                               (pc & ~2u) + 4u + (imm * 4u));
    }
    else
    {
      if (riscv_arm_reg_mapped(rd, &mapped_rd, NULL))
      {
        riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, REG_SP);
        translation_ptr = ptr;
        riscv_emit_addi(mapped_rd, riscv_reg_t0, (s32)(imm * 4u));
        ptr = translation_ptr;
        riscv_emit_arm_reg_store(&ptr, rd, mapped_rd);
        *translation_ptr_ref = ptr;
        return true;
      }

      riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, REG_SP);
      translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)(imm * 4u));
      ptr = translation_ptr;
    }
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);
  }
  else if (hi >= 0xb0u && hi <= 0xb3u)
  {
    s32 offset = (s32)((opcode & 0x7fu) * 4u);

    if (opcode & 0x80u)
      offset = -offset;

    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, REG_SP);
    translation_ptr = ptr;
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, offset);
    ptr = translation_ptr;
    riscv_emit_arm_reg_store(&ptr, REG_SP, riscv_reg_t0);
  }
  else
  {
    return false;
  }

  (void)cycles;
  *translation_ptr_ref = ptr;
  return true;
}

static bool riscv_emit_native_thumb_hi_add_mov(u8 **translation_ptr_ref,
                                               riscv_jit_block_meta *meta,
                                               u32 opcode,
                                               u32 pc,
                                               u32 cycles,
                                               bool exits)
{
  u32 hi = opcode >> 8;
  u32 hrs = (opcode >> 3) & 0x0fu;
  u32 hrd = ((opcode >> 4) & 0x08u) | (opcode & 0x07u);
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (exits)
  {
    if ((hi != 0x44u && hi != 0x46u) || hrd != REG_PC)
      return false;

    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, hrs, pc + 4u);
    if (hi == 0x44u)
    {
      riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t1, pc + 4u);
      translation_ptr = ptr;
      riscv_emit_add(riscv_reg_t0, riscv_reg_t1, riscv_reg_t0);
      ptr = translation_ptr;
    }

    translation_ptr = ptr;
    riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, -2);
    ptr = translation_ptr;
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    riscv_emit_adjust_cycles(&ptr, cycles);
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    riscv_emit_terminal_helper_call(&ptr, meta);

    *translation_ptr_ref = ptr;
    return true;
  }

  if ((hi != 0x44u && hi != 0x46u) || hrd == REG_PC)
    return false;

  if (hrs == REG_PC)
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t1, pc + 4u);
  }
  else
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, hrs);
  }

  if (hi == 0x44u)
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, hrd);
    translation_ptr = ptr;
    riscv_emit_add(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
    ptr = translation_ptr;
    riscv_emit_arm_reg_store(&ptr, hrd, riscv_reg_t2);
  }
  else
  {
    riscv_emit_arm_reg_store(&ptr, hrd, riscv_reg_t1);
  }

  *translation_ptr_ref = ptr;
  return true;
}

static bool riscv_emit_native_thumb_hi_cmp2(u8 **translation_ptr_ref,
                                            riscv_jit_block_meta *meta,
                                            u32 opcode,
                                            u32 pc,
                                            u32 flag_status,
                                            bool clobber_dead_arithmetic_flags)
{
  u32 hi = opcode >> 8;
  u32 hrs = (opcode >> 3) & 0x0fu;
  u32 hrd = ((opcode >> 4) & 0x08u) | (opcode & 0x07u);
  u32 generated_flag_mask = flag_status & 0x0fu;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi != 0x45u)
    return false;

  if (!generated_flag_mask)
  {
    *translation_ptr_ref = ptr;
    riscv_native_data_proc_insns++;
    return true;
  }

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, hrd, pc + 4u);
  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, meta, hrs, pc + 4u);

  translation_ptr = ptr;
  riscv_emit_sub(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
  if (generated_flag_mask & 0x03u)
  {
    riscv_emit_sltu(riscv_reg_t3, riscv_reg_t0, riscv_reg_t1);
    riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
    riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
    riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
  }
  if (generated_flag_mask & 0x01u)
  {
    riscv_emit_and(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
    riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 31);
  }
  ptr = translation_ptr;

  if (clobber_dead_arithmetic_flags)
    riscv_emit_arm_cpsr_store_arithmetic_selected_nzcv(
      &ptr, generated_flag_mask, riscv_reg_t2);
  else
    riscv_emit_arm_cpsr_store_selected_nzcv(
      &ptr, generated_flag_mask, riscv_reg_t2);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_thumb_hi_cmp(u8 **translation_ptr_ref,
                                    riscv_jit_block_meta *meta,
                                    u32 opcode,
                                    u32 pc,
                                    u32 flag_status)
{
  return riscv_emit_native_thumb_hi_cmp2(translation_ptr_ref, meta, opcode,
                                        pc, flag_status, false);
}

bool riscv_emit_native_thumb_hi_cmp_dead_flags(u8 **translation_ptr_ref,
                                               riscv_jit_block_meta *meta,
                                               u32 opcode,
                                               u32 pc,
                                               u32 flag_status)
{
  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_HI_CMP)
    return false;

  return riscv_emit_native_thumb_hi_cmp2(translation_ptr_ref, meta, opcode,
                                        pc, flag_status, true);
}

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
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (cycles_emitted)
    *cycles_emitted = false;

  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_MEMORY)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi >= 0x48u && hi <= 0x4fu)
  {
    rd = hi & 7u;
    load = true;
    mem_type = 0;
    offset = (pc & ~2u) + 4u + (imm * 4u);
    pc_relative = true;
  }
  else if (hi >= 0x50u && hi <= 0x5fu)
  {
    u32 access_type = (opcode >> 9) & 7u;

    reg_offset = true;
    switch (access_type)
    {
      case 0:
        load = false;
        mem_type = 0;
        break;
      case 1:
        load = false;
        mem_type = 1;
        break;
      case 2:
        load = false;
        mem_type = 2;
        break;
      case 3:
        load = true;
        mem_type = 3;
        break;
      case 4:
        load = true;
        mem_type = 0;
        break;
      case 5:
        load = true;
        mem_type = 1;
        break;
      case 6:
        load = true;
        mem_type = 2;
        break;
      default:
        load = true;
        mem_type = 4;
        break;
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
  else if (hi >= 0x90u && hi <= 0x9fu)
  {
    rd = hi & 7u;
    rb = REG_SP;
    load = hi >= 0x98u;
    mem_type = 0;
    offset = imm * 4u;
    sp_relative = true;
  }
  else
  {
    return false;
  }

  if (pc_relative)
  {
    riscv_emit_li(&ptr, riscv_reg_a0, offset);
  }
  else
  {
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rb);
    if (reg_offset)
    {
      riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, ro);
      translation_ptr = ptr;
      riscv_emit_add(riscv_reg_a0, riscv_reg_a0, riscv_reg_t0);
      ptr = translation_ptr;
    }
    else if (offset || sp_relative)
    {
      riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_a0, riscv_reg_a0,
                                       offset, true);
    }
  }

  riscv_emit_guest_pc_load(&ptr, meta, load ? riscv_reg_a1 : riscv_reg_a2,
                           pc + 2u);

  if (load)
  {
    switch (mem_type)
    {
      case 0:
        riscv_emit_thumb_memory_read_call(
          &ptr, RISCV_STACK_HELPER_READ32,
          (uintptr_t)RISCV_THUMB_READ32_TARGET);
        break;
      case 1:
        riscv_emit_thumb_memory_read_call(
          &ptr, RISCV_STACK_HELPER_READ16,
          (uintptr_t)RISCV_THUMB_READ16_TARGET);
        break;
      case 2:
        riscv_emit_thumb_memory_read_call(
          &ptr, RISCV_STACK_HELPER_READ8,
          (uintptr_t)RISCV_THUMB_READ8_TARGET);
        break;
      case 3:
        riscv_emit_thumb_memory_read_call(
          &ptr, RISCV_STACK_HELPER_READ8S,
          (uintptr_t)RISCV_THUMB_READS8_TARGET);
        break;
      default:
        riscv_emit_thumb_memory_read_call(
          &ptr, RISCV_STACK_HELPER_READ16S,
          (uintptr_t)RISCV_THUMB_READS16_TARGET);
        break;
    }

    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    riscv_emit_adjust_cycles(&ptr, cycles + 2u);
    riscv_native_load_insns++;
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
    riscv_native_store_insns++;
  }
  if (cycles_emitted)
    *cycles_emitted = true;

  *translation_ptr_ref = ptr;
  return true;
}

static void riscv_emit_thumb_block_initial_address(u8 **ptr_ref,
                                                   u32 rn,
                                                   s32 origin_offset,
                                                   u32 transfer_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);

  translation_ptr = ptr;
  if (origin_offset)
    riscv_emit_addi(riscv_reg_a0, riscv_reg_a0, origin_offset);
  riscv_emit_andi(riscv_reg_a0, riscv_reg_a0, -4);
  if (transfer_offset)
    riscv_emit_addi(riscv_reg_a0, riscv_reg_a0, (s32)transfer_offset);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_thumb_block_writeback(u8 **ptr_ref,
                                             u32 rn,
                                             s32 end_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);
  translation_ptr = ptr;
  if (end_offset)
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, end_offset);
  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, rn, riscv_reg_t0);

  *ptr_ref = ptr;
}

bool riscv_emit_native_thumb_block_memory(u8 **translation_ptr_ref,
                                          riscv_jit_block_meta *meta,
                                          u32 opcode,
                                          u32 pc,
                                          u32 cycles,
                                          bool *cycles_emitted)
{
  u32 hi = opcode >> 8;
  u32 reglist = opcode & 0xffu;
  bool load;
  bool predec;
  u32 rn;
  u32 low_count;
  u32 count;
  bool writeback_first;
  bool has_pc;
  bool has_lr;
  bool use_s2_cursor;
  s32 end_offset;
  s32 origin_offset;
  u8 *ptr = *translation_ptr_ref;
  u32 offset = 0;
  u32 i;

  if (cycles_emitted)
    *cycles_emitted = false;

  if (riscv_debug_disable_thumb_native &
      RISCV_DEBUG_DISABLE_THUMB_BLOCK_MEMORY)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (!((hi >= 0xb4u && hi <= 0xb5u) ||
        (hi >= 0xbcu && hi <= 0xbdu) ||
        (hi >= 0xc0u && hi <= 0xcfu)))
  {
    return false;
  }

  load = (hi == 0xbcu || hi == 0xbdu || hi >= 0xc8u);
  predec = (hi == 0xb4u || hi == 0xb5u);
  rn = (hi >= 0xc0u && hi <= 0xcfu) ? (hi & 7u) : REG_SP;

  if (hi == 0xb5u)
    reglist |= 1u << REG_LR;
  else if (hi == 0xbdu)
    reglist |= 1u << REG_PC;

  low_count = riscv_word_bit_count(reglist & 0xffu);
  count = low_count;
  if (reglist & ((1u << REG_LR) | (1u << REG_PC)))
    count++;
  has_pc = (reglist & (1u << REG_PC)) != 0;
  has_lr = (reglist & (1u << REG_LR)) != 0;

  if (load && rn < 8u && (reglist & (1u << rn)))
    return false;

  end_offset = predec ? -(s32)(count * 4u) : (s32)(count * 4u);
  writeback_first = load ||
    !(rn < 8u && (reglist & (1u << rn)) &&
      (((1u << rn) - 1u) & reglist) == 0);

  if (predec)
    origin_offset = 0;
  else if (writeback_first)
    origin_offset = -(s32)(count * 4u);
  else
    origin_offset = 0;

  use_s2_cursor = has_pc || (origin_offset ? count >= 2u : count >= 5u);
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 2u);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);

  if (writeback_first && use_s2_cursor)
    riscv_emit_arm_block_s2_writeback_cursor_init(
      &ptr, meta, rn, end_offset, origin_offset);
  else
  {
    if (writeback_first)
      riscv_emit_thumb_block_writeback(&ptr, rn, end_offset);
    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_init(&ptr, meta, rn, origin_offset);
  }

  for (i = 0; i < 8u; i++)
  {
    if (!((reglist >> i) & 1u))
      continue;

    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_load(&ptr);
    else
      riscv_emit_thumb_block_initial_address(&ptr, rn, origin_offset, offset);
    if (load)
    {
      riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_BLOCK_READ32);
      riscv_emit_arm_reg_store(&ptr, i, riscv_reg_a0);
    }
    else
    {
      riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, i);
      riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_BLOCK_STORE32);
    }

    offset += 4u;
    if (use_s2_cursor && offset < (count * 4u))
      riscv_emit_arm_block_s2_cursor_advance(&ptr);
  }

  if (has_lr)
  {
    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_load(&ptr);
    else
      riscv_emit_thumb_block_initial_address(&ptr, rn, origin_offset, offset);
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, REG_LR);
    riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_BLOCK_STORE32);
  }
  else if (has_pc)
  {
    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_load(&ptr);
    else
      riscv_emit_thumb_block_initial_address(&ptr, rn, origin_offset, offset);
    riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_BLOCK_READ32);
    {
      u8 *translation_ptr = ptr;

      riscv_emit_andi(riscv_reg_a0, riscv_reg_a0, -2);
      ptr = translation_ptr;
    }
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_a0);
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  }

  if (use_s2_cursor && !has_pc)
    riscv_emit_block_pc_base_restore(&ptr, meta);

  if (!writeback_first)
    riscv_emit_thumb_block_writeback(&ptr, rn, end_offset);

  riscv_emit_adjust_cycles(&ptr, cycles + count);
  if (cycles_emitted)
    *cycles_emitted = true;

  if (has_pc)
    riscv_emit_terminal_helper_call(&ptr, meta);
  else if (!load && count != 0u)
  {
    /* STM/PUSH helpers accumulate CPU_ALERT_SMC/IRQ/HALT just like the ARM
     * block-store path.  Stop before the next translated Thumb instruction;
     * otherwise stale RAM code or post-HALT guest state can execute before
     * the dispatcher flushes/handles the alert.  riscv_store_u32 returns the
     * accumulated state, including an alert raised before the last transfer. */
    riscv_emit_store_alert_branch(&ptr, meta);
  }

  if (load)
    riscv_native_load_insns++;
  else
    riscv_native_store_insns++;

  *translation_ptr_ref = ptr;
  return true;
}

bool riscv_emit_native_thumb_conditional_branch(u8 **translation_ptr_ref,
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
  bool condition_passed = false;

  if (branch_source)
    *branch_source = NULL;

  if (riscv_debug_disable_thumb_native &
      RISCV_DEBUG_DISABLE_THUMB_COND_BRANCH)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi < 0xd0u || hi > 0xddu)
    return false;

  riscv_emit_adjust_cycles(&ptr, cycles);

  if (riscv_debug_branch_probe_pc == pc)
  {
    u8 *translation_ptr;

    riscv_emit_li(&ptr, riscv_reg_t2,
      (u32)(uintptr_t)&riscv_debug_branch_probe_state);
    translation_ptr = ptr;
    riscv_emit_sw(riscv_reg_a3, riscv_reg_t2,
      offsetof(riscv_runtime_debug_branch_probe, r0_host));
    riscv_emit_sw(riscv_reg_a4, riscv_reg_t2,
      offsetof(riscv_runtime_debug_branch_probe, r1_host));
    riscv_emit_sw(riscv_mapped_host_regs[RISCV_MAPPED_NZCV_SLOT],
      riscv_reg_t2,
      offsetof(riscv_runtime_debug_branch_probe, nzcv_host));
    ptr = translation_ptr;
    riscv_emit_li(&ptr, riscv_reg_t0, pc);
    translation_ptr = ptr;
    riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
      offsetof(riscv_runtime_debug_branch_probe, pc));
    riscv_emit_addi(riscv_reg_t0, riscv_reg_zero, 1);
    riscv_emit_sw(riscv_reg_t0, riscv_reg_t2,
      offsetof(riscv_runtime_debug_branch_probe, valid));
    ptr = translation_ptr;
  }

  (void)known_flag_mask;
  (void)known_flags;
  (void)condition_passed;

  if (!riscv_emit_arm_condition_branch(&ptr, condition ^ 1u, 0,
                                       &branch_skip))
    return false;

  fallthrough_valid_mask = riscv_mapped_valid_mask;
  fallthrough_dirty_mask = riscv_mapped_dirty_mask;

  /* Taken path is patched to the target; fallthrough continues in this block. */
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

bool riscv_emit_native_thumb_b_patchable(u8 **translation_ptr_ref,
                                         riscv_jit_block_meta *meta,
                                         u8 **branch_source,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles,
                                         bool short_patch_site,
                                         bool flush_before_patch_site)
{
  u32 hi = opcode >> 8;
  u32 target_pc;
  u8 *ptr = *translation_ptr_ref;

  if (branch_source)
    *branch_source = NULL;

  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_B)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi < 0xe0u || hi > 0xe7u)
    return false;

  target_pc = pc + 4u + (u32)((s32)((opcode & 0x07ffu) << 21) >> 20);
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, target_pc);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_adjust_cycles(&ptr, cycles);

  /* Keep one terminal scheduler tail behind the patch site. The cycle-expired
   * path skips the external-entry reload and patch; debug dispatch can leave
   * the patch as NOPs and reach the same tail. */
  if (branch_source)
    *branch_source = riscv_emit_terminal_branch_patch_site_with_cycle_exit(
      &ptr, meta, short_patch_site, flush_before_patch_site);
  else
    (void)riscv_emit_terminal_branch_patch_site_with_cycle_exit(
      &ptr, meta, short_patch_site, flush_before_patch_site);

  meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}

bool riscv_emit_native_thumb_bx(u8 **translation_ptr_ref,
                                riscv_jit_block_meta *meta,
                                u32 opcode,
                                u32 pc,
                                u32 cycles)
{
  u32 hi = opcode >> 8;
  u32 hrs = (opcode >> 3) & 0x0fu;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_BX)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi != 0x47u)
    return false;

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, hrs, pc + 4u);
  translation_ptr = ptr;
  /* Storing REG_PC flushes dirty live flags through t0/t1.  Extract the
   * interworking bit first so a flags-producing instruction before BX cannot
   * accidentally switch a Thumb target into ARM state. */
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

bool riscv_emit_native_thumb_swi_patchable(u8 **translation_ptr_ref,
                                           riscv_jit_block_meta *meta,
                                           u8 **branch_source,
                                           u32 opcode,
                                           u32 pc,
                                           u32 cycles,
                                           bool short_patch_site)
{
  u32 hi = opcode >> 8;
  u32 swinum = opcode & 0xffu;
  u8 *ptr = *translation_ptr_ref;

  if (branch_source)
    *branch_source = NULL;

  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_SWI)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi != 0xdfu || swinum == 6u || swinum == 7u)
    return false;

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc);
  riscv_emit_stateful_c_call_stack(
    &ptr, RISCV_STACK_HELPER_EXECUTE_SWI_THUMB, true);
  riscv_emit_adjust_cycles(&ptr, cycles);

  if (idle_loop_target_pc == 0x00000008u)
    riscv_emit_cycles_set(&ptr, riscv_reg_zero);

  if (branch_source)
    *branch_source = riscv_emit_branch_patch_site_with_cycle_exit(
      &ptr, meta, 0x00000008u, short_patch_site, true);
  else
    (void)riscv_emit_branch_patch_site_with_cycle_exit(
      &ptr, meta, 0x00000008u, short_patch_site, true);

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}

static bool riscv_thumb_instruction_may_store(u32 opcode)
{
  u32 hi = opcode >> 8;

  if (hi >= 0x50u && hi <= 0x5fu)
    return ((opcode >> 9) & 7u) < 3u;
  if ((hi >= 0x60u && hi <= 0x67u) ||
      (hi >= 0x70u && hi <= 0x77u) ||
      (hi >= 0x80u && hi <= 0x87u) ||
      (hi >= 0x90u && hi <= 0x97u) ||
      hi == 0xb4u || hi == 0xb5u ||
      (hi >= 0xc0u && hi <= 0xc7u))
  {
    return true;
  }

  return false;
}

bool riscv_emit_native_thumb_instruction(u8 **translation_ptr_ref,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles,
                                         bool exits,
                                         bool *cycles_emitted)
{
  u8 *ptr = *translation_ptr_ref;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (cycles_emitted)
    *cycles_emitted = false;

  if (!(riscv_debug_disable_thumb_native &
        RISCV_DEBUG_DISABLE_THUMB_GENERIC_FAST) &&
      riscv_emit_native_thumb_simple_data(&ptr, meta, opcode, pc,
                                          cycles, exits))
  {
    *translation_ptr_ref = ptr;
    riscv_note_thumb_native_stat(opcode);
    return true;
  }

  if (!(riscv_debug_disable_thumb_native &
        RISCV_DEBUG_DISABLE_THUMB_GENERIC_FAST) &&
      riscv_emit_native_thumb_hi_add_mov(&ptr, meta, opcode, pc, cycles,
                                         exits))
  {
    *translation_ptr_ref = ptr;
    riscv_note_thumb_native_stat(opcode);
    return true;
  }

  riscv_emit_li(&ptr, riscv_reg_a0, opcode & 0xffffu);
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a1, pc);
  riscv_emit_stateful_c_call_stack(
    &ptr, RISCV_STACK_HELPER_THUMB_EXECUTE, !exits);
  riscv_emit_adjust_cycles(&ptr, cycles);
  riscv_emit_cycles_sub_reg(&ptr, riscv_reg_a0);
  if (cycles_emitted)
    *cycles_emitted = true;

  if (exits)
  {
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    riscv_emit_terminal_helper_call(&ptr, meta);
  }
  else if (riscv_thumb_instruction_may_store(opcode))
  {
    /* The helper-backed debug/deopt path has the same SMC/IRQ contract as
     * direct store emitters.  Resume at pc+2 before any following translated
     * instruction when the helper latched an alert. */
    riscv_emit_cpu_alert_branch(&ptr, meta);
  }

  *translation_ptr_ref = ptr;
  riscv_thumb_helper_insns++;
  riscv_note_thumb_native_stat(opcode);
  return true;
}

bool riscv_emit_native_thumb_load_pc_pool_const(u8 **translation_ptr_ref,
                                                riscv_jit_block_meta *meta,
                                                u32 rd,
                                                u32 value)
{
  u8 *ptr = *translation_ptr_ref;

  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_PC_POOL)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED) || rd >= 8u)
    return false;

  if (value)
  {
    riscv_reg_number mapped_rd;

    if (riscv_arm_reg_mapped(rd, &mapped_rd, NULL))
    {
      riscv_emit_guest_pc_load(&ptr, meta, mapped_rd, value);
      riscv_emit_arm_reg_store(&ptr, rd, mapped_rd);
    }
    else
    {
      riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t2, value);
      riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);
    }
  }
  else
  {
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_zero);
  }

  *translation_ptr_ref = ptr;
  riscv_native_load_insns++;
  return true;
}

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

  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_BL)
  {
    riscv_emit_li(&ptr, riscv_reg_a0, first_opcode & 0xffffu);
    riscv_emit_li(&ptr, riscv_reg_a1, second_opcode & 0xffffu);
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a2, pc);
    riscv_emit_mapped_regs_flush_dirty(&ptr);
    riscv_emit_c_call_address_raw(
      &ptr, (uintptr_t)riscv_thumb_execute_bl_pair);
    riscv_emit_test_poison_mapped_regs(&ptr, RISCV_MAPPED_REGS_MASK);
    riscv_note_c_call_clobbers_mapped_regs();
    riscv_emit_adjust_cycles(&ptr, cycles);
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    riscv_emit_terminal_helper_call_no_flush(&ptr, meta);
    *translation_ptr_ref = ptr;
    riscv_thumb_helper_insns++;
    return true;
  }

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (first_opcode < 0xf000u || first_opcode >= 0xf800u ||
      second_opcode < 0xf800u)
  {
    return false;
  }

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

bool riscv_emit_native_thumb_bl_prefix(u8 **translation_ptr_ref,
                                       riscv_jit_block_meta *meta,
                                       u32 opcode,
                                       u32 pc)
{
  u32 hi = opcode >> 8;
  s32 high_offset;
  u8 *ptr = *translation_ptr_ref;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED) ||
      hi < 0xf0u || hi >= 0xf8u)
  {
    return false;
  }

  high_offset = (s32)((opcode & 0x07ffu) << 21) >> 9;
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0,
                           pc + 4u + (u32)high_offset);
  riscv_emit_arm_reg_store(&ptr, REG_LR, riscv_reg_t0);

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}

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
  u8 *translation_ptr;

  if (riscv_debug_disable_thumb_native & RISCV_DEBUG_DISABLE_THUMB_BLH)
    return false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi < 0xf8u)
    return false;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, REG_LR);
  if (low_offset && low_offset <= 2047u)
  {
    translation_ptr = ptr;
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, (s32)low_offset);
    ptr = translation_ptr;
  }
  else if (low_offset)
  {
    riscv_emit_li(&ptr, riscv_reg_t1, low_offset);
    translation_ptr = ptr;
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
      /* PC_WRITTEN is block-wide metadata, but a conditional PC writer only
       * updates PC on its taken path and exits through its own terminal tail.
       * Any path that reaches this fallthrough tail therefore still needs the
       * sequential block-end PC, regardless of what another path emitted. */
      riscv_emit_guest_pc_load_existing_base(&ptr, meta, riscv_reg_t0,
                                             block_end_pc);
      riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);

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
  riscv_note_runtime_block_emit(block_start_pc, block_end_pc,
                                thumb_mode ? 1u : 0u,
                                (u32)(*translation_ptr - (u8 *)meta));
  if (block_start_pc < 0x00004000u &&
      (meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
  {
    riscv_bios_native_blocks_emitted++;
  }
  riscv_blocks_emitted++;
}

void init_emitter(bool must_swap)
{
  (void)must_swap;

  riscv_invalidate_indirect_lookup_cache();

  /* Helper addresses are stable between emitter resets.  The entry-setup
   * selector controls whether the stack-source table is rebuilt per entry;
   * the state-helper selector installs the independent REG_USERDEF vector. */
  if (riscv_entry_setup_optimized() || riscv_state_helpers_enabled())
  {
    riscv_init_helper_table();
  }
  if (riscv_state_helpers_enabled())
  {
    riscv_init_helper_state();
  }
  riscv_cycles_remaining = 0;
  riscv_blocks_emitted = 0;
  riscv_blocks_executed = 0;
  riscv_bios_native_blocks_emitted = 0;
  riscv_bios_native_blocks_executed = 0;
  riscv_bios_interpreter_fallbacks = 0;
  riscv_interpreter_fallbacks = 0;
  riscv_initial_lookup_fallbacks = 0;
  riscv_relookup_fallbacks = 0;
  riscv_unsupported_fallbacks = 0;
  riscv_native_data_proc_insns = 0;
  riscv_native_branch_insns = 0;
  riscv_native_load_insns = 0;
  riscv_native_store_insns = 0;
  riscv_native_psr_insns = 0;
  riscv_thumb_helper_insns = 0;
#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
  riscv_control_stub_entries = 0;
  riscv_control_direct_chain_attempts = 0;
  riscv_control_direct_chain_hits = 0;
  riscv_control_cycle_exits = 0;
  riscv_control_indirect_lookup_hits = 0;
  riscv_control_indirect_lookup_misses = 0;
  riscv_control_indirect_cache_attempts = 0;
  riscv_control_indirect_cache_hits = 0;
  riscv_control_fallthrough_lookup_hits = 0;
  riscv_control_fallthrough_lookup_misses = 0;
  riscv_control_scheduler_updates = 0;
  riscv_control_lookup_stub_entries = 0;
  riscv_control_slow_path_entries = 0;
#endif
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  riscv_perf_helper_call_sites = 0;
  riscv_perf_terminal_call_sites = 0;
  riscv_perf_mapped_flush_sites = 0;
  riscv_perf_mapped_store_ops = 0;
  riscv_perf_mapped_invalidate_sites = 0;
  riscv_perf_mapped_reload_sites = 0;
  riscv_perf_mapped_reload_ops = 0;
#endif
  riscv_cpu_alert = CPU_ALERT_NONE;
  rom_cache_watermark = RISCV_INITIAL_ROM_WATERMARK;
  init_bios_hooks();
}

void riscv_get_runtime_stats(riscv_runtime_stats *stats)
{
  if (!stats)
    return;

  stats->blocks_emitted = riscv_blocks_emitted;
  stats->blocks_executed = riscv_blocks_executed;
  stats->bios_native_blocks_emitted = riscv_bios_native_blocks_emitted;
  stats->bios_native_blocks_executed = riscv_bios_native_blocks_executed;
  stats->bios_interpreter_fallbacks = riscv_bios_interpreter_fallbacks;
  stats->interpreter_fallbacks = riscv_interpreter_fallbacks;
  stats->initial_lookup_fallbacks = riscv_initial_lookup_fallbacks;
  stats->relookup_fallbacks = riscv_relookup_fallbacks;
  stats->unsupported_fallbacks = riscv_unsupported_fallbacks;
  stats->native_data_proc_insns = riscv_native_data_proc_insns;
  stats->native_branch_insns = riscv_native_branch_insns;
  stats->native_load_insns = riscv_native_load_insns;
  stats->native_store_insns = riscv_native_store_insns;
  stats->native_psr_insns = riscv_native_psr_insns;
  stats->thumb_helper_insns = riscv_thumb_helper_insns;
#if defined(RISCV_RUNTIME_CONTROL_FLOW_COUNTERS)
  stats->control_dispatcher_entries = riscv_control_stub_entries;
  stats->control_direct_chain_attempts =
    riscv_control_direct_chain_attempts;
  stats->control_direct_chain_hits = riscv_control_direct_chain_hits;
  stats->control_cycle_exits = riscv_control_cycle_exits;
  stats->control_indirect_lookup_hits =
    riscv_control_indirect_lookup_hits;
  stats->control_indirect_lookup_misses =
    riscv_control_indirect_lookup_misses;
  stats->control_indirect_cache_attempts =
    riscv_control_indirect_cache_attempts;
  stats->control_indirect_cache_hits = riscv_control_indirect_cache_hits;
  stats->control_fallthrough_lookup_hits =
    riscv_control_fallthrough_lookup_hits;
  stats->control_fallthrough_lookup_misses =
    riscv_control_fallthrough_lookup_misses;
  stats->control_scheduler_updates = riscv_control_scheduler_updates;
  stats->control_lookup_stub_entries = riscv_control_lookup_stub_entries;
  stats->control_slow_path_entries = riscv_control_slow_path_entries;
#endif
#if defined(RISCV_RUNTIME_PERF_COUNTERS)
  stats->perf_helper_call_sites = riscv_perf_helper_call_sites;
  stats->perf_terminal_call_sites = riscv_perf_terminal_call_sites;
  stats->perf_mapped_flush_sites = riscv_perf_mapped_flush_sites;
  stats->perf_mapped_store_ops = riscv_perf_mapped_store_ops;
  stats->perf_mapped_invalidate_sites =
    riscv_perf_mapped_invalidate_sites;
  stats->perf_mapped_reload_sites = riscv_perf_mapped_reload_sites;
  stats->perf_mapped_reload_ops = riscv_perf_mapped_reload_ops;
#else
  stats->perf_helper_call_sites = 0;
  stats->perf_terminal_call_sites = 0;
  stats->perf_mapped_flush_sites = 0;
  stats->perf_mapped_store_ops = 0;
  stats->perf_mapped_invalidate_sites = 0;
  stats->perf_mapped_reload_sites = 0;
  stats->perf_mapped_reload_ops = 0;
#endif
}

u32 execute_arm_translate(u32 cycles)
{
  return execute_arm_translate_internal(cycles, &reg[0]);
}

u32 execute_arm_translate_internal(u32 cycles, void *regptr)
{
  u8 *entry_data;
  u32 pc;
  u32 thumb;
  bool optimized_entry_setup = riscv_entry_setup_optimized();
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  bool state_helpers = riscv_state_helpers_enabled();
#endif
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
  bool validated_entry_optimized =
    !riscv_runtime_perf_disable_validated_entry_opt;
#endif

  riscv_cycles_remaining = (s32)cycles;
  riscv_cpu_alert = CPU_ALERT_NONE;
  if (!optimized_entry_setup)
    clear_gamepak_stickybits();

  if (cycles == 0)
    return 0;

  entry_data = riscv_lookup_current_block(&pc, &thumb);

  if (!entry_data || entry_data == RISCV_INVALID_BLOCK_ENTRY)
  {
    riscv_interpreter_fallbacks++;
    riscv_initial_lookup_fallbacks++;
    if (pc < 0x00004000u)
      riscv_bios_interpreter_fallbacks++;
    riscv_note_runtime_fallback(RISCV_RUNTIME_FALLBACK_INITIAL_LOOKUP,
                                pc, thumb,
                                riscv_lookup_result_from_entry(entry_data),
                                cycles);
    if (optimized_entry_setup)
      clear_gamepak_stickybits();
    execute_arm(cycles);
    riscv_cycles_remaining = 0;
    return 0;
  }

#if defined(GPSP_ESP32S31_PSRAM_FAULT_TRACE) && \
    GPSP_ESP32S31_PSRAM_FAULT_TRACE
  esp32s31_psram_fault_trace_note_jit_lookup(
      pc | (thumb ? 1u : 0u), entry_data);
#endif

  if (!optimized_entry_setup)
    riscv_init_helper_table();
#if defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
  (void)riscv_enter_jit(entry_data, regptr,
                        (void *)(uintptr_t)riscv_jit_control_slow,
                        (void *)(uintptr_t)riscv_thumb_execute,
                        (void *)(uintptr_t)riscv_thumb_execute_bl_pair,
                        riscv_helper_table,
                        state_helpers ? 1u : 0u,
                        validated_entry_optimized ? 1u : 0u);
#elif defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  (void)riscv_enter_jit(entry_data, regptr,
                        (void *)(uintptr_t)riscv_jit_control_slow,
                        (void *)(uintptr_t)riscv_thumb_execute,
                        (void *)(uintptr_t)riscv_thumb_execute_bl_pair,
                        riscv_helper_table,
                        state_helpers ? 1u : 0u);
#else
  (void)riscv_enter_jit(entry_data, regptr,
                        (void *)(uintptr_t)riscv_jit_control_slow,
                        (void *)(uintptr_t)riscv_thumb_execute,
                        (void *)(uintptr_t)riscv_thumb_execute_bl_pair,
                        riscv_helper_table);
#endif

  return 0;
}
