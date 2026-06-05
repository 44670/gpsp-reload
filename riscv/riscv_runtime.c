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
static u32 riscv_initial_lookup_fallbacks;
static u32 riscv_relookup_fallbacks;
static u32 riscv_unsupported_fallbacks;
static u32 riscv_native_data_proc_insns;
static u32 riscv_native_branch_insns;
static u32 riscv_native_load_insns;
static u32 riscv_native_store_insns;
static u32 riscv_native_psr_insns;
static cpu_alert_type riscv_cpu_alert;

static u8 *riscv_jit_run_block(const riscv_jit_block_meta *meta);

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

static void riscv_emit_arm_reg_or_pc_load(u8 **ptr, riscv_reg_number rd,
                                          u32 reg_index, u32 pc_value)
{
  if (reg_index == REG_PC)
    riscv_emit_li(ptr, rd, pc_value);
  else
    riscv_emit_arm_reg_load(ptr, rd, reg_index);
}

static void riscv_emit_arm_memory_imm_offset(u8 **ptr_ref,
                                             riscv_reg_number rd,
                                             riscv_reg_number rs,
                                             u32 offset,
                                             bool up);

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

static u8 *riscv_emit_unconditional_branch_patch_site(u8 **ptr_ref)
{
  u8 *translation_ptr = *ptr_ref;
  u8 *source = translation_ptr;

  riscv_emit_nop();
  riscv_emit_nop();
  riscv_emit_nop();

  *ptr_ref = translation_ptr;
  return source;
}

void riscv_patch_unconditional_branch(u8 *source, const u8 *target)
{
  u32 target_addr;
  uint64_t upper;
  s32 lower;

  if (!source || !target)
    return;

  target_addr = (u32)(uintptr_t)target;
  upper = ((uint64_t)target_addr + 0x800u) >> 12;
  lower = (s32)(target_addr - (u32)(upper << 12));

  ((u32 *)source)[0] =
    riscv_encode_u(riscv_opcode_lui, riscv_reg_t6, (u32)upper);
  ((u32 *)source)[1] =
    riscv_encode_i(riscv_opcode_op_imm, 0x0,
                   riscv_reg_t6, riscv_reg_t6, lower);
  ((u32 *)source)[2] =
    riscv_encode_i(riscv_opcode_jalr, 0x0,
                   riscv_reg_zero, riscv_reg_t6, 0);
}

static void riscv_emit_arm_cpsr_flag_value(u8 **ptr_ref,
                                           riscv_reg_number rd,
                                           u32 shift)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, rd, REG_CPSR);
  translation_ptr = ptr;
  riscv_emit_srli(rd, rd, shift);
  riscv_emit_andi(rd, rd, 1);
  ptr = translation_ptr;

  *ptr_ref = ptr;
}

