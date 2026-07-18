/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef RISCV_EMIT_H
#define RISCV_EMIT_H

#include <stdbool.h>
#include "riscv/riscv_codegen.h"

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

typedef char riscv_block_meta_size_check[
  (sizeof(riscv_jit_block_meta) == RISCV_BLOCK_META_BYTES) ? 1 : -1];

typedef struct riscv_runtime_stats
{
  u32 blocks_emitted;
  u32 blocks_executed;
  u32 interpreter_fallbacks;
  u32 initial_lookup_fallbacks;
  u32 relookup_fallbacks;
  u32 unsupported_fallbacks;
  u32 native_data_proc_insns;
  u32 native_branch_insns;
  u32 native_load_insns;
  u32 native_store_insns;
  u32 native_psr_insns;
  u32 thumb_helper_insns;
  /* Emission-time perf instrumentation. These remain zero unless the
   * runtime is built with RISCV_RUNTIME_PERF_COUNTERS. They count code sites
   * and state-sync operations emitted into the measured path; the perf
   * harness combines them with its deterministic execution count to report
   * dynamic totals without perturbing generated code. */
  u32 perf_helper_call_sites;
  u32 perf_terminal_call_sites;
  u32 perf_mapped_flush_sites;
  u32 perf_mapped_store_ops;
  u32 perf_mapped_invalidate_sites;
  u32 perf_mapped_reload_sites;
  u32 perf_mapped_reload_ops;
} riscv_runtime_stats;

typedef enum riscv_runtime_fallback_kind
{
  RISCV_RUNTIME_FALLBACK_INITIAL_LOOKUP = 1,
  RISCV_RUNTIME_FALLBACK_RELOOKUP = 2,
  RISCV_RUNTIME_FALLBACK_UNSUPPORTED = 3
} riscv_runtime_fallback_kind;

typedef enum riscv_runtime_lookup_result
{
  RISCV_RUNTIME_LOOKUP_MISS = 1,
  RISCV_RUNTIME_LOOKUP_INVALID = 2,
  RISCV_RUNTIME_LOOKUP_UNSUPPORTED = 3
} riscv_runtime_lookup_result;

void riscv_emit_block_prologue(u8 **translation_ptr,
                               riscv_jit_block_meta **meta);
void riscv_record_block_start_pc(riscv_jit_block_meta *meta,
                                 u32 block_start_pc);
void riscv_emit_block_pc_base(u8 **translation_ptr,
                              riscv_jit_block_meta *meta,
                              u32 block_start_pc);
void riscv_emit_block_finalize(riscv_jit_block_meta *meta,
                               u8 **translation_ptr,
                               u32 block_start_pc,
                               u32 block_end_pc,
                               bool thumb_mode);
void riscv_mark_block_unsupported(riscv_jit_block_meta *meta);
void riscv_mark_block_no_fallthrough(riscv_jit_block_meta *meta);
bool riscv_emit_native_arm_data_proc(u8 **translation_ptr,
                                     riscv_jit_block_meta *meta,
                                     u32 opcode,
                                     u32 cycles);
bool riscv_emit_native_arm_data_proc_with_pc(u8 **translation_ptr,
                                             riscv_jit_block_meta *meta,
                                             u32 opcode,
                                             u32 pc,
                                             u32 cycles);
bool riscv_emit_native_arm_data_proc_with_pc_ex(u8 **translation_ptr,
                                                riscv_jit_block_meta *meta,
                                                u32 opcode,
                                                u32 pc,
                                                u32 cycles,
                                                u32 flag_status,
                                                bool emit_cycles,
                                                bool *cycles_emitted);
bool riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags(
  u8 **translation_ptr,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted);
bool riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags_known(
  u8 **translation_ptr,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted,
  u32 known_flag_mask,
  u32 known_flags);
bool riscv_emit_native_arm_data_proc_test(u8 **translation_ptr,
                                          riscv_jit_block_meta *meta,
                                          u32 opcode,
                                          u32 cycles);
bool riscv_emit_native_arm_data_proc_test_with_pc(u8 **translation_ptr,
                                                  riscv_jit_block_meta *meta,
                                                  u32 opcode,
                                                  u32 pc,
                                                  u32 cycles);
bool riscv_emit_native_arm_data_proc_test_with_pc_ex(u8 **translation_ptr,
                                                     riscv_jit_block_meta *meta,
                                                     u32 opcode,
                                                     u32 pc,
                                                     u32 cycles,
                                                     u32 flag_status,
                                                     bool emit_cycles,
                                                     bool *cycles_emitted);
bool riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags(
  u8 **translation_ptr,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted);
bool riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags_known(
  u8 **translation_ptr,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  u32 flag_status,
  bool emit_cycles,
  bool *cycles_emitted,
  u32 known_flag_mask,
  u32 known_flags);
bool riscv_emit_native_arm_multiply(u8 **translation_ptr,
                                    riscv_jit_block_meta *meta,
                                    u32 opcode,
                                    u32 cycles);
bool riscv_emit_native_arm_multiply_dead_flags(u8 **translation_ptr,
                                               riscv_jit_block_meta *meta,
                                               u32 opcode,
                                               u32 cycles,
                                               u32 flag_status);
bool riscv_emit_native_arm_multiply_long(u8 **translation_ptr,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 cycles);
bool riscv_emit_native_arm_multiply_long_dead_flags(u8 **translation_ptr,
                                                    riscv_jit_block_meta *meta,
                                                    u32 opcode,
                                                    u32 cycles,
                                                    u32 flag_status);
