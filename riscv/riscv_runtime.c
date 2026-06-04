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

enum
{
  RISCV_INITIAL_ROM_WATERMARK = 16,
  RISCV_BLOCK_NATIVE_SUPPORTED = 1u,
  RISCV_BLOCK_PC_WRITTEN = 2u
};

#define RISCV_INVALID_BLOCK_ENTRY ((u8 *)(uintptr_t)~(uintptr_t)0)

static s32 riscv_cycles_remaining;
static u32 riscv_blocks_emitted;
static u32 riscv_blocks_executed;
static u32 riscv_interpreter_fallbacks;
static u32 riscv_native_data_proc_insns;
static u32 riscv_native_branch_insns;
static u32 riscv_native_load_insns;
static u32 riscv_native_store_insns;
static cpu_alert_type riscv_cpu_alert;

static u8 *riscv_jit_run_block(const riscv_jit_block_meta *meta);

static u8 *riscv_align_ptr(u8 *ptr)
{
  uintptr_t value = (uintptr_t)ptr;
  value = (value + 3u) & ~(uintptr_t)3u;
  return (u8 *)value;
}

static void riscv_emit_li(u8 **ptr, riscv_reg_number rd, u32 value)
{
  u8 *translation_ptr = *ptr;

  if (value <= 2047u)
  {
    riscv_emit_addi(rd, riscv_reg_zero, value);
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

static void riscv_emit_global_u32_load(u8 **ptr, riscv_reg_number rd,
                                       const void *addr)
{
  u8 *translation_ptr = *ptr;

  *ptr = translation_ptr;
  riscv_emit_li(ptr, riscv_reg_t6, (u32)(uintptr_t)addr);
  translation_ptr = *ptr;
  riscv_emit_lw(rd, riscv_reg_t6, 0);

  *ptr = translation_ptr;
}

static void riscv_emit_global_u32_store(u8 **ptr, const void *addr,
                                        riscv_reg_number rs)
{
  u8 *translation_ptr = *ptr;

  *ptr = translation_ptr;
  riscv_emit_li(ptr, riscv_reg_t6, (u32)(uintptr_t)addr);
  translation_ptr = *ptr;
  riscv_emit_sw(rs, riscv_reg_t6, 0);

  *ptr = translation_ptr;
}

static void riscv_emit_arm_reg_load(u8 **ptr, riscv_reg_number rd,
                                    u32 reg_index)
{
  u8 *translation_ptr = *ptr;

  *ptr = translation_ptr;
  riscv_emit_li(ptr, riscv_reg_t5, (u32)(uintptr_t)&reg[0]);
  translation_ptr = *ptr;
  riscv_emit_lw(rd, riscv_reg_t5, reg_index * 4u);

  *ptr = translation_ptr;
}

static void riscv_emit_arm_reg_store(u8 **ptr, u32 reg_index,
                                     riscv_reg_number rs)
{
  u8 *translation_ptr = *ptr;

  *ptr = translation_ptr;
  riscv_emit_li(ptr, riscv_reg_t5, (u32)(uintptr_t)&reg[0]);
  translation_ptr = *ptr;
  riscv_emit_sw(rs, riscv_reg_t5, reg_index * 4u);

  *ptr = translation_ptr;
}

static void riscv_emit_adjust_cycles(u8 **ptr, u32 cycles)
{
  u8 *translation_ptr;

  if (!cycles)
    return;

  riscv_emit_global_u32_load(ptr, riscv_reg_t3, &riscv_cycles_remaining);
  translation_ptr = *ptr;
  if (cycles <= 2047u)
  {
    riscv_emit_addi(riscv_reg_t3, riscv_reg_t3, -(int)cycles);
  }
  else
  {
    *ptr = translation_ptr;
    riscv_emit_li(ptr, riscv_reg_t4, cycles);
    translation_ptr = *ptr;
    riscv_emit_sub(riscv_reg_t3, riscv_reg_t3, riscv_reg_t4);
  }
  *ptr = translation_ptr;
  riscv_emit_global_u32_store(ptr, &riscv_cycles_remaining, riscv_reg_t3);
}

static void riscv_emit_c_call(u8 **ptr, uintptr_t function_addr)
{
  u8 *translation_ptr;

  translation_ptr = *ptr;
  riscv_emit_addi(riscv_reg_sp, riscv_reg_sp, -16);
  riscv_emit_sw(riscv_reg_ra, riscv_reg_sp, 12);
  *ptr = translation_ptr;

  riscv_emit_li(ptr, riscv_reg_t0, (u32)function_addr);
  translation_ptr = *ptr;
  riscv_emit_jalr(riscv_reg_ra, riscv_reg_t0, 0);
  riscv_emit_lw(riscv_reg_ra, riscv_reg_sp, 12);
  riscv_emit_addi(riscv_reg_sp, riscv_reg_sp, 16);

  *ptr = translation_ptr;
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
  u8 *translation_ptr;

  translation_ptr = *ptr;
  riscv_emit_addi(riscv_reg_sp, riscv_reg_sp, -16);
  riscv_emit_sw(riscv_reg_ra, riscv_reg_sp, 12);
  *ptr = translation_ptr;

  riscv_emit_li(ptr, riscv_reg_a0, (u32)(uintptr_t)meta);
  riscv_emit_li(ptr, riscv_reg_t0, (u32)(uintptr_t)riscv_jit_run_block);
  translation_ptr = *ptr;
  riscv_emit_jalr(riscv_reg_ra, riscv_reg_t0, 0);
  riscv_emit_lw(riscv_reg_ra, riscv_reg_sp, 12);
  riscv_emit_addi(riscv_reg_sp, riscv_reg_sp, 16);
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);
  *ptr = translation_ptr;
}

static u8 *riscv_jit_run_block(const riscv_jit_block_meta *meta)
{
  u32 update_ret;
  cpu_alert_type alert = CPU_ALERT_NONE;

  (void)meta;

  riscv_blocks_executed++;
  riscv_interpreter_fallbacks++;

  if (riscv_cpu_alert != CPU_ALERT_NONE)
    alert = riscv_handle_cpu_alert();

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

  if (riscv_cycles_remaining > 0)
  {
    execute_arm((u32)riscv_cycles_remaining);
    riscv_cycles_remaining = 0;
  }

  return NULL;
}

void riscv_emit_block_prologue(u8 **translation_ptr,
                               riscv_jit_block_meta **meta)
{
  u8 *ptr = riscv_align_ptr(*translation_ptr);

  *meta = (riscv_jit_block_meta *)(void *)ptr;
  (*meta)->start_pc = 0;
  (*meta)->end_pc = 0;
  (*meta)->thumb = 0;
  (*meta)->flags = RISCV_BLOCK_NATIVE_SUPPORTED;

  *translation_ptr = ptr + block_prologue_size;
}

void riscv_mark_block_unsupported(riscv_jit_block_meta *meta)
{
  if (meta)
    meta->flags &= ~RISCV_BLOCK_NATIVE_SUPPORTED;
}

bool riscv_emit_native_arm_data_proc(u8 **translation_ptr_ref,
                                     riscv_jit_block_meta *meta,
                                     u32 opcode,
                                     u32 cycles)
{
  u32 condition = opcode >> 28;
  u32 op = (opcode >> 21) & 0xfu;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 imm_op = (opcode >> 25) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 rm = opcode & 0xfu;
  u8 *ptr = *translation_ptr_ref;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || set_flags || rd == REG_PC)
    return false;

  if (op != 0xd && rn == REG_PC)
    return false;

  if (!imm_op && (((opcode >> 4) & 0xffu) != 0 || rm == REG_PC))
    return false;

  if (op != 0x0 && op != 0x1 && op != 0x2 &&
      op != 0x4 && op != 0xc && op != 0xd)
  {
    return false;
  }

  if (op != 0xd)
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t0, rn);

  if (imm_op)
    riscv_emit_li(&ptr, riscv_reg_t1, riscv_arm_expand_imm(opcode));
  else
    riscv_emit_arm_reg_load(&ptr, riscv_reg_t1, rm);

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
      case 0x4:
        riscv_emit_add(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 0xc:
        riscv_emit_or(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
        break;
      case 0xd:
        riscv_emit_add(riscv_reg_t2, riscv_reg_t1, riscv_reg_zero);
        break;
      default:
        return false;
    }

    ptr = translation_ptr;
  }

  riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);
  riscv_emit_adjust_cycles(&ptr, cycles);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_arm_b(u8 **translation_ptr_ref,
                             riscv_jit_block_meta *meta,
                             u32 opcode,
                             u32 pc,
                             u32 cycles)
{
  u32 condition = opcode >> 28;
  u8 *ptr = *translation_ptr_ref;
  u32 target_pc;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe)
    return false;

  target_pc = pc + (u32)riscv_arm_branch_delta(opcode);
  riscv_emit_li(&ptr, riscv_reg_t0, target_pc);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_adjust_cycles(&ptr, cycles);

  meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  *translation_ptr_ref = ptr;
  riscv_native_branch_insns++;
  return true;
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
    riscv_emit_li(&ptr, riscv_reg_t0, pc + 8u);
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