static bool riscv_emit_arm_condition_value(u8 **ptr_ref, u32 condition)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  switch (condition)
  {
    case 0x0:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 30);
      break;
    case 0x1:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 30);
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t0, riscv_reg_t0, 1);
      ptr = translation_ptr;
      break;
    case 0x2:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 29);
      break;
    case 0x3:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 29);
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t0, riscv_reg_t0, 1);
      ptr = translation_ptr;
      break;
    case 0x4:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 31);
      break;
    case 0x5:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 31);
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t0, riscv_reg_t0, 1);
      ptr = translation_ptr;
      break;
    case 0x6:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 28);
      break;
    case 0x7:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 28);
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t0, riscv_reg_t0, 1);
      ptr = translation_ptr;
      break;
    case 0x8:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 29);
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t1, 30);
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t1, riscv_reg_t1, 1);
      riscv_emit_and(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
      ptr = translation_ptr;
      break;
    case 0x9:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 29);
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t1, 30);
      translation_ptr = ptr;
      riscv_emit_xori(riscv_reg_t0, riscv_reg_t0, 1);
      riscv_emit_or(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
      ptr = translation_ptr;
      break;
    case 0xa:
    case 0xb:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 31);
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t1, 28);
      translation_ptr = ptr;
      riscv_emit_xor(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
      if (condition == 0xa)
        riscv_emit_xori(riscv_reg_t0, riscv_reg_t0, 1);
      ptr = translation_ptr;
      break;
    case 0xc:
    case 0xd:
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t0, 31);
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t1, 28);
      riscv_emit_arm_cpsr_flag_value(&ptr, riscv_reg_t2, 30);
      translation_ptr = ptr;
      riscv_emit_xor(riscv_reg_t0, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xori(riscv_reg_t0, riscv_reg_t0, 1);
      riscv_emit_xori(riscv_reg_t2, riscv_reg_t2, 1);
      riscv_emit_and(riscv_reg_t0, riscv_reg_t0, riscv_reg_t2);
      if (condition == 0xd)
        riscv_emit_xori(riscv_reg_t0, riscv_reg_t0, 1);
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
  u8 *translation_ptr;

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (!riscv_emit_arm_condition_value(&ptr, condition))
    return false;

  riscv_emit_adjust_cycles(&ptr, cycles);
  translation_ptr = ptr;
  riscv_emit_bne(riscv_reg_t0, riscv_reg_zero, 16);
  ptr = translation_ptr;

  if (branch_source)
    *branch_source = riscv_emit_unconditional_branch_patch_site(&ptr);
  else
    riscv_emit_unconditional_branch_patch_site(&ptr);

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

static void riscv_emit_arm_cpsr_c_load(u8 **ptr_ref, riscv_reg_number rd);
static void riscv_emit_arm_cpsr_v_load(u8 **ptr_ref, riscv_reg_number rd);

static bool riscv_emit_arm_data_proc_operand2(u8 **ptr_ref, u32 opcode,
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
    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, rm, pc + 12u);
    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t3, rs, pc + 8u);
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
        riscv_emit_sub(riscv_reg_t4, riscv_reg_zero, riscv_reg_t4);
        riscv_emit_and(riscv_reg_t5, riscv_reg_t3, riscv_reg_t4);
        riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
        riscv_emit_addi(riscv_reg_t6, riscv_reg_zero, 31);
        riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t4);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t5, riscv_reg_t6);
        riscv_emit_sra(riscv_reg_t1, riscv_reg_t1, riscv_reg_t3);
        break;
      default:
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 31);
        riscv_emit_sub(riscv_reg_t4, riscv_reg_zero, riscv_reg_t3);
        riscv_emit_srl(riscv_reg_t5, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_sll(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t5);
        break;
    }

    *ptr_ref = translation_ptr;
    return true;
  }

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, rm, pc + 8u);
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

static void riscv_emit_arm_cpsr_v_load(u8 **ptr_ref, riscv_reg_number rd)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, rd, REG_CPSR);
  translation_ptr = ptr;
  riscv_emit_srli(rd, rd, 28);
  riscv_emit_andi(rd, rd, 1);

  *ptr_ref = translation_ptr;
}

