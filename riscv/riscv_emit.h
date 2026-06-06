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

#define RISCV_BLOCK_META_BYTES 16
#define block_prologue_size RISCV_BLOCK_META_BYTES
#define RISCV_BRANCH_PATCH_BYTES 8

typedef struct riscv_jit_block_meta
{
  u32 start_pc;
  u32 end_pc;
  u32 thumb;
  u32 flags;
} riscv_jit_block_meta;

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
bool riscv_emit_native_arm_multiply(u8 **translation_ptr,
                                    riscv_jit_block_meta *meta,
                                    u32 opcode,
                                    u32 cycles);
bool riscv_emit_native_arm_multiply_long(u8 **translation_ptr,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 cycles);
bool riscv_emit_native_arm_psr(u8 **translation_ptr,
                               riscv_jit_block_meta *meta,
                               u32 opcode,
                               u32 cycles);
bool riscv_emit_native_arm_psr_with_pc(u8 **translation_ptr,
                                       riscv_jit_block_meta *meta,
                                       u32 opcode,
                                       u32 pc,
                                       u32 cycles);
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
                                       u32 cycles);
bool riscv_emit_native_arm_bl_patchable(u8 **translation_ptr,
                                        riscv_jit_block_meta *meta,
                                        u8 **branch_source,
                                        u32 opcode,
                                        u32 pc,
                                        u32 cycles);
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
                                         u32 cycles);
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
                                                u32 cycles);
bool riscv_emit_native_thumb_b_patchable(u8 **translation_ptr,
                                         riscv_jit_block_meta *meta,
                                         u8 **branch_source,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles);
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
                                           u32 cycles);
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
                             u32 cycles);

u32 execute_arm_translate(u32 cycles);
u32 execute_arm_translate_internal(u32 cycles, void *regptr);
void init_emitter(bool must_swap);
void riscv_get_runtime_stats(riscv_runtime_stats *stats);
void riscv_note_runtime_block_execute(u32 start_pc, u32 end_pc, u32 thumb);
void riscv_note_runtime_fallback(u32 kind, u32 pc, u32 thumb,
                                 u32 lookup_result,
                                 u32 cycles_remaining);
void riscv_patch_unconditional_branch(u8 *source, const u8 *target);
void riscv_patch_conditional_branch(u8 *source, const u8 *target);

#define generate_block_extra_vars()                                           \
  riscv_jit_block_meta *riscv_block_meta = NULL

#define generate_block_extra_vars_arm()                                       \
  generate_block_extra_vars()

#define generate_block_extra_vars_thumb()                                     \
  generate_block_extra_vars()

#define generate_block_prologue()                                             \
  do                                                                          \
  {                                                                           \
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
                                  cycle_count))                               \
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

#define arm_data_proc(...)                                                    \
  do                                                                          \
  {                                                                           \
    bool riscv_arm_cycles_emitted = false;                                    \
    if (riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags(                \
          &translation_ptr, riscv_block_meta, riscv_arm_effective_opcode(),   \
          pc, cycle_count, flag_status, riscv_arm_emit_cycles_here(),         \
          &riscv_arm_cycles_emitted))                                         \
    {                                                                         \
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
    if (riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags(           \
          &translation_ptr, riscv_block_meta, riscv_arm_effective_opcode(),   \
          pc, cycle_count, flag_status, riscv_arm_emit_cycles_here(),         \
          &riscv_arm_cycles_emitted))                                         \
    {                                                                         \
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
    if (riscv_emit_native_arm_multiply(&translation_ptr,                     \
                                       riscv_block_meta,                     \
                                       riscv_arm_effective_opcode(),         \
                                       cycle_count +                         \
                                         riscv_multiply_extra_cycles))        \
    {                                                                         \
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
    if (riscv_emit_native_arm_multiply_long(&translation_ptr,                \
                                            riscv_block_meta,                \
                                            riscv_arm_effective_opcode(),    \
                                            cycle_count +                    \
                                              riscv_multiply_long_extra_cycles)) \
    {                                                                         \
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
    if (riscv_emit_native_arm_psr_with_pc(&translation_ptr,                   \
                                          riscv_block_meta,                  \
                                          riscv_arm_effective_opcode(),       \
                                          pc, cycle_count))                   \
    {                                                                         \
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
    if (riscv_emit_native_arm_access_memory_ex(                               \
          &translation_ptr, riscv_block_meta, riscv_arm_effective_opcode(),   \
          pc, cycle_count, riscv_arm_emit_cycles_here(),                      \
          &riscv_arm_cycles_emitted))                                         \
    {                                                                         \
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
    if (riscv_emit_native_arm_block_memory(&translation_ptr,                  \
                                           riscv_block_meta,                 \
                                           riscv_arm_effective_opcode(),      \
                                           pc, cycle_count))                  \
    {                                                                         \
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
    if (riscv_emit_native_arm_swap(&translation_ptr, riscv_block_meta,        \
                                   riscv_arm_effective_opcode(),             \
                                   pc, cycle_count))                          \
    {                                                                         \
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
    if (riscv_emit_native_arm_b_patchable(                                    \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          riscv_arm_effective_opcode(), pc, cycle_count))                     \
    {                                                                         \
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
    if (riscv_emit_native_arm_bl_patchable(                                   \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          riscv_arm_effective_opcode(), pc, cycle_count))                     \
    {                                                                         \
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
    if (riscv_emit_native_arm_swi_patchable(                                  \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          riscv_arm_effective_opcode(), pc, cycle_count))                     \
    {                                                                         \
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
    if (!riscv_emit_native_thumb_alu_dead_flags(                              \
          &translation_ptr, riscv_block_meta, opcode, flag_status))           \
    {                                                                         \
      riscv_emit_thumb_instruction(false);                                    \
    }                                                                         \
  } while (0)

#define thumb_data_proc_test(...)                                             \
  do                                                                          \
  {                                                                           \
    if (!riscv_emit_native_thumb_alu_dead_flags(                              \
          &translation_ptr, riscv_block_meta, opcode, flag_status))           \
    {                                                                         \
      riscv_emit_thumb_instruction(false);                                    \
    }                                                                         \
  } while (0)

#define thumb_data_proc_unary(...)                                            \
  do                                                                          \
  {                                                                           \
    if (!riscv_emit_native_thumb_alu_dead_flags(                              \
          &translation_ptr, riscv_block_meta, opcode, flag_status))           \
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
    if (riscv_emit_native_thumb_conditional_branch(                           \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          opcode, pc, cycle_count))                                           \
    {                                                                         \
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
    if (riscv_emit_native_thumb_b_patchable(                                  \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          opcode, pc, cycle_count))                                           \
    {                                                                         \
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
    if (riscv_emit_native_thumb_swi_patchable(                                \
          &translation_ptr, riscv_block_meta,                                 \
          &block_exits[block_exit_position].branch_source,                   \
          opcode, pc, cycle_count))                                           \
    {                                                                         \
      block_exit_position++;                                                  \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_thumb_instruction(true);                                     \
    }                                                                         \
  } while (0)

#endif
