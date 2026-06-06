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

typedef u8 *(*riscv_jit_block_fn)(void);

extern u32 rom_cache_watermark;

static u8 *riscv_jit_run_block(const riscv_jit_block_meta *meta);
static u32 function_cc riscv_thumb_execute(u32 opcode, u32 pc);
static void function_cc riscv_thumb_execute_bl_pair(u32 first_opcode,
                                                    u32 second_opcode,
                                                    u32 pc);

enum
{
  RISCV_HELPER_READ32 = 0,
  RISCV_HELPER_STORE32,
  RISCV_HELPER_READ8,
  RISCV_HELPER_STORE8,
  RISCV_HELPER_READ16,
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
  RISCV_HELPER_COUNT
};

enum
{
  RISCV_STACK_HELPER_READ16S = 8,
  RISCV_STACK_HELPER_EXECUTE_SPSR_RESTORE = 12,
  RISCV_STACK_HELPER_STORE_SPSR = 16,
  RISCV_STACK_HELPER_STORE_CPSR = 20,
  RISCV_STACK_HELPER_EXECUTE_SWI_ARM = 24,
  RISCV_STACK_HELPER_EXECUTE_SWI_THUMB = 28,
  RISCV_STACK_HELPER_HLE_DIV = 32,
  RISCV_STACK_HELPER_SWAP_U8 = 36,
  RISCV_STACK_HELPER_SWAP_U32 = 40,
  RISCV_STACK_HELPER_ARM_BLOCK_MEMORY = 44,
  RISCV_STACK_HELPER_THUMB_EXECUTE = 48,
  RISCV_STACK_JIT_LOOP_RETURN = 52,
  RISCV_INITIAL_ROM_WATERMARK = 16,
  RISCV_BLOCK_NATIVE_SUPPORTED = 1u,
  RISCV_BLOCK_PC_WRITTEN = 2u,
  RISCV_BLOCK_PC_BASE_EMITTED = 4u,
  RISCV_BLOCK_TERMINAL_EMITTED = 8u,
  RISCV_BLOCK_NO_FALLTHROUGH = 16u
};

#define RISCV_INVALID_BLOCK_ENTRY ((u8 *)(uintptr_t)~(uintptr_t)0)

/* Blocks may tail-jump into each other, so one outer JIT frame owns saved regs. */
#if defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 32)
u8 *riscv_enter_jit(u8 *entry_data, void *reg_base, void *run_block,
                    void *thumb_execute, void *thumb_bl_pair,
                    const void *helper_table);

__asm__(
  ".text\n"
  ".align 2\n"
  ".globl riscv_enter_jit\n"
  ".type riscv_enter_jit, @function\n"
  "riscv_enter_jit:\n"
  "  addi sp, sp, -112\n"
  "  sw ra, 108(sp)\n"
  "  sw s0, 104(sp)\n"
  "  sw s1, 100(sp)\n"
  "  sw s2, 96(sp)\n"
  "  sw s3, 92(sp)\n"
  "  sw s4, 88(sp)\n"
  "  sw s5, 84(sp)\n"
  "  sw s6, 80(sp)\n"
  "  sw s7, 76(sp)\n"
  "  sw s8, 72(sp)\n"
  "  sw s9, 68(sp)\n"
  "  sw s10, 64(sp)\n"
  "  sw s11, 60(sp)\n"
  "  mv s0, a1\n"
  "  mv s1, a2\n"
  "  mv s2, a3\n"
  "  sw a3, 48(sp)\n"
  "  lw s4, 0(a5)\n"
  "  lw s5, 4(a5)\n"
  "  lw s6, 8(a5)\n"
  "  lw s7, 12(a5)\n"
  "  lw s8, 16(a5)\n"
  "  lw s9, 20(a5)\n"
  "  lw s3, 24(a5)\n"
  "  lw t0, 28(a5)\n"
  "  sw t0, 8(sp)\n"
  "  lw t0, 32(a5)\n"
  "  sw t0, 12(sp)\n"
  "  lw t0, 36(a5)\n"
  "  sw t0, 16(sp)\n"
  "  lw t0, 40(a5)\n"
  "  sw t0, 20(sp)\n"
  "  lw t0, 44(a5)\n"
  "  sw t0, 24(sp)\n"
  "  lw t0, 48(a5)\n"
  "  sw t0, 28(sp)\n"
  "  lw t0, 52(a5)\n"
  "  sw t0, 32(sp)\n"
  "  lw t0, 56(a5)\n"
  "  sw t0, 36(sp)\n"
  "  lw t0, 60(a5)\n"
  "  sw t0, 40(sp)\n"
  "  lw t0, 64(a5)\n"
  "  sw t0, 44(sp)\n"
  "  lw s10, 68(a5)\n"
  "1:\n"
  "  beqz a0, 2f\n"
  "  addi t0, zero, -1\n"
  "  beq a0, t0, 2f\n"
  "  lw s11, 0(s10)\n"
  "  auipc t0, 0\n"
  "  addi t0, t0, 16\n"
  "  sw t0, 52(sp)\n"
  "  jalr ra, a0, 0\n"
  "  j 1b\n"
  "2:\n"
  "  lw s11, 60(sp)\n"
  "  lw s10, 64(sp)\n"
  "  lw s9, 68(sp)\n"
  "  lw s8, 72(sp)\n"
  "  lw s7, 76(sp)\n"
  "  lw s6, 80(sp)\n"
  "  lw s5, 84(sp)\n"
  "  lw s4, 88(sp)\n"
  "  lw s3, 92(sp)\n"
  "  lw s2, 96(sp)\n"
  "  lw s1, 100(sp)\n"
  "  lw s0, 104(sp)\n"
  "  lw ra, 108(sp)\n"
  "  addi sp, sp, 112\n"
  "  ret\n"
  ".size riscv_enter_jit, .-riscv_enter_jit\n");
#else
static u8 *riscv_enter_jit(u8 *entry_data, void *reg_base, void *run_block,
                           void *thumb_execute, void *thumb_bl_pair,
                           const void *helper_table)
{
  (void)reg_base;
  (void)run_block;
  (void)thumb_execute;
  (void)thumb_bl_pair;
  (void)helper_table;

  do
  {
    riscv_jit_block_fn entry = (riscv_jit_block_fn)entry_data;
    entry_data = entry();
  } while (entry_data && entry_data != RISCV_INVALID_BLOCK_ENTRY);

  return entry_data;
}
#endif

static s32 riscv_cycles_remaining;
static u32 riscv_blocks_emitted;
static u32 riscv_blocks_executed;
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
static cpu_alert_type riscv_cpu_alert;

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

static bool riscv_pc_base_delta(const riscv_jit_block_meta *meta,
                                u32 pc_value,
                                s32 *delta_out)
{
  int64_t delta;

  if (!meta)
    return false;

  delta = (int64_t)pc_value - (int64_t)meta->start_pc;
  if (delta < -2048 || delta > 2047)
    return false;

  *delta_out = (s32)delta;
  return true;
}