static bool riscv_emit_arm_data_proc_operand2_with_carry(u8 **ptr_ref,
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
    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, rm, pc + 12u);
    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t4, rs, pc + 8u);
    riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t6);
    translation_ptr = ptr;
    riscv_emit_andi(riscv_reg_t4, riscv_reg_t4, 0xff);

    switch (shift_type)
    {
      case 0:
        riscv_emit_sub(riscv_reg_t3, riscv_reg_zero, riscv_reg_t4);
        riscv_emit_srl(riscv_reg_t3, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 33);
        riscv_emit_sltiu(riscv_reg_t2, riscv_reg_t4, 1);
        riscv_emit_xori(riscv_reg_t2, riscv_reg_t2, 1);
        riscv_emit_and(riscv_reg_t5, riscv_reg_t5, riscv_reg_t2);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t3, riscv_reg_t3, riscv_reg_t5);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 1);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t3, riscv_reg_t6);
        riscv_emit_sll(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 32);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t1, riscv_reg_t1, riscv_reg_t5);
        break;
      case 1:
        riscv_emit_addi(riscv_reg_t3, riscv_reg_t4, -1);
        riscv_emit_srl(riscv_reg_t3, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 33);
        riscv_emit_sltiu(riscv_reg_t2, riscv_reg_t4, 1);
        riscv_emit_xori(riscv_reg_t2, riscv_reg_t2, 1);
        riscv_emit_and(riscv_reg_t5, riscv_reg_t5, riscv_reg_t2);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t3, riscv_reg_t3, riscv_reg_t5);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 1);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t3, riscv_reg_t6);
        riscv_emit_srl(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 32);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t1, riscv_reg_t1, riscv_reg_t5);
        break;
      case 2:
        riscv_emit_addi(riscv_reg_t3, riscv_reg_t4, -1);
        riscv_emit_srl(riscv_reg_t3, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 32);
        riscv_emit_sltiu(riscv_reg_t2, riscv_reg_t4, 1);
        riscv_emit_xori(riscv_reg_t2, riscv_reg_t2, 1);
        riscv_emit_and(riscv_reg_t5, riscv_reg_t5, riscv_reg_t2);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t3, riscv_reg_t3, riscv_reg_t5);
        riscv_emit_srli(riscv_reg_t5, riscv_reg_t1, 31);
        riscv_emit_sltiu(riscv_reg_t2, riscv_reg_t4, 32);
        riscv_emit_xori(riscv_reg_t2, riscv_reg_t2, 1);
        riscv_emit_sub(riscv_reg_t2, riscv_reg_zero, riscv_reg_t2);
        riscv_emit_and(riscv_reg_t5, riscv_reg_t5, riscv_reg_t2);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t3, riscv_reg_t5);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 1);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t3, riscv_reg_t6);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 32);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t2, riscv_reg_t4, riscv_reg_t5);
        riscv_emit_xori(riscv_reg_t5, riscv_reg_t5, -1);
        riscv_emit_addi(riscv_reg_t6, riscv_reg_zero, 31);
        riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t4, riscv_reg_t2, riscv_reg_t6);
        riscv_emit_sra(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
        break;
      default:
        riscv_emit_addi(riscv_reg_t3, riscv_reg_t4, -1);
        riscv_emit_srl(riscv_reg_t3, riscv_reg_t1, riscv_reg_t3);
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t4, 1);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t5);
        riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
        riscv_emit_xori(riscv_reg_t5, riscv_reg_t5, -1);
        riscv_emit_and(riscv_reg_t3, riscv_reg_t3, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t3, riscv_reg_t3, riscv_reg_t6);
        riscv_emit_andi(riscv_reg_t4, riscv_reg_t4, 31);
        riscv_emit_sub(riscv_reg_t5, riscv_reg_zero, riscv_reg_t4);
        riscv_emit_srl(riscv_reg_t2, riscv_reg_t1, riscv_reg_t4);
        riscv_emit_sll(riscv_reg_t1, riscv_reg_t1, riscv_reg_t5);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t2);
        break;
    }

    *ptr_ref = translation_ptr;
    return true;
  }

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t1, rm, pc + 8u);
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
        riscv_emit_srli(riscv_reg_t3, riscv_reg_t1, shift - 1u);
        riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
        riscv_emit_srli(riscv_reg_t4, riscv_reg_t1, shift);
        riscv_emit_slli(riscv_reg_t1, riscv_reg_t1, 32u - shift);
        riscv_emit_or(riscv_reg_t1, riscv_reg_t1, riscv_reg_t4);
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

static void riscv_emit_arm_cpsr_store_nzcv(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  translation_ptr = ptr;

  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 28);
  riscv_emit_srli(riscv_reg_t5, riscv_reg_t2, 31);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 31);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t2, 1);
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