static bool riscv_emit_native_arm_extra_memory(u8 **translation_ptr_ref,
                                               riscv_jit_block_meta *meta,
                                               u32 opcode,
                                               u32 pc,
                                               u32 cycles)
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
  u32 offset = ((opcode >> 4) & 0xf0u) | (opcode & 0x0fu);
  u8 *ptr = *translation_ptr_ref;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0e000090u) != 0x00000090u ||
      !immediate_offset || !pre_index || writeback || rn == REG_PC ||
      mem_type == 0 || (load && rd == REG_PC) ||
      (!load && mem_type != 1))
  {
    return false;
  }

  riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);

  if (offset)
  {
    u8 *translation_ptr = ptr;
    if (up)
      riscv_emit_addi(riscv_reg_a0, riscv_reg_a0, offset);
    else
      riscv_emit_addi(riscv_reg_a0, riscv_reg_a0, -(int)offset);
    ptr = translation_ptr;
  }

  if (load)
  {
    uintptr_t read_helper;

    switch (mem_type)
    {
      case 1:
        read_helper = (uintptr_t)read_memory16;
        break;
      case 2:
        read_helper = (uintptr_t)read_memory8s;
        break;
      default:
        read_helper = (uintptr_t)read_memory16s;
        break;
    }

    riscv_emit_li(&ptr, riscv_reg_t0, pc);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    riscv_emit_c_call(&ptr, read_helper);
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    riscv_emit_adjust_cycles(&ptr, cycles + 2u);
    riscv_native_load_insns++;
  }
  else
  {
    if (rd == REG_PC)
      riscv_emit_li(&ptr, riscv_reg_a1, pc + 12u);
    else
      riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);

    riscv_emit_li(&ptr, riscv_reg_t0, pc + 4u);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    riscv_emit_c_call(&ptr, (uintptr_t)riscv_store_u16);
    riscv_emit_adjust_cycles(&ptr, cycles + 1u);
    riscv_emit_helper_call(&ptr, meta);
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
  u32 condition = opcode >> 28;
  u32 immediate_offset = (opcode >> 25) & 1u;
  u32 pre_index = (opcode >> 24) & 1u;
  u32 up = (opcode >> 23) & 1u;
  u32 byte = (opcode >> 22) & 1u;
  u32 writeback = (opcode >> 21) & 1u;
  u32 load = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 offset = opcode & 0xfffu;
  u8 *ptr = *translation_ptr_ref;

  if ((opcode & 0x0c000000u) != 0x04000000u)
  {
    return riscv_emit_native_arm_extra_memory(translation_ptr_ref, meta,
                                             opcode, pc, cycles);
  }

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || immediate_offset || !pre_index || writeback ||
      rn == REG_PC || (load && rd == REG_PC))
  {
    return false;
  }

  riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);

  if (offset)
  {
    if (up && offset <= 2047u)
    {
      u8 *translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_a0, riscv_reg_a0, offset);
      ptr = translation_ptr;
    }
    else if (!up && offset <= 2048u)
    {
      u8 *translation_ptr = ptr;
      riscv_emit_addi(riscv_reg_a0, riscv_reg_a0, -(int)offset);
      ptr = translation_ptr;
    }
    else
    {
      u8 *translation_ptr;

      riscv_emit_li(&ptr, riscv_reg_t0, offset);
      translation_ptr = ptr;
      if (up)
        riscv_emit_add(riscv_reg_a0, riscv_reg_a0, riscv_reg_t0);
      else
        riscv_emit_sub(riscv_reg_a0, riscv_reg_a0, riscv_reg_t0);
      ptr = translation_ptr;
    }
  }

  if (load)
  {
    riscv_emit_li(&ptr, riscv_reg_t0, pc);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    riscv_emit_c_call(&ptr, byte ? (uintptr_t)read_memory8
                                : (uintptr_t)read_memory32);
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    riscv_emit_adjust_cycles(&ptr, cycles + 2u);
    riscv_native_load_insns++;
  }
  else
  {
    if (rd == REG_PC)
      riscv_emit_li(&ptr, riscv_reg_a1, pc + 12u);
    else
      riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);

    riscv_emit_li(&ptr, riscv_reg_t0, pc + 4u);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    riscv_emit_c_call(&ptr, byte ? (uintptr_t)riscv_store_u8
                                : (uintptr_t)riscv_store_u32);
    riscv_emit_adjust_cycles(&ptr, cycles + 1u);
    riscv_emit_helper_call(&ptr, meta);
    riscv_native_store_insns++;
  }

  *translation_ptr_ref = ptr;
  return true;
}