bool riscv_emit_native_arm_psr(u8 **translation_ptr,
                               riscv_jit_block_meta *meta,
                               u32 opcode,
                               u32 cycles);
bool riscv_emit_native_arm_psr_with_pc(u8 **translation_ptr,
                                       riscv_jit_block_meta *meta,
                                       u32 opcode,
                                       u32 pc,
                                       u32 cycles);
bool riscv_emit_native_arm_psr_with_pc_ex(u8 **translation_ptr,
                                          riscv_jit_block_meta *meta,
                                          u32 opcode,
                                          u32 pc,
                                          u32 cycles,
                                          u32 flag_status);
bool riscv_emit_native_arm_b(u8 **translation_ptr,
                             riscv_jit_block_meta *meta,
                             u32 opcode,
                             u32 pc,
                             u32 cycles);
bool riscv_emit_native_arm_bl(u8 **translation_ptr,
                              riscv_jit_block_meta *meta,
                              u32 opcode,
                              u32 pc,
                              u32 cycles);
bool riscv_emit_native_arm_b_patchable(u8 **translation_ptr,
                                       riscv_jit_block_meta *meta,
                                       u8 **branch_source,
                                       u32 opcode,
                                       u32 pc,
                                       u32 cycles,
                                       bool short_patch_site,
                                       bool flush_before_patch_site);
bool riscv_emit_native_arm_bl_patchable(u8 **translation_ptr,
                                        riscv_jit_block_meta *meta,
                                        u8 **branch_source,
                                        u32 opcode,
                                        u32 pc,
                                        u32 cycles,
                                        bool short_patch_site,
                                        bool flush_before_patch_site);
bool riscv_emit_native_arm_bx(u8 **translation_ptr,
                              riscv_jit_block_meta *meta,
                              u32 opcode,
                              u32 pc,
                              u32 cycles);
bool riscv_emit_native_arm_swi(u8 **translation_ptr,
                               riscv_jit_block_meta *meta,
                               u32 opcode,
                               u32 pc,
                               u32 cycles);
bool riscv_emit_native_arm_swi_patchable(u8 **translation_ptr,
                                         riscv_jit_block_meta *meta,
                                         u8 **branch_source,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles,
                                         bool short_patch_site);
bool riscv_emit_arm_conditional_block_header(u8 **translation_ptr,
                                             riscv_jit_block_meta *meta,
                                             u32 condition,
                                             u32 cycles,
                                             u8 **branch_source);
bool riscv_emit_native_arm_hle_div(u8 **translation_ptr,
                                   riscv_jit_block_meta *meta,
                                   bool divarm,
                                   u32 cycles);
bool riscv_emit_native_arm_swap(u8 **translation_ptr,
                                riscv_jit_block_meta *meta,
                                u32 opcode,
                                u32 pc,
                                u32 cycles);
bool riscv_emit_native_arm_block_memory(u8 **translation_ptr,
                                        riscv_jit_block_meta *meta,
                                        u32 opcode,
                                        u32 pc,
                                        u32 cycles);
bool riscv_emit_native_arm_access_memory(u8 **translation_ptr,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles);
bool riscv_emit_native_arm_access_memory_ex(u8 **translation_ptr,
                                            riscv_jit_block_meta *meta,
                                            u32 opcode,
                                            u32 pc,
                                            u32 cycles,
                                            bool emit_cycles,
                                            bool *cycles_emitted);
bool riscv_emit_native_arm_access_memory_ex_const(
  u8 **translation_ptr,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 pc,
  u32 cycles,
  bool emit_cycles,
  bool *cycles_emitted,
  u32 const_mask,
  const u32 *const_values);
bool riscv_emit_native_arm_load_pc_pool_const(u8 **translation_ptr,
                                              riscv_jit_block_meta *meta,
                                              u32 rd,
                                              u32 value,
                                              u32 cycles,
                                              bool emit_cycles,
                                              bool *cycles_emitted);
bool riscv_emit_native_thumb_instruction(u8 **translation_ptr,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles,
                                         bool exits,
                                         bool *cycles_emitted);
bool riscv_emit_native_thumb_shift(u8 **translation_ptr,
                                   riscv_jit_block_meta *meta,
                                   u32 opcode,
                                   u32 flag_status);
bool riscv_emit_native_thumb_alu(u8 **translation_ptr,
                                 riscv_jit_block_meta *meta,
                                 u32 opcode,
                                 u32 flag_status);
bool riscv_emit_native_thumb_alu_dead_flags(u8 **translation_ptr,
                                            riscv_jit_block_meta *meta,
                                            u32 opcode,
                                            u32 flag_status);
bool riscv_emit_native_thumb_alu_dead_flags_known(
  u8 **translation_ptr,
  riscv_jit_block_meta *meta,
  u32 opcode,
  u32 flag_status,
  u32 const_mask,
  const u32 *const_values,
  u32 known_flag_mask,
  u32 known_flags);
bool riscv_emit_native_thumb_hi_cmp(u8 **translation_ptr,
                                    riscv_jit_block_meta *meta,
                                    u32 opcode,
                                    u32 pc,
                                    u32 flag_status);
bool riscv_emit_native_thumb_hi_cmp_dead_flags(u8 **translation_ptr,
                                               riscv_jit_block_meta *meta,
                                               u32 opcode,
                                               u32 pc,
                                               u32 flag_status);