static void riscv_emit_guest_pc_load_ex(u8 **ptr_ref,
                                        riscv_jit_block_meta *meta,
                                        riscv_reg_number rd,
                                        u32 pc_value)
{
  s32 delta;

  if (riscv_pc_base_delta(meta, pc_value, &delta) &&
      (meta->flags & RISCV_BLOCK_PC_BASE_EMITTED))
  {
    u8 *translation_ptr = *ptr_ref;

    riscv_emit_addi(rd, riscv_reg_s2, delta);
    *ptr_ref = translation_ptr;
    return;
  }

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

static void riscv_emit_arm_reg_load(u8 **ptr, riscv_reg_number rd,
                                    u32 reg_index)
{
  u8 *translation_ptr = *ptr;

  riscv_emit_lw(rd, riscv_reg_s0, reg_index * 4u);

  *ptr = translation_ptr;
}

static void riscv_emit_arm_reg_store(u8 **ptr, u32 reg_index,
                                     riscv_reg_number rs)
{
  u8 *translation_ptr = *ptr;

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

static void riscv_emit_arm_memory_imm_offset(u8 **ptr_ref,
                                             riscv_reg_number rd,
                                             riscv_reg_number rs,
                                             u32 offset,
                                             bool up);
static bool riscv_arm_memory_reg_offset_const(u32 opcode,
                                              u32 pc,
                                              u32 *offset_out);
static void riscv_emit_arm_memory_const_offset(u8 **ptr_ref,
                                               bool pre_index,
                                               bool up,
                                               u32 const_offset,
                                               riscv_reg_number *writeback_reg);

static void riscv_emit_adjust_cycles(u8 **ptr, u32 cycles)
{
  u8 *translation_ptr;

  if (!cycles)
    return;

  translation_ptr = *ptr;
  if (cycles <= 2047u)
  {
    riscv_emit_addi(riscv_reg_s11, riscv_reg_s11, -(int)cycles);
  }
  else
  {
    *ptr = translation_ptr;
    riscv_emit_li(ptr, riscv_reg_t4, cycles);
    translation_ptr = *ptr;
    riscv_emit_sub(riscv_reg_s11, riscv_reg_s11, riscv_reg_t4);
  }

  *ptr = translation_ptr;
}

static void riscv_emit_c_call_reg(u8 **ptr, riscv_reg_number function_reg)
{
  u8 *translation_ptr = *ptr;

  riscv_emit_jalr(riscv_reg_ra, function_reg, 0);

  *ptr = translation_ptr;
}

static void riscv_emit_c_call_stack(u8 **ptr, u32 stack_offset)
{
  u8 *translation_ptr = *ptr;

  riscv_emit_lw(riscv_reg_t0, riscv_reg_sp, stack_offset);
  riscv_emit_jalr(riscv_reg_ra, riscv_reg_t0, 0);

  *ptr = translation_ptr;
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

static void riscv_patch_local_branch(u8 *source, const u8 *target);
static void riscv_emit_terminal_helper_call(u8 **ptr,
                                            riscv_jit_block_meta *meta);
static void riscv_emit_store_alert_branch(u8 **ptr_ref,
                                          riscv_jit_block_meta *meta);

static u8 *riscv_emit_unconditional_branch_patch_site(u8 **ptr_ref)
{
  u8 *translation_ptr = *ptr_ref;
  u8 *source = translation_ptr;

  riscv_emit_nop();
  riscv_emit_nop();

  *ptr_ref = translation_ptr;
  return source;
}

static u8 *riscv_emit_unconditional_branch_patch_site_short(u8 **ptr_ref)
{
  u8 *translation_ptr = *ptr_ref;
  u8 *source = translation_ptr;

  riscv_emit_nop();

  *ptr_ref = translation_ptr;
  return source;
}

void riscv_patch_unconditional_branch(u8 *source, const u8 *target)
{
  s32 offset;
  s32 upper;
  s32 lower;

  if (!source || !target)
    return;

  offset = (s32)((intptr_t)target - (intptr_t)source);
  upper = (offset + 0x800) >> 12;
  lower = offset - (upper << 12);

  ((u32 *)source)[0] =
    riscv_encode_u(riscv_opcode_auipc, riscv_reg_t6, (u32)upper);
  ((u32 *)source)[1] =
    riscv_encode_i(riscv_opcode_jalr, 0x0,
                   riscv_reg_zero, riscv_reg_t6, lower);
}

void riscv_patch_unconditional_branch_short(u8 *source, const u8 *target)
{
  s32 offset;

  if (!source || !target)
    return;

  offset = (s32)((intptr_t)target - (intptr_t)source);
  ((u32 *)source)[0] = riscv_encode_j_inst(riscv_reg_zero, offset);
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

  offset = (s32)((intptr_t)target - (intptr_t)source);
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
  offset = (s32)((intptr_t)target - (intptr_t)source);

  ((u32 *)source)[0] = riscv_encode_b_inst(funct3, rs1, rs2, offset);
}

static s32 riscv_decode_branch_offset(u32 instruction)
{
  u32 offset =
    (((instruction >> 31) & 0x01u) << 12) |
    (((instruction >> 25) & 0x3fu) << 5) |
    (((instruction >> 8) & 0x0fu) << 1) |
    (((instruction >> 7) & 0x01u) << 11);

  return (s32)(offset << 19) >> 19;
}

static u32 riscv_helper_call_size(const riscv_jit_block_meta *meta)
{
  u32 meta_value = (u32)(uintptr_t)meta;
  u32 upper;
  int lower;

  if (meta_value <= 2047u || meta_value >= 0xfffff800u)
    return 16u;

  upper = (meta_value + 0x800u) >> 12;
  lower = (int)(meta_value - (upper << 12));
  return lower ? 20u : 16u;
}

static void riscv_patch_store_alert_branches(riscv_jit_block_meta *meta,
                                             u32 branch_chain,
                                             const u8 *target)
{
  while (branch_chain)
  {
    u8 *source = ((u8 *)meta) + branch_chain;
    u32 instruction = ((u32 *)source)[0];
    s32 next = riscv_decode_branch_offset(instruction);

    riscv_patch_local_branch(source, target);
    branch_chain = (u32)next;
  }
}

static void riscv_emit_store_alert_branch(u8 **ptr_ref,
                                          riscv_jit_block_meta *meta)
{
  u8 *ptr = *ptr_ref;
  u32 source_offset;
  u32 previous_offset;
  u8 *translation_ptr;

  if (!meta)
    return;

  source_offset = (u32)(ptr - (u8 *)meta);
  previous_offset = meta->thumb;
  if (source_offset > 4094u || previous_offset > 4094u)
  {
    riscv_emit_terminal_helper_call(&ptr, meta);
    *ptr_ref = ptr;
    return;
  }

  translation_ptr = ptr;
  riscv_emit_bne(riscv_reg_a0, riscv_reg_zero, (s32)previous_offset);
  ptr = translation_ptr;
  meta->thumb = source_offset;

  *ptr_ref = ptr;
}

static void riscv_emit_cpu_alert_branch(u8 **ptr_ref,
                                        riscv_jit_block_meta *meta)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_li(&ptr, riscv_reg_a0, (u32)(uintptr_t)&riscv_cpu_alert);
  translation_ptr = ptr;
  riscv_emit_lw(riscv_reg_a0, riscv_reg_a0, 0);
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

static void riscv_emit_arm_cpsr_sign_branch(u8 **ptr_ref,
                                            u8 **branch_source,
                                            u32 flag_bit,
                                            bool set,
                                            s32 offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, REG_CPSR);
  translation_ptr = ptr;
  if (flag_bit != 31u)
    riscv_emit_slli(riscv_reg_t0, riscv_reg_t0, 31u - flag_bit);
  riscv_emit_branch_with_source(&translation_ptr, branch_source,
                                set ? 0x4 : 0x5,
                                riscv_reg_t0, riscv_reg_zero, offset);
  ptr = translation_ptr;

  *ptr_ref = ptr;
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
      riscv_emit_arm_cpsr_sign_branch(&ptr, branch_source, 30, true, offset);
      break;
    case 0x1:
      riscv_emit_arm_cpsr_sign_branch(&ptr, branch_source, 30, false, offset);
      break;
    case 0x2:
      riscv_emit_arm_cpsr_sign_branch(&ptr, branch_source, 29, true, offset);
      break;
    case 0x3:
      riscv_emit_arm_cpsr_sign_branch(&ptr, branch_source, 29, false, offset);
      break;
    case 0x4:
      riscv_emit_arm_cpsr_sign_branch(&ptr, branch_source, 31, true, offset);
      break;
    case 0x5:
      riscv_emit_arm_cpsr_sign_branch(&ptr, branch_source, 31, false, offset);
      break;
    case 0x6:
      riscv_emit_arm_cpsr_sign_branch(&ptr, branch_source, 28, true, offset);
      break;
    case 0x7:
      riscv_emit_arm_cpsr_sign_branch(&ptr, branch_source, 28, false, offset);
      break;
    case 0x8:
    case 0x9:
      riscv_emit_arm_reg_load(&ptr, riscv_reg_t2, REG_CPSR);
      translation_ptr = ptr;
      riscv_emit_srli(riscv_reg_t0, riscv_reg_t2, 29);
      riscv_emit_andi(riscv_reg_t0, riscv_reg_t0, 3);
      riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, -1);
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
      riscv_emit_arm_reg_load(&ptr, riscv_reg_t2, REG_CPSR);
      translation_ptr = ptr;
      riscv_emit_slli(riscv_reg_t1, riscv_reg_t2, 3);
      riscv_emit_xor(riscv_reg_t0, riscv_reg_t2, riscv_reg_t1);
      if (condition == 0xa)
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x5,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      else
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x4,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      ptr = translation_ptr;
      break;
    case 0xc:
    case 0xd:
      riscv_emit_arm_reg_load(&ptr, riscv_reg_t3, REG_CPSR);
      translation_ptr = ptr;
      riscv_emit_slli(riscv_reg_t0, riscv_reg_t3, 3);
      riscv_emit_xor(riscv_reg_t0, riscv_reg_t3, riscv_reg_t0);
      riscv_emit_slli(riscv_reg_t1, riscv_reg_t3, 1);
      riscv_emit_or(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
      if (condition == 0xc)
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x5,
                                      riscv_reg_t0, riscv_reg_zero,
                                      offset);
      else
        riscv_emit_branch_with_source(&translation_ptr, branch_source, 0x4,
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

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (!branch_source)
    return false;

  if (condition > 0x0du)
    return false;

  riscv_emit_adjust_cycles(&ptr, cycles);
  if (!riscv_emit_arm_condition_branch(&ptr, condition ^ 1u, 0,
                                       branch_source))
    return false;

  *translation_ptr_ref = ptr;
  return true;
}

bool riscv_emit_cycle_update(u8 **translation_ptr_ref,
                             riscv_jit_block_meta *meta,
                             u32 cycles)
{
  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  riscv_emit_adjust_cycles(translation_ptr_ref, cycles);
  return true;
}

static u32 function_cc riscv_store_u8(u32 address, u32 value)
{
  cpu_alert_type alert = write_memory8(address, (u8)value);
  riscv_cpu_alert |= alert;
  return alert;
}

static u32 function_cc riscv_store_u16(u32 address, u32 value)
{
  cpu_alert_type alert = write_memory16(address, (u16)value);
  riscv_cpu_alert |= alert;
  return alert;
}

static u32 function_cc riscv_store_u32(u32 address, u32 value)
{
  cpu_alert_type alert = write_memory32(address, value);
  riscv_cpu_alert |= alert;
  return alert;
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

static uintptr_t riscv_helper_table[RISCV_HELPER_COUNT];

static void riscv_init_helper_table(void)
{
  riscv_helper_table[RISCV_HELPER_READ32] = (uintptr_t)read_memory32;
  riscv_helper_table[RISCV_HELPER_STORE32] = (uintptr_t)riscv_store_u32;
  riscv_helper_table[RISCV_HELPER_READ8] = (uintptr_t)read_memory8;
  riscv_helper_table[RISCV_HELPER_STORE8] = (uintptr_t)riscv_store_u8;
  riscv_helper_table[RISCV_HELPER_READ16] = (uintptr_t)read_memory16;
  riscv_helper_table[RISCV_HELPER_STORE16] = (uintptr_t)riscv_store_u16;
  riscv_helper_table[RISCV_HELPER_READ8S] = (uintptr_t)read_memory8s;
  riscv_helper_table[RISCV_HELPER_READ16S] = (uintptr_t)read_memory16s;
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

static u32 function_cc riscv_swap_u8(u32 address, u32 value, u32 pc)
{
  u32 old_value;
  cpu_alert_type alert;

  reg[REG_PC] = pc;
  old_value = read_memory8(address);
  reg[REG_PC] = pc + 4u;
  alert = write_memory8(address, (u8)value);
  riscv_cpu_alert |= alert;
  return old_value;
}

static u32 function_cc riscv_swap_u32(u32 address, u32 value, u32 pc)
{
  u32 old_value;
  cpu_alert_type alert;

  reg[REG_PC] = pc;
  old_value = read_memory32(address);
  reg[REG_PC] = pc + 4u;
  alert = write_memory32(address, value);
  riscv_cpu_alert |= alert;
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

static u8 *riscv_lookup_or_fallback(void)
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
    riscv_note_runtime_fallback(RISCV_RUNTIME_FALLBACK_RELOOKUP,
                                pc, thumb,
                                riscv_lookup_result_from_entry(entry),
                                (u32)riscv_cycles_remaining);
    riscv_run_interpreter_remainder();
    return NULL;
  }

  return entry;
}

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
        riscv_cpu_alert |= write_memory32(address, reg[i]);
        address += 4u;
      }
    }

    if (reglist & (1u << REG_LR))
      riscv_cpu_alert |= write_memory32(address, reg[REG_LR]);
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
    riscv_cpu_alert |= write_memory32(address, value);
  else if (type == 1)
    riscv_cpu_alert |= write_memory16(address, (u16)value);
  else
    riscv_cpu_alert |= write_memory8(address, (u8)value);
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

static void riscv_emit_arm_cpsr_flags_store(u8 **ptr_ref,
                                            riscv_reg_number source)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, REG_CPSR);
  translation_ptr = ptr;
  riscv_emit_srli(source, source, 28);
  riscv_emit_slli(source, source, 28);
  riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, 4);
  riscv_emit_srli(riscv_reg_t1, riscv_reg_t1, 4);
  riscv_emit_or(source, source, riscv_reg_t1);
  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, REG_CPSR, source);

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
        riscv_cpu_alert |= write_memory32(address, value);
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

