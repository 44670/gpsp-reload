/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef XTENSA_EMIT_H
#define XTENSA_EMIT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp32s3/xtensa_codegen.h"

#define block_prologue_size XTENSA_BLOCK_LITERAL_BYTES

void xtensa_emit_block_prologue(u8 **translation_ptr, u8 **literal_base);
void xtensa_emit_block_finalize(u8 *literal_base, u8 **translation_ptr,
                                u32 block_start_pc, u32 block_end_pc,
                                bool thumb_mode);
u32 execute_arm_translate(u32 cycles);
u32 execute_arm_translate_internal(u32 cycles, void *regptr);
u32 xtensa_jit_register_arm_instruction(u32 opcode, u32 pc, bool ram_region);
u32 xtensa_jit_register_thumb_instruction(u32 opcode, u32 pc, bool ram_region);
void xtensa_jit_flush_rom_cache(void);
void xtensa_jit_flush_ram_cache(void);
u32 xtensa_jit_get_blocks_emitted(void);
u32 xtensa_jit_get_blocks_executed(void);
u32 xtensa_jit_get_compiled_arm_instructions(void);
u32 xtensa_jit_get_compiled_thumb_instructions(void);
u32 xtensa_jit_get_helper_arm_instructions_executed(void);
u32 xtensa_jit_get_helper_thumb_instructions_executed(void);
u32 xtensa_jit_get_interpreter_blocks_executed(void);
u32 xtensa_jit_get_generic_fallbacks(void);
u32 xtensa_jit_get_unsupported_opcodes(void);
u32 xtensa_jit_get_thumb_blocks(void);

#define generate_block_extra_vars()                                           \
  u8 *xtensa_block_literal_base = NULL

#define generate_block_extra_vars_arm()                                       \
  generate_block_extra_vars()

#define generate_block_extra_vars_thumb()                                     \
  generate_block_extra_vars()

#define generate_block_prologue()                                             \
  xtensa_emit_block_prologue(&translation_ptr, &xtensa_block_literal_base)

#define generate_cycle_update()                                               \
  do                                                                          \
  {                                                                           \
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

#define xtensa_block_kind_arm false
#define xtensa_block_kind_thumb true

#define generate_translation_gate(type)                                       \
  xtensa_emit_block_finalize(xtensa_block_literal_base, &translation_ptr,     \
                             block_start_pc, block_end_pc,                    \
                             xtensa_block_kind_##type)

#define xtensa_emit_current_arm_instruction()                                 \
  do                                                                          \
  {                                                                           \
    u32 xtensa_insn_index =                                                   \
      xtensa_jit_register_arm_instruction(opcode, pc, ram_region);            \
    xtensa_emit_native_arm_instruction(&translation_ptr,                      \
                                       xtensa_block_literal_base,             \
                                       xtensa_insn_index);                    \
  } while (0)

#define xtensa_emit_current_thumb_instruction()                               \
  do                                                                          \
  {                                                                           \
    u32 xtensa_insn_index =                                                \
      xtensa_jit_register_thumb_instruction(opcode, pc, ram_region);         \
    xtensa_emit_native_arm_instruction(&translation_ptr,                      \
                                       xtensa_block_literal_base,             \
                                       xtensa_insn_index);                    \
  } while (0)

#define arm_conditional_block_header()                                        \
  do                                                                          \
  {                                                                           \
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
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_data_proc_test(...)                                               \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_data_proc_unary(...)                                              \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_multiply(...)                                                     \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_multiply_long(...)                                                \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_psr(...)                                                          \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_access_memory(...)                                                \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_block_memory(...)                                                 \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_swap(...)                                                         \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_b()                                                               \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_bl()                                                              \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_bx()                                                              \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_swi()                                                             \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_arm_instruction();                                    \
  } while (0)

#define arm_hle_div(cpu_mode)                                                 \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_##cpu_mode##_instruction();                           \
  } while (0)

#define arm_hle_div_arm(cpu_mode)                                             \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_##cpu_mode##_instruction();                           \
  } while (0)

#define thumb_shift(...)                                                      \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_data_proc(...)                                                  \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_data_proc_test(...)                                             \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_data_proc_unary(...)                                            \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_data_proc_hi(...)                                               \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_data_proc_test_hi(...)                                          \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_data_proc_mov_hi()                                              \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_load_pc_pool_const(...)                                         \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_load_pc(...)                                                    \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_access_memory(...)                                              \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_load_sp(...)                                                    \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_adjust_sp(...)                                                  \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_block_memory(...)                                               \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_conditional_branch(...)                                         \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_b()                                                             \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_bl()                                                            \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_blh()                                                           \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_bx()                                                            \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

#define thumb_process_cheats()                                                \
  do                                                                          \
  {                                                                           \
  } while (0)

#define arm_process_cheats()                                                  \
  do                                                                          \
  {                                                                           \
  } while (0)

#define thumb_swi()                                                           \
  do                                                                          \
  {                                                                           \
    xtensa_emit_current_thumb_instruction();                                  \
  } while (0)

void init_emitter(bool must_swap);

#endif