bool riscv_emit_native_thumb_access_memory(u8 **translation_ptr,
                                           riscv_jit_block_meta *meta,
                                           u32 opcode,
                                           u32 pc,
                                           u32 cycles,
                                           bool *cycles_emitted);
bool riscv_emit_native_thumb_block_memory(u8 **translation_ptr,
                                          riscv_jit_block_meta *meta,
                                          u32 opcode,
                                          u32 pc,
                                          u32 cycles,
                                          bool *cycles_emitted);
bool riscv_emit_native_thumb_conditional_branch(u8 **translation_ptr,
                                                riscv_jit_block_meta *meta,
                                                u8 **branch_source,
                                                u32 opcode,
                                                u32 pc,
                                                u32 cycles,
                                                u32 known_flag_mask,
                                                u32 known_flags,
                                                bool short_patch_site,
                                                bool flush_before_patch_site);
bool riscv_emit_native_thumb_b_patchable(u8 **translation_ptr,
                                         riscv_jit_block_meta *meta,
                                         u8 **branch_source,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles,
                                         bool short_patch_site,
                                         bool flush_before_patch_site);
bool riscv_emit_native_thumb_bx(u8 **translation_ptr,
                                riscv_jit_block_meta *meta,
                                u32 opcode,
                                u32 pc,
                                u32 cycles);
bool riscv_emit_native_thumb_swi_patchable(u8 **translation_ptr,
                                           riscv_jit_block_meta *meta,
                                           u8 **branch_source,
                                           u32 opcode,
                                           u32 pc,
                                           u32 cycles,
                                           bool short_patch_site);
bool riscv_emit_native_thumb_load_pc_pool_const(u8 **translation_ptr,
                                                riscv_jit_block_meta *meta,
                                                u32 rd,
                                                u32 value);
bool riscv_emit_native_thumb_bl_pair(u8 **translation_ptr,
                                     riscv_jit_block_meta *meta,
                                     u32 first_opcode,
                                     u32 second_opcode,
                                     u32 pc,
                                     u32 cycles);
bool riscv_emit_native_thumb_blh(u8 **translation_ptr,
                                 riscv_jit_block_meta *meta,
                                 u32 opcode,
                                 u32 pc,
                                 u32 cycles);
bool riscv_emit_cycle_update(u8 **translation_ptr,
                             riscv_jit_block_meta *meta,
                             u32 pc,
                             u32 cycles);

u32 execute_arm_translate(u32 cycles);
u32 execute_arm_translate_internal(u32 cycles, void *regptr);
void init_emitter(bool must_swap);
void riscv_get_runtime_stats(riscv_runtime_stats *stats);
void riscv_note_runtime_block_emit(u32 start_pc, u32 end_pc, u32 thumb,
                                   u32 code_bytes);
void riscv_note_runtime_block_execute(u32 start_pc, u32 end_pc, u32 thumb);
void riscv_note_runtime_fallback(u32 kind, u32 pc, u32 thumb,
                                 u32 lookup_result,
                                 u32 cycles_remaining);
void riscv_patch_unconditional_branch(u8 *source, const u8 *target);
void riscv_patch_unconditional_branch_short(u8 *source, const u8 *target);
void riscv_patch_conditional_branch(u8 *source, const u8 *target);
void riscv_emit_arm_conditional_block_close(u8 **translation_ptr,
                                            u8 *branch_source);
void riscv_arm_const_update_data_proc(u32 opcode, u32 pc, u32 condition,
                                      u32 *const_mask, u32 *const_values);
bool riscv_arm_const_data_proc_test_flags(u32 opcode, u32 pc,
                                          u32 const_mask,
                                          const u32 *const_values,
                                          u32 *flag_mask_out,
                                          u32 *flags_out);
bool riscv_arm_const_data_proc_flags(u32 opcode, u32 pc, u32 const_mask,
                                     const u32 *const_values,
                                     u32 known_flag_mask,
                                     u32 known_flags,
                                     u32 *flag_mask_out,
                                     u32 *flags_out);
bool riscv_arm_const_condition_passed(u32 flag_mask, u32 flags,
                                      u32 condition, bool *passed_out);
void riscv_arm_const_update_access_memory(u32 opcode, u32 pc,
                                          u32 *const_mask,
                                          u32 *const_values);
void riscv_arm_const_update_block_memory(u32 opcode, u32 *const_mask);
void riscv_arm_const_update_multiply(u32 opcode, u32 *const_mask);
void riscv_arm_const_update_multiply_long(u32 opcode, u32 *const_mask);
void riscv_arm_const_update_psr(u32 opcode, u32 *const_mask);
void riscv_thumb_const_update(u32 opcode,
                              u32 pc,
                              u32 flag_status,
                              bool ram_region,
                              const u8 *pc_address_block,
                              u32 *const_mask,
                              u32 *const_values,
                              u32 *known_flag_mask,
                              u32 *known_flags);

#define generate_block_extra_vars()                                           \
  riscv_jit_block_meta *riscv_block_meta = NULL;                             \
  u32 riscv_arm_const_mask = 0;                                               \
  u32 riscv_arm_const_values[16];                                             \
  u32 riscv_arm_known_flag_mask = 0;                                          \
  u32 riscv_arm_known_flags = 0;                                              \
  bool riscv_arm_skip_instruction = false

#define generate_block_extra_vars_arm()                                       \
  generate_block_extra_vars()