static void riscv_emit_helper_call(u8 **ptr, const riscv_jit_block_meta *meta)
{
  /* Terminal exit: run_block returns directly to riscv_enter_jit's loop. */
  riscv_emit_li(ptr, riscv_reg_a0, (u32)(uintptr_t)meta);
  {
    u8 *translation_ptr = *ptr;

    riscv_emit_sw(riscv_reg_s11, riscv_reg_s10, 0);
    riscv_emit_lw(riscv_reg_ra, riscv_reg_sp, RISCV_STACK_JIT_LOOP_RETURN);
    riscv_emit_jalr(riscv_reg_zero, riscv_reg_s1, 0);
    *ptr = translation_ptr;
  }
}

static void riscv_emit_terminal_helper_call(u8 **ptr,
                                            riscv_jit_block_meta *meta)
{
  riscv_emit_helper_call(ptr, meta);
  meta->flags |= RISCV_BLOCK_TERMINAL_EMITTED;
  /* Finalize overwrites end_pc; until then it tracks this tail's end. */
  meta->end_pc = (u32)(*ptr - (u8 *)meta);
}

static u8 *riscv_jit_run_block(const riscv_jit_block_meta *meta)
{
  u32 update_ret;
  cpu_alert_type alert = CPU_ALERT_NONE;

  riscv_blocks_executed++;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
  {
    u32 pc;
    u32 thumb;

    if (meta)
    {
      pc = meta->start_pc;
      thumb = meta->thumb;
    }
    else
    {
      riscv_current_lookup_state(&pc, &thumb);
    }

    riscv_interpreter_fallbacks++;
    riscv_unsupported_fallbacks++;
    riscv_note_runtime_fallback(RISCV_RUNTIME_FALLBACK_UNSUPPORTED,
                                pc, thumb,
                                RISCV_RUNTIME_LOOKUP_UNSUPPORTED,
                                (u32)riscv_cycles_remaining);
    riscv_run_interpreter_remainder();
    return NULL;
  }

  riscv_note_runtime_block_execute(meta->start_pc, meta->end_pc,
                                   meta->thumb);

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

  return riscv_lookup_or_fallback();
}

void riscv_emit_block_prologue(u8 **translation_ptr_ref,
                               riscv_jit_block_meta **meta)
{
  u8 *ptr = riscv_align_ptr(*translation_ptr_ref);

  *meta = (riscv_jit_block_meta *)(void *)ptr;
  (*meta)->start_pc = 0;
  (*meta)->end_pc = 0;
  (*meta)->thumb = 0;
  (*meta)->flags = RISCV_BLOCK_NATIVE_SUPPORTED;

  *translation_ptr_ref = ptr + block_prologue_size;
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
  if (meta)
    meta->flags |= RISCV_BLOCK_PC_BASE_EMITTED;

  riscv_emit_li(translation_ptr_ref, riscv_reg_s2, block_start_pc);
}

void riscv_mark_block_unsupported(riscv_jit_block_meta *meta)
{
  if (meta)
    meta->flags &= ~RISCV_BLOCK_NATIVE_SUPPORTED;
}

void riscv_mark_block_no_fallthrough(riscv_jit_block_meta *meta)
{
  if (meta)
    meta->flags |= RISCV_BLOCK_NO_FALLTHROUGH;
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
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, rd, REG_CPSR);
  translation_ptr = ptr;
  riscv_emit_srli(rd, rd, 29);
  riscv_emit_andi(rd, rd, 1);

  *ptr_ref = translation_ptr;
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
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 28);
  riscv_emit_srli(riscv_reg_t5, result_reg, 31);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_sltiu(riscv_reg_t5, result_reg, 1);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 29);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t3);
  riscv_emit_andi(riscv_reg_t6, riscv_reg_t6, 0xff);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

static void riscv_emit_arm_cpsr_store_nzc_preserve_v_ex(
  u8 **ptr_ref, riscv_reg_number result_reg, bool carry_is_const, u32 carry)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t4, riscv_reg_t6, 3);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 3);
  riscv_emit_srli(riscv_reg_t5, result_reg, 31);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_sltiu(riscv_reg_t5, result_reg, 1);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  if (carry_is_const)
  {
    if (carry)
    {
      riscv_emit_lui(riscv_reg_t5, 0x20000u);
      riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
    }
  }
  else
  {
    riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 29);
    riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t3);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
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

static void riscv_emit_arm_cpsr_store_nz_preserve_cv(
  u8 **ptr_ref, riscv_reg_number result_reg)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t6, riscv_reg_t6, 2);
  riscv_emit_srli(riscv_reg_t6, riscv_reg_t6, 2);
  riscv_emit_srli(riscv_reg_t5, result_reg, 31);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
  riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  riscv_emit_sltiu(riscv_reg_t5, result_reg, 1);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
  riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t6);
  *ptr_ref = ptr;
}