void riscv_emit_block_finalize(riscv_jit_block_meta *meta,
                               u8 **translation_ptr,
                               u32 block_start_pc,
                               u32 block_end_pc,
                               bool thumb_mode)
{
  u8 *ptr = *translation_ptr;

  meta->start_pc = block_start_pc;
  meta->end_pc = block_end_pc;
  meta->thumb = thumb_mode ? 1u : 0u;
  if (thumb_mode)
    riscv_mark_block_unsupported(meta);

  if (!(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
  {
    ptr = ((u8 *)meta) + block_prologue_size;
  }
  else
  {
    if (!(meta->flags & RISCV_BLOCK_PC_WRITTEN))
    {
      riscv_emit_li(&ptr, riscv_reg_t0, block_end_pc);
      riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    }
  }

  riscv_emit_helper_call(&ptr, meta);

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
  riscv_native_data_proc_insns = 0;
  riscv_native_branch_insns = 0;
  riscv_native_load_insns = 0;
  riscv_native_store_insns = 0;
  riscv_cpu_alert = CPU_ALERT_NONE;
  rom_cache_watermark = RISCV_INITIAL_ROM_WATERMARK;
  init_bios_hooks();
}

u32 execute_arm_translate(u32 cycles)
{
  return execute_arm_translate_internal(cycles, &reg[0]);
}

u32 execute_arm_translate_internal(u32 cycles, void *regptr)
{
  riscv_jit_block_fn entry;
  u8 *entry_data;

  (void)regptr;

  riscv_cycles_remaining = (s32)cycles;
  riscv_cpu_alert = CPU_ALERT_NONE;
  clear_gamepak_stickybits();

  if (cycles == 0)
    return 0;

  if (reg[REG_CPSR] & 0x20)
    entry_data = block_lookup_address_thumb(reg[REG_PC] & ~1u);
  else
    entry_data = block_lookup_address_arm(reg[REG_PC] & ~0x03);

  if (!entry_data || entry_data == RISCV_INVALID_BLOCK_ENTRY)
  {
    execute_arm(cycles);
    riscv_cycles_remaining = 0;
    return 0;
  }

  entry = (riscv_jit_block_fn)entry_data;
  (void)entry();
  return 0;
}