static void riscv_emit_arm_cpsr_store_long_nzcv(u8 **ptr_ref)
{
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  riscv_emit_arm_reg_load(&ptr, riscv_reg_t6, REG_CPSR);
  riscv_emit_li(&ptr, riscv_reg_t5, 0x300000ffu);
  translation_ptr = ptr;

  riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t5);
  riscv_emit_srli(riscv_reg_t4, riscv_reg_t3, 31);
  riscv_emit_slli(riscv_reg_t4, riscv_reg_t4, 31);
  riscv_emit_sltiu(riscv_reg_t5, riscv_reg_t2, 1);
  riscv_emit_sltiu(riscv_reg_t0, riscv_reg_t3, 1);
  riscv_emit_and(riscv_reg_t5, riscv_reg_t5, riscv_reg_t0);
  riscv_emit_slli(riscv_reg_t5, riscv_reg_t5, 30);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_or(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
  ptr = translation_ptr;

  riscv_emit_arm_reg_store(&ptr, REG_CPSR, riscv_reg_t4);
  *ptr_ref = ptr;
}

bool riscv_emit_native_arm_data_proc_with_pc(u8 **translation_ptr_ref,
                                             riscv_jit_block_meta *meta,
                                             u32 opcode,
                                             u32 pc,
                                             u32 cycles)
{
  u32 condition = opcode >> 28;
  u32 op = (opcode >> 21) & 0xfu;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  bool arithmetic_flags = set_flags &&
    (op == 0x2 || op == 0x3 || op == 0x4 ||
     op == 0x5 || op == 0x6 || op == 0x7);
  bool logical_flags = set_flags &&
    (op == 0x0 || op == 0x1 || op == 0xc ||
     op == 0xd || op == 0xe || op == 0xf);
  u8 *ptr = *translation_ptr_ref;

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

  if (op != 0xd && op != 0xf)
    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, rn, pc + 8u);

  if (logical_flags)
  {
    if (!riscv_emit_arm_data_proc_operand2_with_carry(&ptr, opcode, pc))
      return false;
  }
  else if (!riscv_emit_arm_data_proc_operand2(&ptr, opcode, pc))
  {
    return false;
  }

  if (op == 0x5 || op == 0x6 || op == 0x7)
  {
    u8 *translation_ptr;

    riscv_emit_arm_reg_load(&ptr, riscv_reg_t3, REG_CPSR);
    translation_ptr = ptr;
    riscv_emit_srli(riscv_reg_t3, riscv_reg_t3, 29);
    riscv_emit_andi(riscv_reg_t3, riscv_reg_t3, 1);
    ptr = translation_ptr;
  }

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
        riscv_emit_add(riscv_reg_t2, riscv_reg_t1, riscv_reg_zero);
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

  if (arithmetic_flags)
  {
    u8 *translation_ptr = ptr;

    if (op == 0x2)
    {
      riscv_emit_sltu(riscv_reg_t3, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
      riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
    }
    else if (op == 0x3)
    {
      riscv_emit_sltu(riscv_reg_t3, riscv_reg_t1, riscv_reg_t0);
      riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
      riscv_emit_xor(riscv_reg_t4, riscv_reg_t1, riscv_reg_t0);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t1, riscv_reg_t2);
    }
    else if (op == 0x4)
    {
      riscv_emit_sltu(riscv_reg_t3, riscv_reg_t2, riscv_reg_t0);
      riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
    }
    else if (op == 0x5)
    {
      riscv_emit_add(riscv_reg_t5, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_sltu(riscv_reg_t4, riscv_reg_t5, riscv_reg_t0);
      riscv_emit_sltu(riscv_reg_t6, riscv_reg_t2, riscv_reg_t5);
      riscv_emit_or(riscv_reg_t3, riscv_reg_t4, riscv_reg_t6);
      riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
    }
    else if (op == 0x6)
    {
      riscv_emit_sltu(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, 1);
      riscv_emit_sltu(riscv_reg_t5, riscv_reg_t1, riscv_reg_t0);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t4, riscv_reg_t5);
      riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t3);
      riscv_emit_xor(riscv_reg_t3, riscv_reg_t5, riscv_reg_t6);
      riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
    }
    else
    {
      riscv_emit_sltu(riscv_reg_t4, riscv_reg_t1, riscv_reg_t0);
      riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, 1);
      riscv_emit_sltu(riscv_reg_t5, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t4, riscv_reg_t5);
      riscv_emit_and(riscv_reg_t6, riscv_reg_t6, riscv_reg_t3);
      riscv_emit_xor(riscv_reg_t3, riscv_reg_t5, riscv_reg_t6);
      riscv_emit_xor(riscv_reg_t4, riscv_reg_t1, riscv_reg_t0);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t1, riscv_reg_t2);
    }

    riscv_emit_and(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
    riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 31);
    ptr = translation_ptr;
  }

  riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_t2);
  if (rd == REG_PC)
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;
  if (arithmetic_flags)
    riscv_emit_arm_cpsr_store_nzcv(&ptr);
  else if (logical_flags)
  {
    riscv_emit_arm_cpsr_v_load(&ptr, riscv_reg_t4);
    riscv_emit_arm_cpsr_store_nzcv(&ptr);
  }
  if (rd == REG_PC && set_flags)
    riscv_emit_c_call(&ptr, (uintptr_t)riscv_execute_spsr_restore);
  riscv_emit_adjust_cycles(&ptr, cycles);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
}