static void riscv_emit_arm_cpsr_store_zero_selected_nzcv(u8 **ptr_ref,
                                                         u32 flag_mask)
{
  u32 clear_mask = 0;
  u8 *ptr;
  u8 *translation_ptr;

  flag_mask &= 0x0eu;
  if (!flag_mask)
    return;

  ptr = *ptr_ref;
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  if (flag_mask == 0x0eu)
  {
    translation_ptr = ptr;
    riscv_emit_slli(riscv_reg_t6, riscv_reg_t6, 3);
    riscv_emit_srli(riscv_reg_t6, riscv_reg_t6, 3);
  }
  else if (flag_mask == 0x0cu)
  {
    translation_ptr = ptr;
    riscv_emit_slli(riscv_reg_t6, riscv_reg_t6, 2);
    riscv_emit_srli(riscv_reg_t6, riscv_reg_t6, 2);
  }
  else
  {
    if (flag_mask & 0x08u)
      clear_mask |= RISCV_CPSR_N;
    if (flag_mask & 0x02u)
      clear_mask |= RISCV_CPSR_C;

    if (clear_mask)
    {
      riscv_emit_li(&ptr, riscv_reg_t5, ~clear_mask);
      translation_ptr = ptr;
      riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
    }
    else
    {
      translation_ptr = ptr;
    }
  }

  if (flag_mask & 0x04u)
  {
    riscv_emit_lui(riscv_reg_t5, 0x40000u);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x02u)
  {
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t3, 29);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t6);
  *ptr_ref = ptr;
}

static void riscv_emit_arm_cpsr_store_selected_nzcv(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg)
{
  u32 clear_mask = 0;
  u8 *ptr;
  u8 *translation_ptr;

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

  if (flag_mask & 0x08u)
    clear_mask |= RISCV_CPSR_N;
  if (flag_mask & 0x04u)
    clear_mask |= RISCV_CPSR_Z;
  if (flag_mask & 0x02u)
    clear_mask |= RISCV_CPSR_C;
  if (flag_mask & 0x01u)
    clear_mask |= RISCV_CPSR_V;

  ptr = *ptr_ref;
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  riscv_emit_li(&ptr, riscv_reg_t5, ~clear_mask);
  translation_ptr = ptr;

  riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, result_reg, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_sltiu(riscv_reg_t5, result_reg, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x02u)
  {
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t3, 29);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x01u)
  {
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t4, 28);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t6);
  *ptr_ref = ptr;
}

static void riscv_emit_arm_cpsr_store_arithmetic_selected_nzcv(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg)
{
  u8 *ptr;
  u8 *translation_ptr;

  flag_mask &= 0x0fu;
  if (!flag_mask)
    return;

  if (flag_mask == 0x0fu)
  {
    riscv_emit_arm_cpsr_store_nzcv(ptr_ref, result_reg);
    return;
  }

  ptr = *ptr_ref;
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_andi(riscv_reg_t6, riscv_reg_t6, 0xff);
  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, result_reg, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_sltiu(riscv_reg_t5, result_reg, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x02u)
  {
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t3, 29);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x01u)
  {
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t4, 28);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t6);
  *ptr_ref = ptr;
}

static void riscv_emit_arm_cpsr_store_addsub_zero_test(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg, bool subtract)
{
  u32 clear_mask = 0;
  u8 *ptr;
  u8 *translation_ptr;

  flag_mask &= 0x0fu;
  if (!flag_mask)
    return;

  ptr = *ptr_ref;
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  if (flag_mask == 0x0fu)
  {
    translation_ptr = ptr;
    riscv_emit_andi(riscv_reg_t6, riscv_reg_t6, 0xff);
  }
  else if (flag_mask == 0x0eu)
  {
    translation_ptr = ptr;
    riscv_emit_slli(riscv_reg_t6, riscv_reg_t6, 3);
    riscv_emit_srli(riscv_reg_t6, riscv_reg_t6, 3);
  }
  else if (flag_mask == 0x0cu)
  {
    translation_ptr = ptr;
    riscv_emit_slli(riscv_reg_t6, riscv_reg_t6, 2);
    riscv_emit_srli(riscv_reg_t6, riscv_reg_t6, 2);
  }
  else
  {
    if (flag_mask & 0x08u)
      clear_mask |= RISCV_CPSR_N;
    if (flag_mask & 0x04u)
      clear_mask |= RISCV_CPSR_Z;
    if (flag_mask & 0x02u)
      clear_mask |= RISCV_CPSR_C;
    if (flag_mask & 0x01u)
      clear_mask |= RISCV_CPSR_V;

    riscv_emit_li(&ptr, riscv_reg_t5, ~clear_mask);
    translation_ptr = ptr;
    riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }

  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, result_reg, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_sltiu(riscv_reg_t5, result_reg, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if ((flag_mask & 0x02u) && subtract)
  {
    riscv_emit_lui(riscv_reg_t5, 0x20000u);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t6);
  *ptr_ref = ptr;
}

static void riscv_emit_arm_cpsr_store_addsub_zero_test_dead_flags(
  u8 **ptr_ref, u32 flag_mask, riscv_reg_number result_reg, bool subtract)
{
  u8 *ptr;
  u8 *translation_ptr;

  flag_mask &= 0x0fu;
  if (!flag_mask)
    return;

  ptr = *ptr_ref;
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_andi(riscv_reg_t6, riscv_reg_t6, 0xff);

  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, result_reg, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_sltiu(riscv_reg_t5, result_reg, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if ((flag_mask & 0x02u) && subtract)
  {
    riscv_emit_lui(riscv_reg_t5, 0x20000u);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t6);
  *ptr_ref = ptr;
}

static void riscv_emit_arm_cpsr_store_long_nzcv(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t6, riscv_reg_t6, 2);
  riscv_emit_srli(riscv_reg_t6, riscv_reg_t6, 2);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t3, 31);
  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 31);
  riscv_emit_or(riscv_reg_t5, riscv_reg_t2, riscv_reg_t3);
  riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t5, 1);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

static void riscv_emit_arm_cpsr_store_long_selected_nz(
  u8 **ptr_ref, u32 flag_mask, bool clobber_dead_flags)
{
  u8 *ptr;
  u8 *translation_ptr;

  flag_mask &= 0x0cu;
  if (!flag_mask)
    return;

  if (!clobber_dead_flags && flag_mask == 0x0cu)
  {
    riscv_emit_arm_cpsr_store_long_nzcv(ptr_ref);
    return;
  }

  ptr = *ptr_ref;
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  translation_ptr = ptr;

  if (clobber_dead_flags)
  {
    riscv_emit_andi(riscv_reg_t6, riscv_reg_t6, 0xff);
  }
  else
  {
    u32 clear_mask = 0;

    if (flag_mask & 0x08u)
      clear_mask |= RISCV_CPSR_N;
    if (flag_mask & 0x04u)
      clear_mask |= RISCV_CPSR_Z;

    riscv_emit_li(&ptr, riscv_reg_t5, ~clear_mask);
    translation_ptr = ptr;
    riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }

  if (flag_mask & 0x08u)
  {
    riscv_emit_srli(riscv_reg_t5, riscv_reg_t3, 31);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  if (flag_mask & 0x04u)
  {
    riscv_emit_or(riscv_reg_t5, riscv_reg_t2, riscv_reg_t3);
    riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t5, 1);
    riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
    riscv_emit_or(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t6);
  *ptr_ref = ptr;
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
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t0, (s32)immediate);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
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
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t0, (s32)immediate);
        riscv_emit_xori(riscv_reg_t6, riscv_reg_t2, (s32)immediate);
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
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t0, (s32)immediate);
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
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
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t0, (s32)immediate);
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
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
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t0, (s32)immediate);
        riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
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
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t0, (s32)immediate);
        riscv_emit_xori(riscv_reg_t6, riscv_reg_t2, (s32)immediate);
      }
      break;

    default:
      return false;
  }

  if (need_v)
  {
    riscv_emit_and(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
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
  bool clobber_dead_arithmetic_flags)
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

  if (logical_flags &&
      (generated_flag_mask & 0x02u) &&
      riscv_arm_operand2_preserves_carry(opcode))
  {
    generated_flag_mask &= ~0x02u;
  }
  live_flags = generated_flag_mask != 0;

  if (imm_op)
  {
    immediate = riscv_arm_expand_imm(opcode);
    if (logical_flags && generated_flag_mask == 0x0eu &&
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

  if (imm_op && immediate == 0 && (op == 0x2 || op == 0x4))
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

  if (!result_emitted && logical_flags && (generated_flag_mask & 0x02u) &&
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

  if (!result_emitted && arithmetic_flags && live_flags)
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
  if (addsub_zero_flags)
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
      riscv_emit_arm_cpsr_store_selected_nzcv(
        &ptr, generated_flag_mask, result_reg);
  }
  else if (logical_flags && live_flags)
  {
    if (logical_const_c)
      riscv_emit_arm_cpsr_store_nzc_const_c_preserve_v(
        &ptr, result_reg, logical_carry);
    else
      riscv_emit_arm_cpsr_store_selected_nzcv(
        &ptr, generated_flag_mask, result_reg);
  }
  if (writes_pc && set_flags)
    riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_EXECUTE_SPSR_RESTORE);
  if (emit_cycles || writes_pc)
  {
    riscv_emit_adjust_cycles(&ptr, cycles);
    if (cycles_emitted)
      *cycles_emitted = true;
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
    cycles_emitted, false);
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
    cycles_emitted, true);
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
  bool clobber_dead_arithmetic_flags)
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

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, rn, pc + 8u);

  if (imm_op)
  {
    u32 immediate = riscv_arm_expand_imm(opcode);

    if (logical_test && generated_flag_mask == 0x0eu &&
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
        riscv_emit_arm_cpsr_store_nzc_const_c_preserve_v(
          &ptr, riscv_reg_t2, logical_carry);
      else
        riscv_emit_arm_cpsr_store_selected_nzcv(
          &ptr, generated_flag_mask, riscv_reg_t2);
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
    cycles_emitted, false);
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
    cycles_emitted, true);
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

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0fc000f0u) != 0x00000090u ||
      rd == REG_PC || rm == REG_PC || rs == REG_PC ||
      (accumulate && rn == REG_PC))
  {
    return false;
  }

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
  {
    if (clobber_dead_flags)
      riscv_emit_arm_cpsr_store_arithmetic_selected_nzcv(
        &ptr, live_flag_mask, riscv_reg_t2);
    else
      riscv_emit_arm_cpsr_store_selected_nzcv(
        &ptr, live_flag_mask, riscv_reg_t2);
  }
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
  {
    if (clobber_dead_flags)
      riscv_emit_arm_cpsr_store_long_selected_nz(
        &ptr, live_flag_mask, true);
    else
      riscv_emit_arm_cpsr_store_long_selected_nz(
        &ptr, live_flag_mask, false);
  }
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
  return riscv_emit_native_arm_multiply_long2(translation_ptr_ref, meta,
                                             opcode, cycles, flag_status,
                                             true);
}