#define generate_block_extra_vars_thumb()                                     \
  generate_block_extra_vars()

#define generate_block_prologue()                                             \
  do                                                                          \
  {                                                                           \
    (void)riscv_arm_const_mask;                                               \
    (void)riscv_arm_const_values;                                             \
    (void)riscv_arm_known_flag_mask;                                          \
    (void)riscv_arm_known_flags;                                              \
    (void)riscv_arm_skip_instruction;                                         \
    riscv_emit_block_prologue(&translation_ptr, &riscv_block_meta);           \
    if (block_needs_pc_base)                                                  \
      riscv_emit_block_pc_base(&translation_ptr, riscv_block_meta,            \
                               block_start_pc);                               \
    else                                                                      \
      riscv_record_block_start_pc(riscv_block_meta, block_start_pc);          \
  } while (0)

#define generate_cycle_update()                                               \
  do                                                                          \
  {                                                                           \
    if (cycle_count != 0)                                                     \
    {                                                                         \
      if (riscv_emit_cycle_update(&translation_ptr, riscv_block_meta,         \
                                  pc, cycle_count))                           \
      {                                                                       \
        cycle_count = 0;                                                      \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        riscv_mark_block_unsupported(riscv_block_meta);                       \
        cycle_count = 0;                                                      \
      }                                                                       \
    }                                                                         \
  } while (0)

#define generate_branch_patch_conditional(dest, offset)                       \
  do                                                                          \
  {                                                                           \
    riscv_patch_conditional_branch((dest), (offset));                         \
  } while (0)

#define generate_branch_patch_unconditional(dest, offset)                     \
  do                                                                          \
  {                                                                           \
    riscv_patch_unconditional_branch((dest), (offset));                       \
  } while (0)

#define riscv_branch_patch_internal()                                         \
  ((block_exits[block_exit_position].branch_target >= block_start_pc) &&      \
   (block_exits[block_exit_position].branch_target < block_end_pc))

/* Internal targets are bounded by HOST_MAX_BLOCK_SIZE and fit a JAL patch.
 * External targets can occupy any point in the full ROM/RAM cache, so they
 * reserve the AUIPC/JALR form instead of constraining cache capacity. */
#define riscv_branch_patch_short()                                            \
  riscv_branch_patch_internal()

#define riscv_block_kind_arm false
#define riscv_block_kind_thumb true