bool riscv_emit_native_arm_data_proc(u8 **translation_ptr_ref,
                                     riscv_jit_block_meta *meta,
                                     u32 opcode,
                                     u32 cycles)
{
  return riscv_emit_native_arm_data_proc_with_pc(translation_ptr_ref, meta,
                                                opcode, 0, cycles);
}

bool riscv_emit_native_arm_data_proc_test_with_pc(u8 **translation_ptr_ref,
                                                  riscv_jit_block_meta *meta,
                                                  u32 opcode,
                                                  u32 pc,
                                                  u32 cycles)
{
  u32 condition = opcode >> 28;
  u32 op = (opcode >> 21) & 0xfu;
  u32 set_flags = (opcode >> 20) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 logical_test = (op == 0x8 || op == 0x9);
  u8 *ptr = *translation_ptr_ref;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || !set_flags)
    return false;

  if (!logical_test && op != 0xa && op != 0xb)
    return false;

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, rn, pc + 8u);

  if (logical_test)
  {
    if (!riscv_emit_arm_data_proc_operand2_with_carry(&ptr, opcode, pc))
      return false;
  }
  else if (!riscv_emit_arm_data_proc_operand2(&ptr, opcode, pc))
  {
    return false;
  }

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
      riscv_emit_sub(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_sltu(riscv_reg_t3, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xori(riscv_reg_t3, riscv_reg_t3, 1);
      riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
    }
    else
    {
      riscv_emit_add(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_sltu(riscv_reg_t3, riscv_reg_t2, riscv_reg_t0);
      riscv_emit_xor(riscv_reg_t4, riscv_reg_t0, riscv_reg_t1);
      riscv_emit_xori(riscv_reg_t4, riscv_reg_t4, -1);
      riscv_emit_xor(riscv_reg_t6, riscv_reg_t0, riscv_reg_t2);
    }

    if (!logical_test)
    {
      riscv_emit_and(riscv_reg_t4, riscv_reg_t4, riscv_reg_t6);
      riscv_emit_srli(riscv_reg_t4, riscv_reg_t4, 31);
    }

    ptr = translation_ptr;
  }

  if (logical_test)
    riscv_emit_arm_cpsr_v_load(&ptr, riscv_reg_t4);

  riscv_emit_arm_cpsr_store_nzcv(&ptr);
  riscv_emit_adjust_cycles(&ptr, cycles);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
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

bool riscv_emit_native_arm_multiply(u8 **translation_ptr_ref,
                                    riscv_jit_block_meta *meta,
                                    u32 opcode,
                                    u32 cycles)
{
  u32 condition = opcode >> 28;
  u32 accumulate = (opcode >> 21) & 1u;
  u32 set_flags = (opcode >> 20) & 1u;
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
  if (set_flags)
  {
    riscv_emit_arm_cpsr_c_load(&ptr, riscv_reg_t3);
    riscv_emit_arm_cpsr_v_load(&ptr, riscv_reg_t4);
    riscv_emit_arm_cpsr_store_nzcv(&ptr);
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
  u32 condition = opcode >> 28;
  u32 signed_multiply = (opcode >> 22) & 1u;
  u32 accumulate = (opcode >> 21) & 1u;
  u32 set_flags = (opcode >> 20) & 1u;
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
  if (set_flags)
    riscv_emit_arm_cpsr_store_long_nzcv(&ptr);
  riscv_emit_adjust_cycles(&ptr, cycles);

  *translation_ptr_ref = ptr;
  riscv_native_data_proc_insns++;
  return true;
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

  riscv_emit_li(&ptr, riscv_reg_t0, pc + 4u);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_li(&ptr, riscv_reg_a1,
                ((opcode >> 16) & 1u) | ((opcode >> 18) & 2u));
  riscv_emit_c_call(&ptr, use_spsr ? (uintptr_t)riscv_store_spsr
                                  : (uintptr_t)riscv_store_cpsr);
  riscv_emit_adjust_cycles(&ptr, cycles);
  riscv_emit_helper_call(&ptr, meta);

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
                                                bool patchable)
{
  u32 condition = opcode >> 28;
  u8 *ptr = *translation_ptr_ref;
  u32 target_pc;

  if (branch_source)
    *branch_source = NULL;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe)
    return false;

  if (link)
  {
    riscv_emit_li(&ptr, riscv_reg_t0, pc + 4u);
    riscv_emit_arm_reg_store(&ptr, REG_LR, riscv_reg_t0);
  }

  target_pc = pc + (u32)riscv_arm_branch_delta(opcode);
  riscv_emit_li(&ptr, riscv_reg_t0, target_pc);
  riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
  riscv_emit_adjust_cycles(&ptr, cycles);

  if (patchable)
  {
    if (branch_source)
      *branch_source = riscv_emit_unconditional_branch_patch_site(&ptr);
    else
      riscv_emit_unconditional_branch_patch_site(&ptr);
    riscv_emit_helper_call(&ptr, meta);
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
                                            false, false);
}

bool riscv_emit_native_arm_bl(u8 **translation_ptr_ref,
                              riscv_jit_block_meta *meta,
                              u32 opcode,
                              u32 pc,
                              u32 cycles)
{
  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            NULL, opcode, pc, cycles,
                                            true, false);
}