bool riscv_emit_native_arm_psr_with_pc(u8 **translation_ptr_ref,
                                       riscv_jit_block_meta *meta,
                                       u32 opcode,
                                       u32 pc,
                                       u32 cycles)
{
  u32 condition = opcode >> 28;
  u32 op_class = (opcode >> 20) & 0xffu;
  u32 use_spsr = (opcode >> 22) & 1u;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 rm = opcode & 0xfu;
  u32 psr_pfield = ((opcode >> 16) & 1u) | ((opcode >> 18) & 2u);
  u8 *ptr = *translation_ptr_ref;

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
    riscv_emit_li(&ptr, riscv_reg_a0, riscv_arm_expand_imm(opcode));
  else
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rm);

  if (psr_pfield == 2u)
  {
    if (use_spsr)
      riscv_emit_arm_spsr_flags_store(&ptr, riscv_reg_a0);
    else
      riscv_emit_arm_cpsr_flags_store(&ptr, riscv_reg_a0);
    riscv_emit_adjust_cycles(&ptr, cycles);

    *translation_ptr_ref = ptr;
    riscv_native_psr_insns++;
    return true;
  }

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 4u);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_li(&ptr, riscv_reg_a1, psr_pfield);
  riscv_emit_c_call_stack(&ptr, use_spsr ? RISCV_STACK_HELPER_STORE_SPSR
                                         : RISCV_STACK_HELPER_STORE_CPSR);
  riscv_emit_adjust_cycles(&ptr, cycles);
  riscv_emit_terminal_helper_call(&ptr, meta);

  *translation_ptr_ref = ptr;
  riscv_native_psr_insns++;
  return true;
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
                                                bool short_patch_site)
{
  u32 condition = opcode >> 28;
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
    u32 target_pc = pc + (u32)riscv_arm_branch_delta(opcode);

    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, target_pc);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  }
  riscv_emit_adjust_cycles(&ptr, cycles);

  if (patchable)
  {
    /* The frontend patches direct branches before publishing the block. */
    if (branch_source)
      *branch_source = short_patch_site ?
        riscv_emit_unconditional_branch_patch_site_short(&ptr) :
        riscv_emit_unconditional_branch_patch_site(&ptr);
    else
    {
      if (short_patch_site)
        riscv_emit_unconditional_branch_patch_site_short(&ptr);
      else
        riscv_emit_unconditional_branch_patch_site(&ptr);
    }
  }
  else
  {
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
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
                                            false, false, false);
}

bool riscv_emit_native_arm_bl(u8 **translation_ptr_ref,
                              riscv_jit_block_meta *meta,
                              u32 opcode,
                              u32 pc,
                              u32 cycles)
{
  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            NULL, opcode, pc, cycles,
                                            true, false, false);
}

bool riscv_emit_native_arm_b_patchable(u8 **translation_ptr_ref,
                                       riscv_jit_block_meta *meta,
                                       u8 **branch_source,
                                       u32 opcode,
                                       u32 pc,
                                       u32 cycles,
                                       bool short_patch_site)
{
  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            branch_source, opcode, pc,
                                            cycles, false, true,
                                            short_patch_site);
}

bool riscv_emit_native_arm_bl_patchable(u8 **translation_ptr_ref,
                                        riscv_jit_block_meta *meta,
                                        u8 **branch_source,
                                        u32 opcode,
                                        u32 pc,
                                        u32 cycles,
                                        bool short_patch_site)
{
  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            branch_source, opcode, pc,
                                            cycles, true, true,
                                            short_patch_site);
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

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe)
    return false;

  if (rn == REG_PC)
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 8u);
  else
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);

  translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t1, riscv_reg_t0, -2);
  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t1);
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t2, REG_CPSR);

  translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t2, riscv_reg_t2, -33);
  riscv_emit_andi(riscv_reg_t3, riscv_reg_t0, 1);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 5);
  riscv_emit_or(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t2);
  riscv_emit_adjust_cycles(&ptr, cycles);

  meta->flags |= RISCV_BLOCK_PC_WRITTEN;
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
                                             bool patchable)
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
  riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_EXECUTE_SWI_ARM);
  riscv_emit_adjust_cycles(&ptr, cycles);

  if (patchable)
  {
    if (branch_source)
      *branch_source = riscv_emit_unconditional_branch_patch_site(&ptr);
    else
      riscv_emit_unconditional_branch_patch_site(&ptr);
    riscv_emit_terminal_helper_call(&ptr, meta);
  }
  else
  {
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
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
                                         NULL, opcode, pc, cycles, false);
}

bool riscv_emit_native_arm_swi_patchable(u8 **translation_ptr_ref,
                                         riscv_jit_block_meta *meta,
                                         u8 **branch_source,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles)
{
  return riscv_emit_native_arm_swi_common(translation_ptr_ref, meta,
                                         branch_source, opcode, pc,
                                         cycles, true);
}

