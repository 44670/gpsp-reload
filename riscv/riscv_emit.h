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

typedef struct riscv_jit_block_meta
{
  u32 start_pc;
  u32 end_pc;
  u32 thumb;
  u32 flags;
} riscv_jit_block_meta;

void riscv_emit_block_prologue(u8 **translation_ptr,
                               riscv_jit_block_meta **meta);
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
bool riscv_emit_native_arm_b(u8 **translation_ptr,
                             riscv_jit_block_meta *meta,
                             u32 opcode,
                             u32 pc,
                             u32 cycles);

u32 execute_arm_translate(u32 cycles);
u32 execute_arm_translate_internal(u32 cycles, void *regptr);
void init_emitter(bool must_swap);

#define generate_block_extra_vars()                                           \
  riscv_jit_block_meta *riscv_block_meta = NULL

#define generate_block_extra_vars_arm()                                       \
  generate_block_extra_vars()

#define generate_block_extra_vars_thumb()                                     \
  generate_block_extra_vars()

#define generate_block_prologue()                                             \
  riscv_emit_block_prologue(&translation_ptr, &riscv_block_meta)

#define generate_cycle_update()                                               \
  do                                                                          \
  {                                                                           \
    riscv_mark_block_unsupported(riscv_block_meta);                           \
  } while (0)

#define generate_branch_patch_conditional(dest, offset)                       \
  do                                                                          \
  {                                                                           \
    (void)(dest);                                                             \
    (void)(offset);                                                           \
  } while (0)

#define generate_branch_patch_unconditional(dest, offset)                     \
  do                                                                          \
  {                                                                           \
    (void)(dest);                                                             \
    (void)(offset);                                                           \
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
    riscv_mark_block_unsupported(riscv_block_meta);                           \
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

#define arm_data_proc(...)                                                    \
  do                                                                          \
  {                                                                           \
    if (riscv_emit_native_arm_data_proc(&translation_ptr,                     \
                                        riscv_block_meta, opcode,             \
                                        cycle_count))                         \
    {                                                                         \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_data_proc_test(...)                                               \
  riscv_emit_current_arm_instruction()

#define arm_data_proc_unary(...)                                              \
  arm_data_proc(__VA_ARGS__)

#define arm_multiply(...)                                                     \
  riscv_emit_current_arm_instruction()

#define arm_multiply_long(...)                                                \
  riscv_emit_current_arm_instruction()

#define arm_psr(...)                                                          \
  riscv_emit_current_arm_instruction()

#define arm_access_memory(...)                                                \
  riscv_emit_current_arm_instruction()

#define arm_block_memory(...)                                                 \
  riscv_emit_current_arm_instruction()

#define arm_swap(...)                                                         \
  riscv_emit_current_arm_instruction()

#define arm_b()                                                               \
  do                                                                          \
  {                                                                           \
    if (riscv_emit_native_arm_b(&translation_ptr, riscv_block_meta,           \
                                opcode, pc, cycle_count))                     \
    {                                                                         \
      cycle_count = 0;                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      riscv_emit_current_arm_instruction();                                   \
    }                                                                         \
  } while (0)

#define arm_bl()                                                              \
  riscv_emit_current_arm_instruction()

#define arm_bx()                                                              \
  riscv_emit_current_arm_instruction()

#define arm_swi()                                                             \
  riscv_emit_current_arm_instruction()

#define arm_hle_div(cpu_mode)                                                 \
  riscv_emit_current_##cpu_mode##_instruction()

#define arm_hle_div_arm(cpu_mode)                                             \
  riscv_emit_current_##cpu_mode##_instruction()

#define thumb_shift(...)                                                      \
  riscv_emit_current_thumb_instruction()

#define thumb_data_proc(...)                                                  \
  riscv_emit_current_thumb_instruction()

#define thumb_data_proc_test(...)                                             \
  riscv_emit_current_thumb_instruction()

#define thumb_data_proc_unary(...)                                            \
  riscv_emit_current_thumb_instruction()

#define thumb_data_proc_hi(...)                                               \
  riscv_emit_current_thumb_instruction()

#define thumb_data_proc_test_hi(...)                                          \
  riscv_emit_current_thumb_instruction()

#define thumb_data_proc_mov_hi()                                              \
  riscv_emit_current_thumb_instruction()

#define thumb_load_pc_pool_const(...)                                         \
  riscv_emit_current_thumb_instruction()

#define thumb_load_pc(...)                                                    \
  riscv_emit_current_thumb_instruction()

#define thumb_access_memory(...)                                              \
  riscv_emit_current_thumb_instruction()

#define thumb_load_sp(...)                                                    \
  riscv_emit_current_thumb_instruction()

#define thumb_adjust_sp(...)                                                  \
  riscv_emit_current_thumb_instruction()

#define thumb_block_memory(...)                                               \
  riscv_emit_current_thumb_instruction()

#define thumb_conditional_branch(...)                                         \
  riscv_emit_current_thumb_instruction()

#define thumb_b()                                                             \
  riscv_emit_current_thumb_instruction()

#define thumb_bl()                                                            \
  riscv_emit_current_thumb_instruction()

#define thumb_blh()                                                           \
  riscv_emit_current_thumb_instruction()

#define thumb_bx()                                                            \
  riscv_emit_current_thumb_instruction()

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
  riscv_emit_current_thumb_instruction()

#endif