bool riscv_emit_native_arm_b_patchable(u8 **translation_ptr_ref,
                                       riscv_jit_block_meta *meta,
                                       u8 **branch_source,
                                       u32 opcode,
                                       u32 pc,
                                       u32 cycles)
{
  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            branch_source, opcode, pc,
                                            cycles, false, true);
}

bool riscv_emit_native_arm_bl_patchable(u8 **translation_ptr_ref,
                                        riscv_jit_block_meta *meta,
                                        u8 **branch_source,
                                        u32 opcode,
                                        u32 pc,
                                        u32 cycles)
{
  return riscv_emit_native_arm_direct_branch(translation_ptr_ref, meta,
                                            branch_source, opcode, pc,
                                            cycles, true, true);
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

  riscv_emit_li(&ptr, riscv_reg_a0, pc + 4u);
  riscv_emit_c_call(&ptr, (uintptr_t)riscv_execute_swi_arm);
  riscv_emit_adjust_cycles(&ptr, cycles);

  if (patchable)
  {
    if (branch_source)
      *branch_source = riscv_emit_unconditional_branch_patch_site(&ptr);
    else
      riscv_emit_unconditional_branch_patch_site(&ptr);
    riscv_emit_helper_call(&ptr, meta);
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
  riscv_emit_c_call(&ptr, (uintptr_t)riscv_hle_div);
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
  riscv_emit_li(&ptr, riscv_reg_a2, pc);
  riscv_emit_c_call(&ptr, byte ? (uintptr_t)riscv_swap_u8
                              : (uintptr_t)riscv_swap_u32);
  riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
  riscv_emit_adjust_cycles(&ptr, cycles + 3u);
  riscv_emit_helper_call(&ptr, meta);

  *translation_ptr_ref = ptr;
  riscv_native_store_insns++;
  return true;
}

bool riscv_emit_native_arm_block_memory(u8 **translation_ptr_ref,
                                        riscv_jit_block_meta *meta,
                                        u32 opcode,
                                        u32 pc,
                                        u32 cycles)
{
  u32 condition = opcode >> 28;
  u32 reglist = opcode & 0xffffu;
  u32 load = (opcode >> 20) & 1u;
  u8 *ptr = *translation_ptr_ref;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0e000000u) != 0x08000000u ||
      reglist == 0)
  {
    return false;
  }

  riscv_emit_li(&ptr, riscv_reg_a0, opcode);
  riscv_emit_li(&ptr, riscv_reg_a1, pc);
  riscv_emit_c_call(&ptr, (uintptr_t)riscv_arm_block_memory);
  riscv_emit_adjust_cycles(&ptr, cycles + riscv_word_bit_count(reglist));
  riscv_emit_helper_call(&ptr, meta);

  if (load && (reglist & (1u << REG_PC)))
    meta->flags |= RISCV_BLOCK_PC_WRITTEN;

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
  u32 rm = opcode & 0xfu;
  u32 offset = immediate_offset ? (((opcode >> 4) & 0xf0u) | rm) : rm;
  u8 *ptr = *translation_ptr_ref;
  bool writeback_address = writeback || !pre_index;
  bool pc_base = rn == REG_PC;
  riscv_reg_number writeback_reg = riscv_reg_a0;

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (opcode & 0x0e000090u) != 0x00000090u ||
      (pc_base && writeback_address) ||
      mem_type == 0 ||
      (load && rd == REG_PC &&
       (mem_type != 1 || writeback_address || !immediate_offset || pc_base)) ||
      (!load && mem_type != 1) ||
      (!immediate_offset && ((opcode >> 8) & 0xfu) != 0))
  {
    return false;
  }

  if (pc_base)
    riscv_emit_li(&ptr, riscv_reg_a0, pc + 8u);
  else
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);

  if (immediate_offset)
  {
    if (!pre_index)
    {
      riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_t2, riscv_reg_a0,
                                       offset, up);
      writeback_reg = riscv_reg_t2;
    }
    else if (offset)
    {
      riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_a0, riscv_reg_a0,
                                       offset, up);
    }
  }
  else if (!immediate_offset)
  {
    u8 *translation_ptr;

    riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, rm, pc + 8u);
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
      riscv_emit_li(&ptr, riscv_reg_a1, pc + 12u);
    else
      riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);
  }

  if (writeback_address)
    riscv_emit_arm_reg_store(&ptr, rn, writeback_reg);

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
    if (rd == REG_PC)
      meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    riscv_emit_adjust_cycles(&ptr, cycles + 2u);
    riscv_native_load_insns++;
  }
  else
  {
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

static bool riscv_emit_arm_memory_reg_offset(u8 **ptr_ref, u32 opcode, u32 pc)
{
  u32 rm = opcode & 0xfu;
  u32 shift_type = (opcode >> 5) & 0x3u;
  u32 shift = (opcode >> 7) & 0x1fu;
  u8 *ptr = *ptr_ref;
  u8 *translation_ptr;

  if ((opcode >> 4) & 1u)
    return false;

  riscv_emit_arm_reg_or_pc_load(&ptr, riscv_reg_t0, rm, pc + 8u);
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

bool riscv_emit_native_arm_access_memory(u8 **translation_ptr_ref,
                                         riscv_jit_block_meta *meta,
                                         u32 opcode,
                                         u32 pc,
                                         u32 cycles)
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

  if ((opcode & 0x0c000000u) != 0x04000000u)
  {
    return riscv_emit_native_arm_extra_memory(translation_ptr_ref, meta,
                                             opcode, pc, cycles);
  }

  if (!meta || !(meta->flags & RISCV_BLOCK_NATIVE_SUPPORTED))
    return false;

  if (condition != 0xe || (pc_base && writeback_address) ||
      (load && rd == REG_PC && byte &&
       (writeback_address || register_offset)))
  {
    return false;
  }

  if (pc_base)
    riscv_emit_li(&ptr, riscv_reg_a0, pc + 8u);
  else
    riscv_emit_arm_reg_load(&ptr, riscv_reg_a0, rn);

  if (register_offset)
  {
    u8 *translation_ptr;

    if (!riscv_emit_arm_memory_reg_offset(&ptr, opcode, pc))
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
  else if (!pre_index)
  {
    riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_t2, riscv_reg_a0,
                                     offset, up);
    writeback_reg = riscv_reg_t2;
  }
  else if (offset)
  {
    riscv_emit_arm_memory_imm_offset(&ptr, riscv_reg_a0, riscv_reg_a0,
                                     offset, up);
  }

  if (!load)
  {
    if (rd == REG_PC)
      riscv_emit_li(&ptr, riscv_reg_a1, pc + 12u);
    else
      riscv_emit_arm_reg_load(&ptr, riscv_reg_a1, rd);
  }

  if (writeback_address)
    riscv_emit_arm_reg_store(&ptr, rn, writeback_reg);

  if (load)
  {
    riscv_emit_li(&ptr, riscv_reg_t0, pc);
    riscv_emit_arm_reg_store(&ptr, REG_PC, riscv_reg_t0);
    riscv_emit_c_call(&ptr, byte ? (uintptr_t)read_memory8
                                : (uintptr_t)read_memory32);
    riscv_emit_arm_reg_store(&ptr, rd, riscv_reg_a0);
    if (rd == REG_PC)
      meta->flags |= RISCV_BLOCK_PC_WRITTEN;
    riscv_emit_adjust_cycles(&ptr, cycles + 2u);
    riscv_native_load_insns++;
  }
  else
  {
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
  riscv_initial_lookup_fallbacks = 0;
  riscv_relookup_fallbacks = 0;
  riscv_unsupported_fallbacks = 0;
  riscv_native_data_proc_insns = 0;
  riscv_native_branch_insns = 0;
  riscv_native_load_insns = 0;
  riscv_native_store_insns = 0;
  riscv_native_psr_insns = 0;
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
}

u32 execute_arm_translate(u32 cycles)
{
  return execute_arm_translate_internal(cycles, &reg[0]);
}

u32 execute_arm_translate_internal(u32 cycles, void *regptr)
{
  riscv_jit_block_fn entry;
  u8 *entry_data;
  u32 pc;
  u32 thumb;

  (void)regptr;

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

  do
  {
    entry = (riscv_jit_block_fn)entry_data;
    entry_data = entry();
  } while (entry_data && entry_data != RISCV_INVALID_BLOCK_ENTRY);

  return 0;
}