bool riscv_emit_native_arm_hle_div(u8 **translation_ptr_ref,
                                   riscv_jit_block_meta *meta,
                                   bool divarm,
                                   u32 cycles)
{
  u8 *ptr = *translation_ptr_ref;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  riscv_emit_li(&ptr, riscv_reg_a0, divarm ? 1u : 0u);
  riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_HLE_DIV);
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
                                                u32 rn,
                                                s32 origin_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_s2, rn);

  translation_ptr = ptr;
  if (origin_offset)
    riscv_emit_addi(riscv_reg_s2, riscv_reg_s2, origin_offset);
  riscv_emit_andi(riscv_reg_s2, riscv_reg_s2, -4);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_s2_writeback_cursor_init(
  u8 **ptr_ref, u32 rn, s32 end_offset, s32 origin_offset)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_s2, rn);

  translation_ptr = ptr;
  if (origin_offset == -end_offset)
  {
    riscv_emit_addi(riscv_reg_t0, riscv_reg_s2, end_offset);
    ptr = translation_ptr;
    riscv_emit_arm_reg_store(&ptr, rn, riscv_reg_t0);

    translation_ptr = ptr;
    riscv_emit_andi(riscv_reg_s2, riscv_reg_s2, -4);
    ptr = translation_ptr;

    *ptr_ref = ptr;
    return;
  }

  if (end_offset)
    riscv_emit_addi(riscv_reg_s2, riscv_reg_s2, end_offset);
  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, rn, riscv_reg_s2);

  translation_ptr = ptr;
  if (origin_offset)
    riscv_emit_addi(riscv_reg_s2, riscv_reg_s2, origin_offset);
  riscv_emit_andi(riscv_reg_s2, riscv_reg_s2, -4);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_s2_cursor_load(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr = ptr;

  riscv_emit_addi(riscv_reg_a0, riscv_reg_s2, 0);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_arm_block_s2_cursor_advance(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr = ptr;

  riscv_emit_addi(riscv_reg_s2, riscv_reg_s2, 4);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static void riscv_emit_block_pc_base_restore(u8 **ptr_ref,
                                             const riscv_jit_block_meta *meta)
{
  if (meta && (meta->flags & RISCV_BLOCK_PC_BASE_EMITTED))
    riscv_emit_li(ptr_ref, riscv_reg_s2, meta->start_pc);
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
    riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_ARM_BLOCK_MEMORY);
    riscv_emit_adjust_cycles(&ptr, cycles + count);
    riscv_emit_terminal_helper_call(&ptr, meta);

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
      &ptr, rn, end_offset, origin_offset);
  else
  {
    if (address_from_writeback)
      riscv_emit_arm_block_writeback(&ptr, rn, end_offset);
    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_init(&ptr, rn, origin_offset);
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
      riscv_emit_c_call_reg(&ptr, riscv_reg_s4);
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
      riscv_emit_c_call_reg(&ptr, riscv_reg_s5);
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
    riscv_emit_cpu_alert_branch(&ptr, meta);
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
                                               bool *cycles_emitted)
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
  u32 const_register_offset = pc + 8u;
  bool const_register_offset_valid = !immediate_offset && rm == REG_PC;
  bool writeback_same_as_base =
    (immediate_offset && offset == 0) ||
    (const_register_offset_valid && const_register_offset == 0);

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

  if (immediate_offset && !pc_base)
  {
    riscv_emit_arm_memory_const_offset(&ptr, pre_index, up, offset,
                                       &writeback_reg);
  }
  else if (const_register_offset_valid && !pc_base)
  {
    riscv_emit_arm_memory_const_offset(&ptr, pre_index, up,
                                       const_register_offset,
                                       &writeback_reg);
  }
  else if (!immediate_offset && !const_register_offset_valid)
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

  if (!load)
  {
    if (rd == REG_PC)
      riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a1, pc + 12u);
    else
      riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);
  }

  if (writeback_address && !writeback_same_as_base)
    riscv_emit_arm_reg_store(&ptr, rn, writeback_reg);

  if (load)
  {
    riscv_reg_number read_helper_reg = riscv_reg_zero;
    u32 read_helper_stack_offset = 0;

    switch (mem_type)
    {
      case 1:
        read_helper_reg = riscv_reg_s8;
        break;
      case 2:
        read_helper_reg = riscv_reg_s3;
        break;
      default:
        read_helper_stack_offset = RISCV_STACK_HELPER_READ16S;
        break;
    }

    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    if (read_helper_stack_offset)
      riscv_emit_c_call_stack(&ptr, read_helper_stack_offset);
    else
      riscv_emit_c_call_reg(&ptr, read_helper_reg);
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    if (rd == REG_PC)
      meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    if (emit_cycles || rd == REG_PC)
    {
      riscv_emit_adjust_cycles(&ptr, cycles + 2u);
      if (cycles_emitted)
        *cycles_emitted = true;
    }
    riscv_native_load_insns++;
  }
  else
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 4u);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    riscv_emit_c_call_reg(&ptr, riscv_reg_s9);
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
                                              u32 *offset_out)
{
  u32 rm = opcode & 0xfu;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;
  u32 value = pc + 8u;

  if (((opcode >> 4) & 1u) || rm != REG_PC)
    return false;

  switch (shift_type)
  {
    case 0:
      *offset_out = value << shift;
      return true;
    case 1:
      *offset_out = shift ? (value >> shift) : 0;
      return true;
    case 2:
      *offset_out = (u32)((s32)value >> (shift ? shift : 31u));
      return true;
    default:
      if (!shift)
        return false;
      *offset_out = (value >> shift) | (value << (32u - shift));
      return true;
  }
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

bool riscv_emit_native_arm_access_memory_ex(u8 **translation_ptr_ref,
                                            riscv_jit_block_meta *meta,
                                            u32 opcode,
                                            u32 pc,
                                            u32 cycles,
                                            bool emit_cycles,
                                            bool *cycles_emitted)
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
    riscv_arm_memory_reg_offset_const(opcode, pc, &const_register_offset);
  bool writeback_same_as_base =
    (!register_offset && offset == 0) ||
    (const_register_offset_valid && const_register_offset == 0);

  if (cycles_emitted)
    *cycles_emitted = false;

  if ((opcode & 0x0c000000u) != 0x04000000u)
  {
    return riscv_emit_native_arm_extra_memory(translation_ptr_ref, meta,
                                             opcode, pc, cycles,
                                             emit_cycles, cycles_emitted);
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

  if (register_offset)
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
  else if (!pc_base && !pre_index && offset)
  {
    riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_t2, riscv_reg_a0,
                                     offset, up);
    writeback_reg = riscv_reg_t2;
  }
  else if (!pc_base && offset)
  {
    riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_a0, riscv_reg_a0,
                                     offset, up);
  }

  if (!load)
  {
    if (rd == REG_PC)
      riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a1, pc + 12u);
    else
      riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);
  }

  if (writeback_address && !writeback_same_as_base)
    riscv_emit_arm_reg_store(&ptr, rn, writeback_reg);

  if (load)
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    if (byte)
      riscv_emit_c_call_reg(&ptr, riscv_reg_s6);
    else
      riscv_emit_c_call_reg(&ptr, riscv_reg_s4);
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    if (rd == REG_PC)
      meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    if (emit_cycles || rd == REG_PC)
    {
      riscv_emit_adjust_cycles(&ptr, cycles + 2u);
      if (cycles_emitted)
        *cycles_emitted = true;
    }
    riscv_native_load_insns++;
  }
  else
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 4u);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    if (byte)
      riscv_emit_c_call_reg(&ptr, riscv_reg_s7);
    else
      riscv_emit_c_call_reg(&ptr, riscv_reg_s5);
    riscv_emit_adjust_cycles(&ptr, cycles + 1u);
    if (cycles_emitted)
      *cycles_emitted = true;
    riscv_emit_store_alert_branch(&ptr, meta);
    riscv_native_store_insns++;
  }

  *translation_ptr_ref = ptr;
  return true;
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
  u8 *translation_ptr;

  if (cycles_emitted)
    *cycles_emitted = false;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED) ||
      rd >= REG_PC)
  {
    return false;
  }

  if (value)
  {
    riscv_emit_li(&ptr, riscv_reg_t2, value);
    translation_ptr = ptr;
    riscv_emit_sw(riscv_reg_t2, riscv_reg_s0, rd * 4u);
    ptr = translation_ptr;
  }
  else
  {
    translation_ptr = ptr;
    riscv_emit_sw(riscv_reg_zero, riscv_reg_s0, rd * 4u);
    ptr = translation_ptr;
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
  u8 *translation_ptr = *ptr_ref;

  riscv_emit_lw(riscv_reg_t4, riscv_reg_s0, REG_CPSR * 4u);
  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 2);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 2);
  if (zero_result)
  {
    riscv_emit_lui(riscv_reg_t6, 0x40000u);
    riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
  }
  riscv_emit_sw(riscv_reg_t4, riscv_reg_s0, REG_CPSR * 4u);

  *ptr_ref = translation_ptr;
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

static void riscv_emit_thumb_cpsr_store_c_preserve_nzv(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t4, REG_CPSR);
  riscv_emit_li(&ptr, riscv_reg_t5, ~RISCV_CPSR_C);
  translation_ptr = ptr;

  riscv_emit_and(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 29);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t3);
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
  bool need_nz = (flag_status & 0x0cu) != 0;
  bool need_c = (flag_status & 0x02u) != 0;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

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

  if (need_nz && need_c)
    riscv_emit_thumb_cpsr_store_nzc_preserve_v(&ptr);
  else if (need_nz)
    riscv_emit_thumb_cpsr_store_nz_preserve_cv(&ptr);
  else if (need_c)
    riscv_emit_thumb_cpsr_store_c_preserve_nzv(&ptr);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