#define generate_translation_gate(type)                                       \
  riscv_emit_block_finalize(riscv_block_meta, &translation_ptr,              \
                            block_start_pc, block_end_pc,                    \
                            riscv_block_kind_##type)

#define riscv_emit_current_arm_instruction()                                  \
  do                                                                          \
  {                                                                           \
    riscv_mark_block_unsupported(riscv_block_meta);                           \
    cycle_count = 0;                                                          \
  } while (0)

#define riscv_emit_current_thumb_instruction()                                \
  do                                                                          \
  {                                                                           \
    riscv_mark_block_unsupported(riscv_block_meta);                           \
    cycle_count = 0;                                                          \
  } while (0)

#define thumb_backend_post_instruction()                                      \
  riscv_thumb_const_update(opcode, pc, flag_status, ram_region,              \
                           pc_address_block, &riscv_arm_const_mask,          \
                           riscv_arm_const_values,                           \
                           &riscv_arm_known_flag_mask,                       \
                           &riscv_arm_known_flags)

#define arm_conditional_block_header()                                        \
  do                                                                          \
  {                                                                           \
    if (riscv_emit_arm_conditional_block_header(                              \
          &translation_ptr, riscv_block_meta, condition,                      \
          cycle_count, &backpatch_address))                                  \
    {                                                                         \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_mark_block_unsupported(riscv_block_meta);                         \
    }                                                                         \
  } while (0)

#define emit_trace_arm_instruction(pc)                                        \
  do                                                                          \
  {                                                                           \
    (void)(pc);                                                               \
  } while (0)

#define emit_trace_thumb_instruction(pc)                                      \
  do                                                                          \
  {                                                                           \
    (void)(pc);                                                               \
  } while (0)

#define riscv_arm_effective_opcode()                                          \
  ((condition == 0x0e) ? opcode : ((opcode & 0x0fffffffu) | 0xe0000000u))

#define riscv_arm_emit_cycles_here()                                          \
  ((pc + arm_instruction_width) == block_end_pc)

#define riscv_arm_clear_known_flags()                                         \
  do                                                                          \
  {                                                                           \
    riscv_arm_known_flag_mask = 0;                                            \
    riscv_arm_known_flags = 0;                                                \
  } while (0)

#define riscv_arm_condition_known_check()                                     \
  do                                                                          \
  {                                                                           \
    bool riscv_arm_condition_pass = false;                                    \
    riscv_arm_skip_instruction = false;                                       \
    if (((condition) & 0x0f) != 0x0e &&                                       \
        riscv_arm_const_condition_passed(                                     \
          riscv_arm_known_flag_mask, riscv_arm_known_flags,                   \
          (condition) & 0x0f, &riscv_arm_condition_pass))                     \
    {                                                                         \
      if ((last_condition & 0x0f) != 0x0e)                                    \
        riscv_emit_arm_conditional_block_close(&translation_ptr,              \
                                               backpatch_address);            \
      last_condition = 0x0e;                                                  \
      condition &= 0x0f;                                                      \
      if (!riscv_arm_condition_pass)                                          \
        riscv_arm_skip_instruction = true;                                    \
      else                                                                    \
      {                                                                       \
        opcode = (opcode & 0x0fffffffu) | 0xe0000000u;                       \
        condition = 0x0e;                                                     \
      }                                                                       \
    }                                                                         \
  } while (0)

#define arm_data_proc(...)                                                    \
  do                                                                          \
  {                                                                           \
    bool riscv_arm_cycles_emitted = false;                                    \
    u32 riscv_arm_emitted_opcode = riscv_arm_effective_opcode();             \
    u32 riscv_arm_data_flags = 0;                                             \
    u32 riscv_arm_data_flag_mask = 0;                                         \
    bool riscv_arm_data_flags_known =                                        \
      ((riscv_arm_emitted_opcode >> 20) & 1u) &&                             \
      ((condition & 0x0f) == 0x0e) &&                                        \
      riscv_arm_const_data_proc_flags(                                       \
        riscv_arm_emitted_opcode, pc, riscv_arm_const_mask,                  \
        riscv_arm_const_values, riscv_arm_known_flag_mask,                   \
        riscv_arm_known_flags, &riscv_arm_data_flag_mask,                    \
        &riscv_arm_data_flags);                                               \
    if (riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags_known(          \
          &translation_ptr, riscv_block_meta, riscv_arm_emitted_opcode,       \
          pc, cycle_count, flag_status, riscv_arm_emit_cycles_here(),         \
          &riscv_arm_cycles_emitted,                                          \
          riscv_arm_data_flags_known ? riscv_arm_data_flag_mask : 0,          \
          riscv_arm_data_flags_known ? riscv_arm_data_flags : 0))             \
    {                                                                         \
      riscv_arm_const_update_data_proc(riscv_arm_emitted_opcode, pc,          \
                                       condition, &riscv_arm_const_mask,      \
                                       riscv_arm_const_values);               \
      if ((riscv_arm_emitted_opcode >> 20) & 1u)                              \
      {                                                                       \
        if (riscv_arm_data_flags_known)                                       \
        {                                                                     \
          riscv_arm_known_flag_mask = riscv_arm_data_flag_mask;              \
          riscv_arm_known_flags = riscv_arm_data_flags;                       \
        }                                                                     \
        else                                                                  \
          riscv_arm_clear_known_flags();                                      \
      }                                                                       \
      if (riscv_arm_cycles_emitted)                                           \
        cycle_count = 0;                                                      \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_data_proc_test(...)                                               \
  do                                                                          \
  {                                                                           \
    bool riscv_arm_cycles_emitted = false;                                    \
    u32 riscv_arm_emitted_opcode = riscv_arm_effective_opcode();             \
    u32 riscv_arm_test_flags = 0;                                             \
    u32 riscv_arm_test_flag_mask = 0;                                        \
    bool riscv_arm_test_flags_known =                                        \
      ((condition & 0x0f) == 0x0e) &&                                        \
      riscv_arm_const_data_proc_test_flags(                                  \
        riscv_arm_emitted_opcode, pc, riscv_arm_const_mask,                  \
        riscv_arm_const_values, &riscv_arm_test_flag_mask,                   \
        &riscv_arm_test_flags);                                               \
    if (riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags_known(     \
          &translation_ptr, riscv_block_meta, riscv_arm_emitted_opcode,       \
          pc, cycle_count, flag_status, riscv_arm_emit_cycles_here(),         \
          &riscv_arm_cycles_emitted,                                          \
          riscv_arm_test_flags_known ? riscv_arm_test_flag_mask : 0,          \
          riscv_arm_test_flags_known ? riscv_arm_test_flags : 0))             \
    {                                                                         \
      if (riscv_arm_test_flags_known)                                         \
      {                                                                       \
        riscv_arm_known_flag_mask = riscv_arm_test_flag_mask;                \
        riscv_arm_known_flags = riscv_arm_test_flags;                         \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        riscv_arm_clear_known_flags();                                        \
      }                                                                       \
      if (riscv_arm_cycles_emitted)                                           \
        cycle_count = 0;                                                      \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_data_proc_unary(...)                                              \
  arm_data_proc(__VA_ARGS__)

#define arm_multiply(...)                                                     \
  do                                                                          \
  {                                                                           \
    u32 riscv_multiply_extra_cycles = ((opcode >> 21) & 1u) ? 3u : 2u;       \
    u32 riscv_arm_emitted_opcode = riscv_arm_effective_opcode();             \
    if (riscv_emit_native_arm_multiply_dead_flags(                            \
          &translation_ptr, riscv_block_meta, riscv_arm_emitted_opcode,       \
          cycle_count + riscv_multiply_extra_cycles, flag_status))            \
    {                                                                         \
      riscv_arm_const_update_multiply(riscv_arm_emitted_opcode,               \
                                      &riscv_arm_const_mask);                 \
      if ((riscv_arm_emitted_opcode >> 20) & 1u)                              \
        riscv_arm_clear_known_flags();                                        \
      cycle_count = (u32)(0u - riscv_multiply_extra_cycles);                 \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_multiply_long(...)                                                \
  do                                                                          \
  {                                                                           \
    u32 riscv_multiply_long_extra_cycles =                                   \
      (((opcode >> 22) & 1u) && !((opcode >> 21) & 1u)) ? 2u : 3u;           \
    u32 riscv_arm_emitted_opcode = riscv_arm_effective_opcode();             \
    if (riscv_emit_native_arm_multiply_long_dead_flags(                       \
          &translation_ptr, riscv_block_meta, riscv_arm_emitted_opcode,       \
          cycle_count + riscv_multiply_long_extra_cycles, flag_status))       \
    {                                                                         \
      riscv_arm_const_update_multiply_long(riscv_arm_emitted_opcode,          \
                                           &riscv_arm_const_mask);            \
      if ((riscv_arm_emitted_opcode >> 20) & 1u)                              \
        riscv_arm_clear_known_flags();                                        \
      cycle_count = (u32)(0u - riscv_multiply_long_extra_cycles);            \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_psr(...)                                                          \
  do                                                                          \
  {                                                                           \
    u32 riscv_arm_emitted_opcode = riscv_arm_effective_opcode();             \
    if (riscv_emit_native_arm_psr_with_pc_ex(&translation_ptr,                \
                                             riscv_block_meta,               \
                                             riscv_arm_emitted_opcode,        \
                                             pc, cycle_count, flag_status))   \
    {                                                                         \
      riscv_arm_const_update_psr(riscv_arm_emitted_opcode,                   \
                                 &riscv_arm_const_mask);                      \
      riscv_arm_clear_known_flags();                                          \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_access_memory(...)                                                \
  do                                                                          \
  {                                                                           \
    bool riscv_arm_cycles_emitted = false;                                    \
    u32 riscv_arm_emitted_opcode = riscv_arm_effective_opcode();             \
    if (riscv_emit_native_arm_access_memory_ex_const(                         \
          &translation_ptr, riscv_block_meta, riscv_arm_emitted_opcode,       \
          pc, cycle_count, riscv_arm_emit_cycles_here(),                      \
          &riscv_arm_cycles_emitted, riscv_arm_const_mask,                   \
          riscv_arm_const_values))                                            \
    {                                                                         \
      riscv_arm_const_update_access_memory(                                   \
        riscv_arm_emitted_opcode, pc, &riscv_arm_const_mask,                 \
        riscv_arm_const_values);                                              \
      if (riscv_arm_cycles_emitted)                                           \
        cycle_count = 0;                                                      \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_load_pc_pool_const(rd, value)                                     \
  do                                                                          \
  {                                                                           \
    bool riscv_arm_cycles_emitted = false;                                    \
    if (riscv_emit_native_arm_load_pc_pool_const(                             \
          &translation_ptr, riscv_block_meta, (rd), (value),                 \
          cycle_count, riscv_arm_emit_cycles_here(),                         \
          &riscv_arm_cycles_emitted))                                         \
    {                                                                         \
      if ((rd) < REG_PC)                                                      \
      {                                                                       \
        if (condition == 0x0e)                                                \
        {                                                                     \
          riscv_arm_const_values[(rd)] = (value);                             \
          riscv_arm_const_mask |= (1u << (rd));                               \
        }                                                                     \
        else                                                                  \
          riscv_arm_const_mask &= ~(1u << (rd));                              \
      }                                                                       \
      if (riscv_arm_cycles_emitted)                                           \
        cycle_count = 0;                                                      \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_block_memory(...)                                                 \
  do                                                                          \
  {                                                                           \
    u32 riscv_arm_emitted_opcode = riscv_arm_effective_opcode();             \
    if (riscv_emit_native_arm_block_memory(&translation_ptr,                  \
                                           riscv_block_meta,                 \
                                           riscv_arm_emitted_opcode,          \
                                           pc, cycle_count))                  \
    {                                                                         \
      riscv_arm_const_update_block_memory(riscv_arm_emitted_opcode,           \
                                          &riscv_arm_const_mask);             \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_swap(...)                                                         \
  do                                                                          \
  {                                                                           \
    u32 riscv_arm_emitted_opcode = riscv_arm_effective_opcode();             \
    if (riscv_emit_native_arm_swap(&translation_ptr, riscv_block_meta,        \
                                   riscv_arm_emitted_opcode,                  \
                                   pc, cycle_count))                          \
    {                                                                         \
      riscv_arm_const_mask &=                                                \
        ~(1u << ((riscv_arm_emitted_opcode >> 12) & 0x0fu));                 \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_b()                                                               \
  do                                                                          \
  {                                                                           \
    bool riscv_internal_patch = riscv_branch_patch_internal();                \
    bool riscv_short_patch = riscv_branch_patch_short();                      \
    if (riscv_emit_native_arm_b_patchable(                                    \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          riscv_arm_effective_opcode(), pc, cycle_count,                      \
          riscv_short_patch, true))                                           \
    {                                                                         \
      riscv_arm_const_mask = 0;                                               \
      riscv_arm_clear_known_flags();                                          \
      block_exits[block_exit_position].branch_patch_short =                   \
        riscv_short_patch;                                                    \
      if (!riscv_internal_patch && pc + 4u == block_end_pc)                  \
        riscv_mark_block_no_fallthrough(riscv_block_meta);                   \
      block_exit_position++;                                                  \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_bl()                                                              \
  do                                                                          \
  {                                                                           \
    bool riscv_internal_patch = riscv_branch_patch_internal();                \
    bool riscv_short_patch = riscv_branch_patch_short();                      \
    if (riscv_emit_native_arm_bl_patchable(                                   \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          riscv_arm_effective_opcode(), pc, cycle_count,                      \
          riscv_short_patch, true))                                           \
    {                                                                         \
      riscv_arm_const_mask = 0;                                               \
      riscv_arm_clear_known_flags();                                          \
      block_exits[block_exit_position].branch_patch_short =                   \
        riscv_short_patch;                                                    \
      if (!riscv_internal_patch && pc + 4u == block_end_pc)                  \
        riscv_mark_block_no_fallthrough(riscv_block_meta);                   \
      block_exit_position++;                                                  \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_bx()                                                              \
  do                                                                          \
  {                                                                           \
    if (riscv_emit_native_arm_bx(&translation_ptr, riscv_block_meta,          \
                                 riscv_arm_effective_opcode(),               \
                                 pc, cycle_count))                            \
    {                                                                         \
      riscv_arm_const_mask = 0;                                               \
      riscv_arm_clear_known_flags();                                          \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_swi()                                                             \
  do                                                                          \
  {                                                                           \
    bool riscv_short_patch = riscv_branch_patch_short();                      \
    if (riscv_emit_native_arm_swi_patchable(                                  \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          riscv_arm_effective_opcode(), pc, cycle_count,                      \
          riscv_short_patch))                                                 \
    {                                                                         \
      riscv_arm_const_mask = 0;                                               \
      riscv_arm_clear_known_flags();                                          \
      block_exits[block_exit_position].branch_patch_short =                   \
        riscv_short_patch;                                                    \
      block_exit_position++;                                                  \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define riscv_emit_arm_hle_div(divarm_value)                                  \
  do                                                                          \
  {                                                                           \
    if (riscv_emit_native_arm_hle_div(&translation_ptr,                       \
                                      riscv_block_meta,                       \
                                      (divarm_value), cycle_count))           \
    {                                                                         \
      riscv_arm_const_mask = 0;                                               \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define riscv_emit_thumb_hle_div(divarm_value)                                \
  do                                                                            \
  {                                                                             \
    if (riscv_emit_native_arm_hle_div(&translation_ptr,                         \
                                      riscv_block_meta,                         \
                                      (divarm_value), cycle_count))             \
    {                                                                           \
      cycle_count = 0;                                                          \
    }                                                                           \
    else                                                                        \
    {                                                                           \
      riscv_emit_current_thumb_instruction();                                   \
    }                                                                           \
  } while (0)

#define arm_hle_div(cpu_mode)                                                 \
  riscv_emit_##cpu_mode##_hle_div(false)

#define arm_hle_div_arm(cpu_mode)                                             \
  riscv_emit_##cpu_mode##_hle_div(true)

#define riscv_thumb_mul_frontend_extra(opcode_value)                          \
  ((((opcode_value) & 0xffc0u) == 0x4340u) ? (u32)(0u - 2u) : 0u)

#define riscv_emit_thumb_instruction(exits_value)                             \
  do                                                                          \
  {                                                                           \
    bool riscv_thumb_cycles_emitted = false;                                  \
    if (riscv_emit_native_thumb_instruction(&translation_ptr,                  \
                                            riscv_block_meta, opcode, pc,      \
                                            cycle_count, (exits_value),        \
                                            &riscv_thumb_cycles_emitted))      \
    {                                                                         \
      if (riscv_thumb_cycles_emitted)                                         \
        cycle_count = riscv_thumb_mul_frontend_extra(opcode);                 \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_thumb_instruction();                                 \
    }                                                                         \
  } while (0)

#define thumb_shift(...)                                                      \
  do                                                                          \
  {                                                                           \
    if (!riscv_emit_native_thumb_shift(&translation_ptr, riscv_block_meta,    \
                                       opcode, flag_status))                  \
    {                                                                         \
      riscv_emit_thumb_instruction(false);                                    \
    }                                                                         \
  } while (0)

#define thumb_data_proc(...)                                                  \
  do                                                                          \
  {                                                                           \
    if (!riscv_emit_native_thumb_alu_dead_flags_known(                        \
          &translation_ptr, riscv_block_meta, opcode, flag_status,            \
          riscv_arm_const_mask, riscv_arm_const_values,                       \
          riscv_arm_known_flag_mask, riscv_arm_known_flags))                  \
    {                                                                         \
      riscv_emit_thumb_instruction(false);                                    \
    }                                                                         \
  } while (0)

#define thumb_data_proc_test(...)                                             \
  do                                                                          \
  {                                                                           \
    if (!riscv_emit_native_thumb_alu_dead_flags_known(                        \
          &translation_ptr, riscv_block_meta, opcode, flag_status,            \
          riscv_arm_const_mask, riscv_arm_const_values,                       \
          riscv_arm_known_flag_mask, riscv_arm_known_flags))                  \
    {                                                                         \
      riscv_emit_thumb_instruction(false);                                    \
    }                                                                         \
  } while (0)

#define thumb_data_proc_unary(...)                                            \
  do                                                                          \
  {                                                                           \
    if (!riscv_emit_native_thumb_alu_dead_flags_known(                        \
          &translation_ptr, riscv_block_meta, opcode, flag_status,            \
          riscv_arm_const_mask, riscv_arm_const_values,                       \
          riscv_arm_known_flag_mask, riscv_arm_known_flags))                  \
    {                                                                         \
      riscv_emit_thumb_instruction(false);                                    \
    }                                                                         \
  } while (0)

#define thumb_data_proc_hi(...)                                               \
  riscv_emit_thumb_instruction(((opcode & 0x0087u) == 0x0087u))

#define thumb_data_proc_test_hi(...)                                          \
  do                                                                          \
  {                                                                           \
    if (!riscv_emit_native_thumb_hi_cmp_dead_flags(                           \
          &translation_ptr, riscv_block_meta, opcode, pc, flag_status))       \
    {                                                                         \
      riscv_emit_thumb_instruction(false);                                    \
    }                                                                         \
  } while (0)

#define thumb_data_proc_mov_hi()                                              \
  riscv_emit_thumb_instruction(((opcode & 0x0087u) == 0x0087u))

#define thumb_load_pc_pool_const(rd, value)                                   \
  do                                                                          \
  {                                                                           \
    if (!riscv_emit_native_thumb_load_pc_pool_const(                           \
          &translation_ptr, riscv_block_meta, (rd), (value)))                 \
    {                                                                         \
      riscv_emit_current_thumb_instruction();                                 \
    }                                                                         \
  } while (0)

#define thumb_load_pc(...)                                                    \
  riscv_emit_thumb_instruction(false)

#define thumb_access_memory(...)                                              \
  do                                                                          \
  {                                                                           \
    bool riscv_thumb_cycles_emitted = false;                                  \
    if (riscv_emit_native_thumb_access_memory(&translation_ptr,                \
                                              riscv_block_meta, opcode, pc,   \
                                              cycle_count,                    \
                                              &riscv_thumb_cycles_emitted))   \
    {                                                                         \
      if (riscv_thumb_cycles_emitted)                                         \
        cycle_count = 0;                                                      \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_thumb_instruction(false);                                    \
    }                                                                         \
  } while (0)

#define thumb_load_sp(...)                                                    \
  riscv_emit_thumb_instruction(false)

#define thumb_adjust_sp(...)                                                  \
  riscv_emit_thumb_instruction(false)

#define thumb_block_memory(...)                                               \
  do                                                                          \
  {                                                                           \
    bool riscv_thumb_cycles_emitted = false;                                  \
    if (riscv_emit_native_thumb_block_memory(                                 \
          &translation_ptr, riscv_block_meta, opcode, pc, cycle_count,        \
          &riscv_thumb_cycles_emitted))                                       \
    {                                                                         \
      if (riscv_thumb_cycles_emitted)                                         \
        cycle_count = 0;                                                      \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_thumb_instruction(((opcode & 0xff00u) == 0xbd00u));         \
    }                                                                         \
  } while (0)

#define thumb_conditional_branch(...)                                         \
  do                                                                          \
  {                                                                           \
    bool riscv_short_patch = riscv_branch_patch_short();                      \
    if (riscv_emit_native_thumb_conditional_branch(                           \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          opcode, pc, cycle_count, riscv_arm_known_flag_mask,                 \
          riscv_arm_known_flags, riscv_short_patch, true))                    \
    {                                                                         \
      block_exits[block_exit_position].branch_patch_short =                   \
        riscv_short_patch;                                                    \
      block_exit_position++;                                                  \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_thumb_instruction(true);                                     \
    }                                                                         \
  } while (0)

#define thumb_b()                                                             \
  do                                                                          \
  {                                                                           \
    bool riscv_short_patch = riscv_branch_patch_short();                      \
    if (riscv_emit_native_thumb_b_patchable(                                  \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          opcode, pc, cycle_count, riscv_short_patch, true))                  \
    {                                                                         \
      block_exits[block_exit_position].branch_patch_short =                   \
        riscv_short_patch;                                                    \
      block_exit_position++;                                                  \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_thumb_instruction(true);                                     \
    }                                                                         \
  } while (0)

#define thumb_bl()                                                            \
  do                                                                          \
  {                                                                           \
    if (riscv_emit_native_thumb_bl_pair(&translation_ptr, riscv_block_meta,    \
                                        last_opcode, opcode, pc, cycle_count)) \
    {                                                                         \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_thumb_instruction();                                 \
    }                                                                         \
  } while (0)

#define thumb_blh()                                                           \
  do                                                                          \
  {                                                                           \
    if (riscv_emit_native_thumb_blh(&translation_ptr, riscv_block_meta,       \
                                    opcode, pc, cycle_count))                 \
    {                                                                         \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_thumb_instruction(true);                                     \
    }                                                                         \
  } while (0)

#define thumb_bx()                                                            \
  do                                                                          \
  {                                                                           \
    if (riscv_emit_native_thumb_bx(&translation_ptr, riscv_block_meta,        \
                                   opcode, pc, cycle_count))                  \
    {                                                                         \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_thumb_instruction(true);                                     \
    }                                                                         \
  } while (0)

#define thumb_process_cheats()                                                \
  do                                                                          \
  {                                                                           \
    riscv_mark_block_unsupported(riscv_block_meta);                           \
  } while (0)

#define arm_process_cheats()                                                  \
  do                                                                          \
  {                                                                           \
    riscv_mark_block_unsupported(riscv_block_meta);                           \
  } while (0)

#define thumb_swi()                                                           \
  do                                                                          \
  {                                                                           \
    bool riscv_short_patch = riscv_branch_patch_short();                      \
    if (riscv_emit_native_thumb_swi_patchable(                                \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          opcode, pc, cycle_count, riscv_short_patch))                        \
    {                                                                         \
      block_exits[block_exit_position].branch_patch_short =                   \
        riscv_short_patch;                                                    \
      block_exit_position++;                                                  \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_thumb_instruction(true);                                     \
    }                                                                         \
  } while (0)

#endif