static bool riscv_emit_native_thumb_alu_flags(u8 **translation_ptr_ref,
                                              riscv_jit_block_meta *meta,
                                              u32 opcode,
                                              u32 flag_status,
                                              bool clobber_dead_arithmetic_flags)
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
      riscv_emit_thumb_cpsr_store_nz_preserve_cv(&ptr);

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

  if (test_op)
  {
    if (clobber_dead_arithmetic_flags)
      return riscv_emit_native_arm_data_proc_test_with_pc_ex_dead_flags(
        translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false,
        NULL);

    return riscv_emit_native_arm_data_proc_test_with_pc_ex(
      translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false,
      NULL);
  }

  if (clobber_dead_arithmetic_flags)
    return riscv_emit_native_arm_data_proc_with_pc_ex_dead_flags(
      translation_ptr_ref, meta, arm_opcode, 0, 0, flag_status, false, NULL);

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
  bool need_nz = (flag_status & 0x0cu) != 0;
  bool need_c = (flag_status & 0x02u) != 0;
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (!((hi == 0x40u && alu_op >= 2u) ||
        (hi == 0x41u && (alu_op == 0u || alu_op == 3u))))
  {
    return false;
  }

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rd);
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rs);
  if (need_c)
    riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);

  translation_ptr = ptr;
  if (hi == 0x40u && alu_op == 2u)
  {
    if (need_c)
    {
      riscv_emit_reg_lsl_with_carry(&translation_ptr, riscv_reg_t2,
                                    riscv_reg_t0, riscv_reg_t1,
                                    riscv_reg_t3, riscv_reg_t4);
    }
    else
    {
      riscv_emit_sll(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
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
                                    riscv_reg_t0, riscv_reg_t1,
                                    riscv_reg_t3, riscv_reg_t4);
    }
    else
    {
      riscv_emit_srl(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
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
                                    riscv_reg_t0, riscv_reg_t1,
                                    riscv_reg_t3, riscv_reg_t4);
    }
    else
    {
      riscv_emit_sltiu(riscv_reg_t4, riscv_reg_t1, 32);
      riscv_emit_addi(riscv_reg_t4, riscv_reg_t4, -1);
      riscv_emit_or(riscv_reg_t4, riscv_reg_t1, riscv_reg_t4);
      riscv_emit_sra(riscv_reg_t2, riscv_reg_t0, riscv_reg_t4);
    }
  }
  else
  {
    riscv_emit_sub(riscv_reg_t4, riscv_reg_zero, riscv_reg_t1);
    riscv_emit_srl(riscv_reg_t5, riscv_reg_t0, riscv_reg_t1);
    riscv_emit_sll(riscv_reg_t2, riscv_reg_t0, riscv_reg_t4);
    riscv_emit_or(riscv_reg_t2, riscv_reg_t2, riscv_reg_t5);

    if (need_c)
    {
      u8 *carry_done;

      riscv_emit_branch_with_source(&translation_ptr, &carry_done, 0x0,
                                    riscv_reg_t1, riscv_reg_zero, 0);
      riscv_emit_srli(riscv_reg_t3, riscv_reg_t2, 31);
      riscv_patch_local_branch(carry_done, translation_ptr);
    }
  }
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);

  if (need_nz && need_c)
    riscv_emit_thumb_cpsr_store_nzc_preserve_v(&ptr);
  else if (need_nz)
    riscv_emit_thumb_cpsr_store_nz_preserve_cv(&ptr);
  else if (need_c)
    riscv_emit_thumb_cpsr_store_c_preserve_nzv(&ptr);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

static bool riscv_emit_native_thumb_alu2(u8 **translation_ptr_ref,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 flag_status,
                                         bool clobber_dead_arithmetic_flags)
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
                                            clobber_dead_arithmetic_flags);

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

  if (load_rd)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rd);

  translation_ptr = ptr;
  if (hi >= 0x18u && hi <= 0x1fu)
  {
    bool subtract = (hi & 0x02u) != 0;

    if (hi & 0x04u)
    {
      if (subtract)
        riscv_emit_addi(riscv_reg_t2, riscv_reg_t1, -(s32)rn);
      else
        riscv_emit_addi(riscv_reg_t2, riscv_reg_t1, rn);
    }
    else
    {
      riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);
      translation_ptr = ptr;
      if (subtract)
        riscv_emit_sub(riscv_reg_t2, riscv_reg_t1, riscv_reg_t0);
      else
        riscv_emit_add(riscv_reg_t2, riscv_reg_t1, riscv_reg_t0);
    }
  }
  else if (hi >= 0x20u && hi <= 0x27u)
  {
    if (imm)
      riscv_emit_addi(riscv_reg_t2, riscv_reg_zero, imm);
    else
      result_reg = riscv_reg_zero;
  }
  else if (hi >= 0x30u && hi <= 0x3fu)
  {
    if (hi & 0x08u)
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, -(s32)imm);
    else
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, imm);
  }
  else if (hi == 0x40u)
  {
    if (alu_op == 0u)
      riscv_emit_and(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
    else
      riscv_emit_xor(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
  }
  else if (hi == 0x42u)
  {
    riscv_emit_sub(riscv_reg_t2, riscv_reg_zero, riscv_reg_t1);
  }
  else
  {
    switch (alu_op)
    {
      case 0:
        riscv_emit_or(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 1:
        riscv_emit_mul(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 2:
        riscv_emit_xori(riscv_reg_t1, riscv_reg_t1, -1);
        riscv_emit_and(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      default:
        riscv_emit_xori(riscv_reg_t2, riscv_reg_t1, -1);
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
                                     flag_status, false);
}

bool riscv_emit_native_thumb_alu_dead_flags(u8 **translation_ptr_ref,
                                            riscv_jit_block_meta *meta,
                                            u32 opcode,
                                            u32 flag_status)
{
  return riscv_emit_native_thumb_alu2(translation_ptr_ref, meta, opcode,
                                     flag_status, true);
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
    if (imm)
    {
      riscv_emit_li(&ptr, riscv_reg_t2, imm);
      translation_ptr = ptr;
      riscv_emit_sw(riscv_reg_t2, riscv_reg_s0, rd * 4u);
      ptr = translation_ptr;
    }
    else
    {
      translation_ptr = ptr;
      riscv_emit_sw(riscv_reg_zero, riscv_reg_s0, rd * 4u);
      ptr = translation_ptr;
    }
    riscv_emit_thumb_cpsr_store_mov_imm_nz(&ptr, imm == 0);
  }
  else if (hi >= 0xa0u && hi <= 0xafu)
  {
    if (hi < 0xa8u)
    {
      riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t2,
                               (pc & ~2u) + 4u + (imm * 4u));
    }
    else
    {
      translation_ptr = ptr;
      riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, REG_SP * 4u);
      riscv_emit_addi(riscv_reg_t2, riscv_reg_t0, (s32)(imm * 4u));
      ptr = translation_ptr;
    }
    translation_ptr = ptr;
    riscv_emit_sw(riscv_reg_t2, riscv_reg_s0, rd * 4u);
    ptr = translation_ptr;
  }
  else if (hi >= 0xb0u && hi <= 0xb3u)
  {
    s32 offset = (s32)((opcode & 0x7fu) * 4u);

    if (opcode & 0x80u)
      offset = -offset;

    translation_ptr = ptr;
    riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, REG_SP * 4u);
    riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, offset);
    riscv_emit_sw(riscv_reg_t0, riscv_reg_s0, REG_SP * 4u);
    ptr = translation_ptr;
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
                                               bool exits)
{
  u32 hi = opcode >> 8;
  u32 hrs = (opcode >> 3) & 0x0fu;
  u32 hrd = ((opcode >> 4) & 0x08u) | (opcode & 0x07u);
  u8 *ptr = *translation_ptr_ref;
  u8 *translation_ptr;

  if (exits || (hi != 0x44u && hi != 0x46u) || hrd == REG_PC)
    return false;

  if (hrs == REG_PC)
  {
    riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t1, pc + 4u);
  }
  else
  {
    translation_ptr = ptr;
    riscv_emit_lw(riscv_reg_t1, riscv_reg_s0, hrs * 4u);
    ptr = translation_ptr;
  }

  translation_ptr = ptr;
  if (hi == 0x44u)
  {
    riscv_emit_lw(riscv_reg_t0, riscv_reg_s0, hrd * 4u);
    riscv_emit_add(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
    riscv_emit_sw(riscv_reg_t2, riscv_reg_s0, hrd * 4u);
  }
  else
  {
    riscv_emit_sw(riscv_reg_t1, riscv_reg_s0, hrd * 4u);
  }
  ptr = translation_ptr;

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

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 2u);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);

  if (load)
  {
    switch (mem_type)
    {
      case 0:
        riscv_emit_c_call_reg(&ptr, riscv_reg_s4);
        break;
      case 1:
        riscv_emit_c_call_reg(&ptr, riscv_reg_s8);
        break;
      case 2:
        riscv_emit_c_call_reg(&ptr, riscv_reg_s6);
        break;
      case 3:
        riscv_emit_c_call_reg(&ptr, riscv_reg_s3);
        break;
      default:
        riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_READ16S);
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
        riscv_emit_c_call_reg(&ptr, riscv_reg_s5);
        break;
      case 1:
        riscv_emit_c_call_reg(&ptr, riscv_reg_s9);
        break;
      case 2:
        riscv_emit_c_call_reg(&ptr, riscv_reg_s7);
        break;
      default:
        return false;
    }

    riscv_emit_adjust_cycles(&ptr, cycles + 1u);
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

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, pc + 2u);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);

  use_s2_cursor = has_pc || (origin_offset ? count >= 2u : count >= 5u);
  if (writeback_first && use_s2_cursor)
    riscv_emit_arm_block_s2_writeback_cursor_init(
      &ptr, rn, end_offset, origin_offset);
  else
  {
    if (writeback_first)
      riscv_emit_thumb_block_writeback(&ptr, rn, end_offset);
    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_init(&ptr, rn, origin_offset);
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
      riscv_emit_c_call_reg(&ptr, riscv_reg_s4);
      riscv_emit_arm_reg_store(&ptr, i, riscv_reg_a0);
    }
    else
    {
      riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, i);
      riscv_emit_c_call_reg(&ptr, riscv_reg_s5);
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
    riscv_emit_c_call_reg(&ptr, riscv_reg_s5);
  }
  else if (has_pc)
  {
    if (use_s2_cursor)
      riscv_emit_arm_block_s2_cursor_load(&ptr);
    else
      riscv_emit_thumb_block_initial_address(&ptr, rn, origin_offset, offset);
    riscv_emit_c_call_reg(&ptr, riscv_reg_s4);
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
                                                bool short_patch_site)
{
  u32 hi = opcode >> 8;
  u32 condition = hi & 0x0fu;
  u8 *ptr = *translation_ptr_ref;
  u8 *branch_skip;

  (void)pc;

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi < 0xd0u || hi > 0xddu)
    return false;

  if (!riscv_emit_arm_condition_branch(&ptr, condition ^ 1u, 0,
                                       &branch_skip))
    return false;

  riscv_emit_adjust_cycles(&ptr, cycles);

  /* Taken path is patched to the target; fallthrough exits via finalizer. */
  if (branch_source)
    *branch_source = short_patch_site ?
      riscv_emit_unconditional_branch_patch_site_short(&ptr) :
      riscv_emit_unconditional_branch_patch_site(&ptr);
  else
  {
    if (short_patch_site)
      riscv_emit_unconditional_branch_patch_site_short(&ptr);
    else
      riscv_emit_unconditional_branch_patch_site(&ptr);
  }

  riscv_patch_local_branch(branch_skip, ptr);

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
                                         bool short_patch_site)
{
  u32 hi = opcode >> 8;
  u32 target_pc;
  u32 patch_bytes = short_patch_site ? RISCV_BRANCH_PATCH_SHORT_BYTES :
    RISCV_BRANCH_PATCH_BYTES;
  u8 *ptr = *translation_ptr_ref;

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi < 0xe0u || hi > 0xe7u)
    return false;

  target_pc = pc + 4u + (u32)((s32)((opcode & 0x07ffu) << 21) >> 20);
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, target_pc);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_adjust_cycles(&ptr, cycles);
  {
    u8 *translation_ptr = ptr;

    riscv_emit_bge(riscv_reg_zero, riscv_reg_s11,
                   4 + patch_bytes);
    ptr = translation_ptr;
  }

  if (branch_source)
    *branch_source = short_patch_site ?
      riscv_emit_unconditional_branch_patch_site_short(&ptr) :
      riscv_emit_unconditional_branch_patch_site(&ptr);
  else
  {
    if (short_patch_site)
      riscv_emit_unconditional_branch_patch_site_short(&ptr);
    else
      riscv_emit_unconditional_branch_patch_site(&ptr);
  }

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

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi != 0x47u)
    return false;

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, meta, hrs, pc + 4u);
  translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t1, riscv_reg_t0, -2);
  ptr = translation_ptr;
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t1);
  riscv_emit_arm_reg_load(&ptr, riscv_reg_t2, REG_CPSR);

  translation_ptr = ptr;
  riscv_emit_andi(riscv_reg_t2, riscv_reg_t2, -33);
  riscv_emit_andi(riscv_reg_t3, riscv_reg_t0, 1);
  riscv_emit_slli(riscv_reg_t3, riscv_reg_t3, 5);
  riscv_emit_or(riscv_reg_t2, riscv_reg_t2, riscv_reg_t3);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t2);
  riscv_emit_adjust_cycles(&ptr, cycles);
  meta->flags |= RISCV_BLOCK_PC_WRITTEN;

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
}

bool riscv_emit_native_thumb_swi_patchable(u8 **translation_ptr_ref,
                                           riscv_jit_block_meta *meta,
                                           u8 **branch_source,
                                           u32 opcode,
                                           u32 pc,
                                           u32 cycles)
{
  u32 hi = opcode >> 8;
  u32 swinum = opcode & 0xffu;
  u8 *ptr = *translation_ptr_ref;

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (hi != 0xdfu || swinum == 6u || swinum == 7u)
    return false;

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a0, pc);
  riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_EXECUTE_SWI_THUMB);
  riscv_emit_adjust_cycles(&ptr, cycles);

  if (branch_source)
    *branch_source = riscv_emit_unconditional_branch_patch_site(&ptr);
  else
    riscv_emit_unconditional_branch_patch_site(&ptr);
  riscv_emit_terminal_helper_call(&ptr, meta);

  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
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

  if (riscv_emit_native_thumb_simple_data(&ptr, meta, opcode, pc,
                                          cycles, exits))
  {
    *translation_ptr_ref = ptr;
    riscv_note_thumb_native_stat(opcode);
    return true;
  }

  if (riscv_emit_native_thumb_hi_add_mov(&ptr, meta, opcode, pc, exits))
  {
    *translation_ptr_ref = ptr;
    riscv_note_thumb_native_stat(opcode);
    return true;
  }

  riscv_emit_li(&ptr, riscv_reg_a0, opcode & 0xffffu);
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_a1, pc);
  riscv_emit_c_call_stack(&ptr, RISCV_STACK_HELPER_THUMB_EXECUTE);
  riscv_emit_adjust_cycles(&ptr, cycles);
  {
    u8 *translation_ptr = ptr;

    riscv_emit_sub(riscv_reg_s11, riscv_reg_s11, riscv_reg_a0);
    ptr = translation_ptr;
  }
  if (cycles_emitted)
    *cycles_emitted = true;

  if (exits)
  {
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    riscv_emit_terminal_helper_call(&ptr, meta);
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
  u8 *translation_ptr;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED) || rd >= 8u)
    return false;

  if (value)
  {
    riscv_emit_li(&ptr, riscv_reg_t2, value);
    translation_ptr = ptr;
    riscv_emit_sw(riscv_reg_t2, riscv_reg_s0, rd * 4u);
    ptr = translation_ptr;
  }
  else
  {
    translation_ptr = ptr;
    riscv_emit_sw(riscv_reg_zero, riscv_reg_s0, rd * 4u);
    ptr = translation_ptr;
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

  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, link_value);
  riscv_emit_arm_reg_store(&ptr, REG_LR, riscv_reg_t0);
  riscv_emit_guest_pc_load(&ptr, meta, riscv_reg_t0, target_pc);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_adjust_cycles(&ptr, cycles);
  meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  riscv_emit_terminal_helper_call(&ptr, meta);

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
  u32 store_alert_branches = meta->thumb;
  u8 *helper_tail = NULL;
  bool terminal_at_end =
    (meta->flags & RISCV_BLOCK_TERMINAL_EMITTED) &&
    meta->end_pc == (u32)(ptr - (u8 *)meta);

  meta->start_pc = block_start_pc;
  meta->end_pc = block_end_pc;
  meta->thumb = thumb_mode ? 1u : 0u;

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
    helper_tail = ptr - riscv_helper_call_size(meta);
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

void init_emitter(bool must_swap)
{
  (void)must_swap;

  riscv_cycles_remaining = 0;
  riscv_blocks_emitted = 0;
  riscv_blocks_executed = 0;
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

  riscv_cycles_remaining = (s32)cycles;
  riscv_cpu_alert = CPU_ALERT_NONE;
  clear_gamepak_stickybits();

  if (cycles == 0)
    return 0;

  entry_data = riscv_lookup_current_block(&pc, &thumb);

  if (!entry_data || entry_data == RISCV_INVALID_BLOCK_ENTRY)
  {
    riscv_interpreter_fallbacks++;
    riscv_initial_lookup_fallbacks++;
    riscv_note_runtime_fallback(RISCV_RUNTIME_FALLBACK_INITIAL_LOOKUP,
                                pc, thumb,
                                riscv_lookup_result_from_entry(entry_data),
                                cycles);
    execute_arm(cycles);
    riscv_cycles_remaining = 0;
    return 0;
  }

  riscv_init_helper_table();
  (void)riscv_enter_jit(entry_data, regptr,
                        (void *)(uintptr_t)riscv_jit_run_block,
                        (void *)(uintptr_t)riscv_thumb_execute,
                        (void *)(uintptr_t)riscv_thumb_execute_bl_pair,
                        riscv_helper_table);

  return 0;
}
