/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "common.h"
#include "esp32s3/xtensa_emit.h"
#include "esp32s3/xtensa_hle.h"
#include "esp32s3/xtensa_native_emit.h"
#include "esp32s3/psram_static.h"
#include "esp32s3/xtensa_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef u8 *(*xtensa_jit_block_fn)(void);

extern u32 rom_cache_watermark;
void gpsp_debug_trace_cpu(u32 pc, u32 opcode, u32 thumb);
bool gpsp_debug_cpu_should_break(u32 pc, u32 opcode, u32 thumb);
bool gpsp_debug_cpu_stop_requested(void);
void gpsp_debug_dump_recent_cpu_trace(void);

enum
{
  XTENSA_INITIAL_ROM_WATERMARK = 16,
  XTENSA_COMPILED_INSN_INDEX_MASK = 0x0000FFFFu,
  XTENSA_COMPILED_INSN_RAM_BIT = 0x00010000u,
  XTENSA_COMPILED_INSN_INVALID =
    XTENSA_COMPILED_INSN_RAM_BIT | XTENSA_COMPILED_INSN_INDEX_MASK,
  XTENSA_MAX_COMPILED_ARM_INSNS = XTENSA_COMPILED_INSN_INDEX_MASK,
  XTENSA_MAX_COMPILED_THUMB_INSNS = XTENSA_COMPILED_INSN_INDEX_MASK
};

typedef struct xtensa_compiled_arm_insn
{
  u32 opcode;
  u32 pc;
} xtensa_compiled_arm_insn;

typedef struct xtensa_compiled_thumb_insn
{
  u16 opcode;
  u16 reserved;
  u32 pc;
} xtensa_compiled_thumb_insn;

static u32 xtensa_blocks_emitted;
static u32 xtensa_blocks_executed;
static u32 xtensa_generic_fallbacks;
static u32 xtensa_unsupported_opcodes;
static u32 xtensa_thumb_blocks;
static u32 xtensa_interpreter_blocks_executed;
static u32 xtensa_compiled_rom_arm_insn_count;
static u32 xtensa_compiled_rom_thumb_insn_count;
static u32 xtensa_compiled_rom_arm_insn_watermark;
static u32 xtensa_compiled_rom_thumb_insn_watermark;
static u32 xtensa_compiled_ram_arm_insn_count;
static u32 xtensa_compiled_ram_thumb_insn_count;
static u32 xtensa_native_arm_insn_count;
static u32 xtensa_helper_arm_insns_executed;
static u32 xtensa_helper_thumb_insns_executed;
static u32 xtensa_pc_guard_mismatches;
static s32 xtensa_cycles_remaining;
static cpu_alert_type xtensa_cpu_alert;
static xtensa_jit_state xtensa_state;

#define XTENSA_INVALID_BLOCK_ENTRY ((u8 *)(uintptr_t)~(uintptr_t)0)

static GPSP_EXT_RAM_BSS xtensa_compiled_arm_insn
  xtensa_compiled_rom_arm_insns[XTENSA_MAX_COMPILED_ARM_INSNS];
static GPSP_EXT_RAM_BSS xtensa_compiled_thumb_insn
  xtensa_compiled_rom_thumb_insns[XTENSA_MAX_COMPILED_THUMB_INSNS];
static GPSP_EXT_RAM_BSS xtensa_compiled_arm_insn
  xtensa_compiled_ram_arm_insns[XTENSA_MAX_COMPILED_ARM_INSNS];
static GPSP_EXT_RAM_BSS xtensa_compiled_thumb_insn
  xtensa_compiled_ram_thumb_insns[XTENSA_MAX_COMPILED_THUMB_INSNS];

typedef struct xtensa_arm_flags
{
  u32 n;
  u32 z;
  u32 c;
  u32 v;
} xtensa_arm_flags;

static void xtensa_state_sync_from_globals(xtensa_jit_state *state)
{
  memcpy(state->r, reg, sizeof(state->r));
  memcpy(state->spsr, spsr, sizeof(state->spsr));
  memcpy(state->reg_mode, reg_mode, sizeof(state->reg_mode));
  state->jit_cycles = xtensa_cycles_remaining;
  state->jit_alert = xtensa_cpu_alert;
  state->exit_reason = 0;
}

static void xtensa_state_sync_to_globals(const xtensa_jit_state *state)
{
  memcpy(reg, state->r, sizeof(state->r));
  memcpy(spsr, state->spsr, sizeof(state->spsr));
  memcpy(reg_mode, state->reg_mode, sizeof(state->reg_mode));
  xtensa_cycles_remaining = state->jit_cycles;
  xtensa_cpu_alert = (cpu_alert_type)state->jit_alert;
}

static void xtensa_extract_flags(xtensa_arm_flags *flags)
{
  flags->n = reg[REG_CPSR] >> 31;
  flags->z = (reg[REG_CPSR] >> 30) & 0x01;
  flags->c = (reg[REG_CPSR] >> 29) & 0x01;
  flags->v = (reg[REG_CPSR] >> 28) & 0x01;
}

static void xtensa_collapse_flags(const xtensa_arm_flags *flags)
{
  reg[REG_CPSR] = (flags->n << 31) | (flags->z << 30) |
                  (flags->c << 29) | (flags->v << 28) |
                  (reg[REG_CPSR] & 0xFF);
}

static const u32 xtensa_cpsr_masks[4][2] =
{
  {0x00000000, 0x00000000},
  {0x00000020, 0x000000EF},
  {0xF0000000, 0xF0000000},
  {0xF0000020, 0xF00000EF}
};

static const u32 xtensa_spsr_masks[4] =
{
  0x00000000, 0x000000EF, 0xF0000000, 0xF00000EF
};

static void xtensa_store_cpsr(u32 source, u32 psr_pfield,
                              xtensa_arm_flags *flags,
                              cpu_alert_type *cpu_alert)
{
  u32 store_mask;

  xtensa_collapse_flags(flags);
  store_mask = xtensa_cpsr_masks[psr_pfield][PRIVMODE(reg[CPU_MODE])];
  reg[REG_CPSR] = (source & store_mask) | (reg[REG_CPSR] & ~store_mask);
  xtensa_extract_flags(flags);

  if (store_mask & 0xFF)
  {
    set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0x0F]);
    if (check_and_raise_interrupts())
      *cpu_alert |= CPU_ALERT_IRQ;
    xtensa_extract_flags(flags);
  }
}

static void xtensa_store_spsr(u32 source, u32 psr_pfield)
{
  u32 store_mask = xtensa_spsr_masks[psr_pfield];
  u32 psr = REG_SPSR(reg[CPU_MODE]);
  REG_SPSR(reg[CPU_MODE]) = (source & store_mask) | (psr & ~store_mask);
}

static bool xtensa_condition_passed(u32 condition, const xtensa_arm_flags *flags)
{
  switch (condition)
  {
    case 0x0:
      return flags->z != 0;
    case 0x1:
      return flags->z == 0;
    case 0x2:
      return flags->c != 0;
    case 0x3:
      return flags->c == 0;
    case 0x4:
      return flags->n != 0;
    case 0x5:
      return flags->n == 0;
    case 0x6:
      return flags->v != 0;
    case 0x7:
      return flags->v == 0;
    case 0x8:
      return (flags->c != 0) && (flags->z == 0);
    case 0x9:
      return (flags->c == 0) || (flags->z != 0);
    case 0xA:
      return flags->n == flags->v;
    case 0xB:
      return flags->n != flags->v;
    case 0xC:
      return (flags->z == 0) && (flags->n == flags->v);
    case 0xD:
      return (flags->z != 0) || (flags->n != flags->v);
    case 0xE:
      return true;
    default:
      return false;
  }
}

static void xtensa_calculate_flags_logic(xtensa_arm_flags *flags, u32 dest)
{
  flags->z = (dest == 0);
  flags->n = ((s32)dest < 0);
}

static void xtensa_calculate_flags_add(xtensa_arm_flags *flags, u32 dest,
                                       u32 src_a, u32 src_b)
{
  xtensa_calculate_flags_logic(flags, dest);
  flags->v = ((~(src_a ^ src_b) & (src_a ^ dest)) >> 31);
}

static void xtensa_calculate_flags_sub(xtensa_arm_flags *flags, u32 dest,
                                       u32 src_a, u32 src_b, u32 carry)
{
  xtensa_calculate_flags_logic(flags, dest);
  flags->c = carry ? (src_b <= src_a) : (src_b < src_a);
  flags->v = (((src_a ^ src_b) & (~src_b ^ dest)) >> 31);
}

static inline u32 xtensa_ror32(u32 value, u32 shift)
{
  shift &= 31;
  if (shift == 0)
    return value;
  return (value >> shift) | (value << (32 - shift));
}

static u32 xtensa_read_arm_reg(u32 current_pc, u32 reg_num, u32 pc_bias)
{
  if (reg_num == REG_PC)
    return current_pc + pc_bias;
  return reg[reg_num];
}

static void xtensa_account_nseq_cycles(s32 *cycles_remaining, u32 address,
                                       u32 size_bits)
{
  if (address < 0x10000000)
  {
    u32 region = address >> 24;
    *cycles_remaining -= ws_cyc_nseq[region][(size_bits - 8) / 16];
  }
}

static void xtensa_account_seq_cycles(s32 *cycles_remaining, u32 address)
{
  if (address < 0x10000000)
  {
    u32 region = address >> 24;
    *cycles_remaining -= ws_cyc_seq[region][1];
  }
}

static u32 xtensa_fetch_arm_opcode(u32 pc)
{
  u32 pc_region = pc >> 15;
  u8 *pc_address_block = memory_map_read[pc_region];

  if (!pc_address_block)
    pc_address_block = load_gamepak_page(pc_region & 0x3FF);

  touch_gamepak_page(pc_region);
  return readaddress32(pc_address_block, pc & 0x7FFF);
}

static u16 xtensa_fetch_thumb_opcode(u32 pc)
{
  u32 pc_region = pc >> 15;
  u8 *pc_address_block = memory_map_read[pc_region];

  if (!pc_address_block)
    pc_address_block = load_gamepak_page(pc_region & 0x3FF);

  touch_gamepak_page(pc_region);
  return readaddress16(pc_address_block, pc & 0x7FFF);
}

static bool xtensa_thumb_bl_pair_entry(u32 live_pc,
                                       const xtensa_compiled_thumb_insn *insn)
{
  u16 low_opcode;

  if ((live_pc + 2) != (insn->pc & ~1u))
    return false;
  if (insn->opcode < 0xF800)
    return false;

  low_opcode = xtensa_fetch_thumb_opcode(live_pc);
  return low_opcode >= 0xF000 && low_opcode < 0xF800;
}

static u32 xtensa_load_u8(u32 address, s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 8);
  return read_memory8(address);
}

static u32 xtensa_load_s8(u32 address, s32 *cycles_remaining)
{
  return (u32)(s32)(s8)xtensa_load_u8(address, cycles_remaining);
}

static u32 xtensa_load_u16(u32 address, s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 16);
  return read_memory16(address);
}

static u32 xtensa_load_s16(u32 address, s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 16);
  return (u32)(s32)(s16)read_memory16_signed(address);
}

static u32 xtensa_load_u32(u32 address, s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 32);
  return read_memory32(address);
}

static cpu_alert_type xtensa_mem_store_u8(u32 address, u32 value,
                                          s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 8);
  return write_memory8(address, (u8)value);
}

static cpu_alert_type xtensa_mem_store_u16(u32 address, u32 value,
                                           s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 16);
  return write_memory16(address, (u16)value);
}

static cpu_alert_type xtensa_mem_store_u32(u32 address, u32 value,
                                           s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 32);
  return write_memory32(address, value);
}

static u32 xtensa_arm_load_value(u32 address, u32 mem_type,
                                 s32 *cycles_remaining)
{
  switch (mem_type)
  {
    case 0:
      return xtensa_load_u8(address, cycles_remaining);
    case 1:
      return xtensa_load_u16(address, cycles_remaining);
    case 2:
      return xtensa_load_u32(address, cycles_remaining);
    case 3:
      return xtensa_load_s8(address, cycles_remaining);
    default:
      return xtensa_load_s16(address, cycles_remaining);
  }
}

static cpu_alert_type xtensa_arm_store_value(u32 address, u32 value,
                                             u32 mem_type,
                                             s32 *cycles_remaining)
{
  switch (mem_type)
  {
    case 0:
      return xtensa_mem_store_u8(address, value, cycles_remaining);
    case 1:
      return xtensa_mem_store_u16(address, value, cycles_remaining);
    default:
      return xtensa_mem_store_u32(address, value, cycles_remaining);
  }
}

static u32 xtensa_load_aligned32(u32 address, s32 *cycles_remaining)
{
  xtensa_account_seq_cycles(cycles_remaining, address);
  return read_memory32(address);
}

static cpu_alert_type xtensa_store_aligned32(u32 address, u32 value,
                                             s32 *cycles_remaining)
{
  xtensa_account_seq_cycles(cycles_remaining, address);
  return write_memory32(address, value);
}

static u32 xtensa_align_pc_for_current_mode(u32 address)
{
  return (reg[REG_CPSR] & 0x20) ? (address & ~1u) : (address & ~3u);
}

static void xtensa_restore_spsr(xtensa_arm_flags *flags,
                                cpu_alert_type *cpu_alert)
{
  if (reg[CPU_MODE] != MODE_USER && reg[CPU_MODE] != MODE_SYSTEM)
  {
    reg[REG_CPSR] = REG_SPSR(reg[CPU_MODE]);
    xtensa_extract_flags(flags);
    set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);
    if (check_and_raise_interrupts())
      *cpu_alert |= CPU_ALERT_IRQ;
    xtensa_extract_flags(flags);
  }
}

static void xtensa_exec_hle_div(bool divarm)
{
  xtensa_hle_div_result result = divarm ?
    xtensa_hle_divide(reg[1], reg[0]) : xtensa_hle_divide(reg[0], reg[1]);

  reg[0] = result.quotient;
  reg[1] = result.remainder;
  reg[3] = result.abs_quotient;
}

static u32 xtensa_eval_operand2(u32 current_pc, u32 opcode,
                                xtensa_arm_flags *flags,
                                bool update_carry)
{
  if (opcode & (1u << 25))
  {
    u32 imm = opcode & 0xFF;
    u32 imm_ror = ((opcode >> 8) & 0x0F) * 2;
    u32 value = xtensa_ror32(imm, imm_ror);

    if (update_carry && imm_ror != 0)
      flags->c = value >> 31;

    return value;
  }

  {
    u32 rm = opcode & 0x0F;
    u32 shift_kind = (opcode >> 4) & 0x07;
    u32 value;

    if ((shift_kind & 0x01) != 0)
      value = xtensa_read_arm_reg(current_pc, rm, 12);
    else
      value = xtensa_read_arm_reg(current_pc, rm, 8);

    switch (shift_kind)
    {
      case 0x0:
      {
        u32 imm = (opcode >> 7) & 0x1F;
        if (update_carry && imm != 0)
          flags->c = (value >> (32 - imm)) & 0x01;
        return value << imm;
      }

      case 0x1:
      {
        u32 shift = reg[(opcode >> 8) & 0x0F] & 0xFF;
        if (update_carry && shift != 0)
        {
          if (shift > 31)
          {
            flags->c = (shift == 32) ? (value & 0x01) : 0;
            return 0;
          }
          flags->c = (value >> (32 - shift)) & 0x01;
        }
        return (shift <= 31) ? (value << shift) : 0;
      }

      case 0x2:
      {
        u32 imm = (opcode >> 7) & 0x1F;
        if (imm == 0)
        {
          if (update_carry)
            flags->c = value >> 31;
          return 0;
        }
        if (update_carry)
          flags->c = (value >> (imm - 1)) & 0x01;
        return value >> imm;
      }

      case 0x3:
      {
        u32 shift = reg[(opcode >> 8) & 0x0F] & 0xFF;
        if (shift == 0)
          return value;
        if (shift > 31)
        {
          if (update_carry)
            flags->c = (shift == 32) ? ((value >> 31) & 0x01) : 0;
          return 0;
        }
        if (update_carry)
          flags->c = (value >> (shift - 1)) & 0x01;
        return value >> shift;
      }

      case 0x4:
      {
        u32 imm = (opcode >> 7) & 0x1F;
        if (imm == 0)
        {
          u32 result = (u32)((s32)value >> 31);
          if (update_carry)
            flags->c = result & 0x01;
          return result;
        }
        if (update_carry)
          flags->c = (value >> (imm - 1)) & 0x01;
        return (u32)((s32)value >> imm);
      }

      case 0x5:
      {
        u32 shift = reg[(opcode >> 8) & 0x0F] & 0xFF;
        if (shift == 0)
          return value;
        if (shift > 31)
        {
          u32 result = (u32)((s32)value >> 31);
          if (update_carry)
            flags->c = result & 0x01;
          return result;
        }
        if (update_carry)
          flags->c = (value >> (shift - 1)) & 0x01;
        return (u32)((s32)value >> shift);
      }

      case 0x6:
      {
        u32 imm = (opcode >> 7) & 0x1F;
        if (imm == 0)
        {
          u32 old_c = flags->c;
          if (update_carry)
            flags->c = value & 0x01;
          return (value >> 1) | (old_c << 31);
        }
        if (update_carry)
          flags->c = (value >> (imm - 1)) & 0x01;
        return xtensa_ror32(value, imm);
      }

      default:
      {
        u32 shift = reg[(opcode >> 8) & 0x0F] & 0xFF;
        if (shift == 0)
          return value;
        if (update_carry)
          flags->c = (value >> ((shift - 1) & 31)) & 0x01;
        return xtensa_ror32(value, shift);
      }
    }
  }
}

static u32 xtensa_eval_reg_offset(u32 current_pc, u32 opcode,
                                  const xtensa_arm_flags *flags)
{
  u32 rm = opcode & 0x0F;
  u32 value = xtensa_read_arm_reg(current_pc, rm, 8);

  switch ((opcode >> 5) & 0x03)
  {
    case 0x0:
      return value << ((opcode >> 7) & 0x1F);

    case 0x1:
    {
      u32 imm = (opcode >> 7) & 0x1F;
      return (imm == 0) ? 0 : (value >> imm);
    }

    case 0x2:
    {
      u32 imm = (opcode >> 7) & 0x1F;
      return (imm == 0) ? (u32)((s32)value >> 31)
                        : (u32)((s32)value >> imm);
    }

    default:
    {
      u32 imm = (opcode >> 7) & 0x1F;
      if (imm == 0)
        return (value >> 1) | (flags->c << 31);
      return xtensa_ror32(value, imm);
    }
  }
}

static cpu_alert_type xtensa_exec_arm_block_transfer(u32 current_pc, u32 opcode,
                                                     s32 *cycles_remaining)
{
  cpu_alert_type cpu_alert = CPU_ALERT_NONE;
  u32 rn = (opcode >> 16) & 0x0F;
  u32 reg_list = opcode & 0xFFFF;
  bool load = ((opcode >> 20) & 0x01) != 0;
  bool writeback = ((opcode >> 21) & 0x01) != 0;
  bool sbit = ((opcode >> 22) & 0x01) != 0;
  bool up = ((opcode >> 23) & 0x01) != 0;
  bool pre = ((opcode >> 24) & 0x01) != 0;
  u32 base = xtensa_read_arm_reg(current_pc, rn, 8);
  u32 numops = bit_count[reg_list >> 8] + bit_count[reg_list & 0xFF];
  s32 addr_off = up ? 4 : -4;
  u32 endaddr = base + addr_off * numops;
  u32 address;
  bool wrbck_base = ((1u << rn) & reg_list) != 0;
  bool base_first = (((1u << rn) - 1) & reg_list) == 0;
  bool writeback_first = load || !(wrbck_base && base_first);
  u32 old_cpsr = reg[REG_CPSR];
  u32 i;

  if (pre)
    address = up ? (base + 4) : endaddr;
  else
    address = up ? base : (endaddr + 4);

  address &= ~3u;

  if (sbit && (!load || rn != REG_PC))
    set_cpu_mode(MODE_USER);

  if (writeback && writeback_first)
    reg[rn] = endaddr;

  reg[REG_PC] = current_pc + 4;

  for (i = 0; i < 16; i++)
  {
    if (((reg_list >> i) & 0x01) == 0)
      continue;

    if (load)
    {
      reg[i] = xtensa_load_aligned32(address, cycles_remaining);
    }
    else
    {
      u32 value = (i == REG_PC) ? (current_pc + 8) : reg[i];
      cpu_alert |= xtensa_store_aligned32(address, value, cycles_remaining);
    }

    address += 4;
  }

  if (writeback && !writeback_first)
    reg[rn] = endaddr;

  if (sbit && (!load || rn != REG_PC))
    set_cpu_mode(cpu_modes[old_cpsr & 0xF]);

  return cpu_alert;
}

static bool xtensa_exec_arm_extra_transfer(u32 current_pc, u32 opcode,
                                           xtensa_arm_flags *flags,
                                           s32 *cycles_remaining,
                                           cpu_alert_type *cpu_alert)
{
  u32 top = (opcode >> 20) & 0xFF;
  u32 transfer_kind = (opcode >> 5) & 0x03;
  bool pre = ((opcode >> 24) & 0x01) != 0;
  bool up = ((opcode >> 23) & 0x01) != 0;
  bool immediate = ((opcode >> 22) & 0x01) != 0;
  bool writeback = !pre || (((opcode >> 21) & 0x01) != 0);
  bool load = ((opcode >> 20) & 0x01) != 0;
  u32 rn = (opcode >> 16) & 0x0F;
  u32 rd = (opcode >> 12) & 0x0F;
  u32 rs = (opcode >> 8) & 0x0F;
  u32 rm = opcode & 0x0F;
  u32 next_pc = current_pc + 4;

  if ((opcode & 0x90) != 0x90 || top >= 0x20)
    return false;

  if (transfer_kind == 0)
  {
    if (top >= 0x08 && top <= 0x0F)
    {
      bool signed_multiply = ((opcode >> 22) & 0x01) != 0;
      bool accumulate = ((opcode >> 21) & 0x01) != 0;
      bool setflags = ((opcode >> 20) & 0x01) != 0;
      u64 result;
      u32 dest_lo;
      u32 dest_hi;

      if (signed_multiply)
      {
        s64 signed_result = (s64)(s32)reg[rm] * (s64)(s32)reg[rs];
        if (accumulate)
          signed_result += (s64)(((u64)reg[(opcode >> 16) & 0x0F] << 32) |
                                 reg[(opcode >> 12) & 0x0F]);
        result = (u64)signed_result;
      }
      else
      {
        result = (u64)reg[rm] * (u64)reg[rs];
        if (accumulate)
          result += ((u64)reg[(opcode >> 16) & 0x0F] << 32) |
                    reg[(opcode >> 12) & 0x0F];
      }

      dest_lo = (u32)result;
      dest_hi = (u32)(result >> 32);
      reg[(opcode >> 12) & 0x0F] = dest_lo;
      reg[(opcode >> 16) & 0x0F] = dest_hi;
      reg[REG_PC] = next_pc;

      if (setflags)
      {
        flags->z = (dest_lo == 0 && dest_hi == 0);
        flags->n = dest_hi >> 31;
      }

      return true;
    }

    if (top == 0x10 || top == 0x14)
    {
      bool byte = (top == 0x14);
      u32 address = xtensa_read_arm_reg(current_pc, rn, 8);
      u32 value = byte ? xtensa_load_u8(address, cycles_remaining)
                       : xtensa_load_u32(address, cycles_remaining);

      *cpu_alert |= byte ?
        xtensa_mem_store_u8(address, reg[rm], cycles_remaining) :
        xtensa_mem_store_u32(address, reg[rm], cycles_remaining);
      reg[rd] = value;
      reg[REG_PC] = next_pc;
      return true;
    }

    return false;
  }

  {
    u32 base = xtensa_read_arm_reg(current_pc, rn, 8);
    u32 offset = immediate ?
      (((opcode >> 4) & 0xF0) | (opcode & 0x0F)) :
      xtensa_read_arm_reg(current_pc, rm, 8);
    u32 writeback_value = up ? (base + offset) : (base - offset);
    u32 address = pre ? writeback_value : base;
    u32 mem_type = (transfer_kind == 1) ? 1 :
                   (transfer_kind == 2) ? 3 : 4;

    if (!load && transfer_kind != 1)
      return false;

    if (writeback)
      reg[rn] = writeback_value;

    if (load)
    {
      reg[rd] = xtensa_arm_load_value(address, mem_type, cycles_remaining);
      if (rd == REG_PC)
        reg[REG_PC] &= ~0x03;
      else
        reg[REG_PC] = next_pc;
    }
    else
    {
      u32 value = (rd == REG_PC) ? (current_pc + 12) : reg[rd];
      *cpu_alert |= xtensa_arm_store_value(address, value, 1,
                                           cycles_remaining);
      reg[REG_PC] = next_pc;
    }

    return true;
  }
}

typedef enum xtensa_access_mode
{
  XTENSA_ACC_LOAD,
  XTENSA_ACC_STORE
} xtensa_access_mode;

typedef enum xtensa_addr_mode
{
  XTENSA_ADDR_PRE_INC,
  XTENSA_ADDR_PRE_DEC,
  XTENSA_ADDR_POST_INC,
  XTENSA_ADDR_POST_DEC
} xtensa_addr_mode;

static cpu_alert_type xtensa_exec_thumb_block_transfer(u32 current_pc, u32 rn,
                                                       u32 reg_list,
                                                       xtensa_access_mode mode,
                                                       xtensa_addr_mode addr_mode,
                                                       s32 *cycles_remaining)
{
  cpu_alert_type cpu_alert = CPU_ALERT_NONE;
  u32 base = reg[rn];
  u32 numops = bit_count[reg_list & 0xFF] +
               (bit_count[reg_list >> 8] ? 1 : 0);
  s32 addr_off = (addr_mode == XTENSA_ADDR_PRE_INC ||
                  addr_mode == XTENSA_ADDR_POST_INC) ? 4 : -4;
  u32 endaddr = base + addr_off * numops;
  u32 address = (addr_mode == XTENSA_ADDR_PRE_INC) ? base + 4 :
                (addr_mode == XTENSA_ADDR_POST_INC) ? base :
                (addr_mode == XTENSA_ADDR_PRE_DEC) ? endaddr : endaddr + 4;
  bool wrbck_base = ((1u << rn) & reg_list) != 0;
  bool base_first = (((1u << rn) - 1) & reg_list) == 0;
  bool writeback_first = (mode == XTENSA_ACC_LOAD) ||
                         !(wrbck_base && base_first);
  u32 i;

  address &= ~3u;

  if (writeback_first)
    reg[rn] = endaddr;

  reg[REG_PC] = current_pc + 2;

  if (mode == XTENSA_ACC_LOAD)
  {
    for (i = 0; i < 8; i++)
    {
      if ((reg_list >> i) & 0x01)
      {
        reg[i] = xtensa_load_aligned32(address, cycles_remaining);
        address += 4;
      }
    }

    if (reg_list & (1u << REG_PC))
    {
      reg[REG_PC] = xtensa_load_aligned32(address, cycles_remaining) & ~1u;
    }
  }
  else
  {
    for (i = 0; i < 8; i++)
    {
      if ((reg_list >> i) & 0x01)
      {
        cpu_alert |= xtensa_store_aligned32(address, reg[i],
                                            cycles_remaining);
        address += 4;
      }
    }

    if (reg_list & (1u << REG_LR))
    {
      cpu_alert |= xtensa_store_aligned32(address, reg[REG_LR],
                                          cycles_remaining);
    }
  }

  if (!writeback_first)
    reg[rn] = endaddr;

  return cpu_alert;
}

static bool xtensa_exec_arm_instruction(const xtensa_jit_block_meta *meta,
                                        u32 opcode, xtensa_arm_flags *flags,
                                        s32 *cycles_remaining,
                                        cpu_alert_type *cpu_alert)
{
  (void)meta;
  u32 current_pc = reg[REG_PC] & ~0x03;
  u32 condition = opcode >> 28;
  u32 next_pc = current_pc + 4;

  if (!xtensa_condition_passed(condition, flags))
  {
    reg[REG_PC] = next_pc;
    return true;
  }

  if ((opcode & 0x0FFFFFF0) == 0x012FFF10)
  {
    u32 rn = opcode & 0x0F;
    u32 src = xtensa_read_arm_reg(current_pc, rn, 8);

    if (src & 0x01)
    {
      reg[REG_PC] = src - 1;
      reg[REG_CPSR] |= 0x20;
    }
    else
    {
      reg[REG_PC] = src;
    }

    *cycles_remaining -= ws_cyc_nseq[reg[REG_PC] >> 24][1];
    return true;
  }

  if ((opcode & 0x0E000000) == 0x0A000000)
  {
    s32 offset = ((s32)(opcode & 0x00FFFFFF) << 8) >> 6;

    if (opcode & (1u << 24))
      reg[REG_LR] = current_pc + 4;

    reg[REG_PC] = current_pc + offset + 8;
    *cycles_remaining -= ws_cyc_nseq[reg[REG_PC] >> 24][1];
    return true;
  }

  if ((opcode & 0x0E000000) == 0x08000000)
  {
    *cpu_alert |= xtensa_exec_arm_block_transfer(current_pc, opcode,
                                                 cycles_remaining);
    if (((opcode >> 20) & 0x01) != 0 && (opcode & 0x8000) != 0)
      reg[REG_PC] &= ~0x03;
    return true;
  }

  if ((opcode & 0x0C000000) == 0x04000000)
  {
    u32 rn = (opcode >> 16) & 0x0F;
    u32 rd = (opcode >> 12) & 0x0F;
    bool load = ((opcode >> 20) & 0x01) != 0;
    bool writeback = ((opcode >> 21) & 0x01) != 0;
    bool byte = ((opcode >> 22) & 0x01) != 0;
    bool up = ((opcode >> 23) & 0x01) != 0;
    bool pre = ((opcode >> 24) & 0x01) != 0;
    u32 base = xtensa_read_arm_reg(current_pc, rn, 8);
    u32 offset = (opcode & (1u << 25)) ?
      xtensa_eval_reg_offset(current_pc, opcode, flags) :
      (opcode & 0x0FFF);
    u32 address = pre ? (up ? (base + offset) : (base - offset)) : base;
    u32 writeback_value = up ? (base + offset) : (base - offset);

    if (load)
    {
      u32 value = byte ? xtensa_load_u8(address, cycles_remaining)
                       : xtensa_load_u32(address, cycles_remaining);
      reg[rd] = value;

      if (rd == REG_PC)
      {
        reg[REG_PC] &= ~0x03;
        return true;
      }
    }
    else
    {
      u32 value = (rd == REG_PC) ? (current_pc + 12) : reg[rd];
      *cpu_alert |= byte ? xtensa_mem_store_u8(address, value, cycles_remaining)
                         : xtensa_mem_store_u32(address, value, cycles_remaining);
    }

    if (writeback || !pre)
      reg[rn] = writeback_value;

    reg[REG_PC] = next_pc;
    return true;
  }

  if (xtensa_exec_arm_extra_transfer(current_pc, opcode, flags,
                                     cycles_remaining, cpu_alert))
    return true;

  if ((opcode & 0x0FC000F0) == 0x00000090)
  {
    u32 rd = (opcode >> 16) & 0x0F;
    u32 rn = (opcode >> 12) & 0x0F;
    u32 rs = (opcode >> 8) & 0x0F;
    u32 rm = opcode & 0x0F;
    bool accumulate = ((opcode >> 21) & 0x01) != 0;
    bool setflags = ((opcode >> 20) & 0x01) != 0;
    u32 result = reg[rm] * reg[rs];

    if (accumulate)
      result += reg[rn];

    reg[rd] = result;
    reg[REG_PC] = next_pc;

    if (setflags)
      xtensa_calculate_flags_logic(flags, result);

    return true;
  }

  if ((opcode & 0x0F000000) == 0x0F000000)
  {
    u32 swinum = (opcode >> 16) & 0xFF;

    if ((swinum & 0xFE) == 0x06)
    {
      xtensa_exec_hle_div(swinum == 0x07);
      reg[REG_PC] = next_pc;
      *cycles_remaining -= 64;
      return true;
    }

    reg[REG_BUS_VALUE] = 0xe3a02004;
    REG_MODE(MODE_SUPERVISOR)[6] = current_pc + 4;
    REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];
    reg[REG_PC] = 0x00000008;
    reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3F) | 0x13 | 0x80;
    set_cpu_mode(MODE_SUPERVISOR);
    xtensa_extract_flags(flags);
    return true;
  }

  if (((opcode >> 24) & 0x0F) >= 0x0C)
  {
    reg[REG_PC] = next_pc;
    return true;
  }

  switch ((opcode >> 20) & 0xFF)
  {
    case 0x10:
      if ((opcode & 0x90) != 0x90)
      {
        u32 rd = (opcode >> 12) & 0x0F;
        xtensa_collapse_flags(flags);
        reg[rd] = reg[REG_CPSR];
        reg[REG_PC] = next_pc;
        return true;
      }
      break;

    case 0x12:
      if ((opcode & 0x90) != 0x90 && (opcode & 0x10) == 0)
      {
        u32 psr_pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
        u32 rm = opcode & 0x0F;
        reg[REG_PC] = next_pc;
        xtensa_store_cpsr(reg[rm], psr_pfield, flags, cpu_alert);
        return true;
      }
      break;

    case 0x14:
      if ((opcode & 0x90) != 0x90)
      {
        u32 rd = (opcode >> 12) & 0x0F;
        reg[rd] = REG_SPSR(reg[CPU_MODE]);
        reg[REG_PC] = next_pc;
        return true;
      }
      break;

    case 0x16:
      if ((opcode & 0x90) != 0x90)
      {
        u32 psr_pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
        u32 rm = opcode & 0x0F;
        reg[REG_PC] = next_pc;
        xtensa_store_spsr(reg[rm], psr_pfield);
        return true;
      }
      break;

    case 0x32:
    {
      u32 psr_pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
      u32 source = xtensa_eval_operand2(current_pc, opcode, flags, false);
      reg[REG_PC] = next_pc;
      xtensa_store_cpsr(source, psr_pfield, flags, cpu_alert);
      return true;
    }

    case 0x36:
    {
      u32 psr_pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
      u32 source = xtensa_eval_operand2(current_pc, opcode, flags, false);
      reg[REG_PC] = next_pc;
      xtensa_store_spsr(source, psr_pfield);
      return true;
    }
  }

  if ((opcode & 0x0C000000) == 0x00000000)
  {
    u32 op = (opcode >> 21) & 0x0F;
    bool setflags = ((opcode >> 20) & 0x01) != 0;
    u32 rd = (opcode >> 12) & 0x0F;
    u32 rn = (opcode >> 16) & 0x0F;
    u32 lhs = xtensa_read_arm_reg(current_pc, rn, 8);
    u32 rhs = xtensa_eval_operand2(current_pc, opcode, flags, setflags);
    u32 dest = 0;
    bool store_result = true;

    switch (op)
    {
      case 0x0:
        dest = lhs & rhs;
        if (setflags)
          xtensa_calculate_flags_logic(flags, dest);
        break;

      case 0x1:
        dest = lhs ^ rhs;
        if (setflags)
          xtensa_calculate_flags_logic(flags, dest);
        break;

      case 0x2:
        dest = lhs - rhs;
        if (setflags)
          xtensa_calculate_flags_sub(flags, dest, lhs, rhs, 1);
        break;

      case 0x3:
        dest = rhs - lhs;
        if (setflags)
          xtensa_calculate_flags_sub(flags, dest, rhs, lhs, 1);
        break;

      case 0x4:
        dest = lhs + rhs;
        if (setflags)
        {
          flags->c = (dest < rhs);
          xtensa_calculate_flags_add(flags, dest, lhs, rhs);
        }
        break;

      case 0x5:
      {
        u32 carry = flags->c;
        dest = lhs + rhs;
        dest += carry;
        if (setflags)
        {
          flags->c = (dest < carry) || ((lhs + rhs) < rhs);
          xtensa_calculate_flags_add(flags, dest, lhs, rhs);
        }
        break;
      }

      case 0x6:
      {
        u32 carry = flags->c;
        dest = lhs + (~rhs) + carry;
        if (setflags)
          xtensa_calculate_flags_sub(flags, dest, lhs, rhs, carry);
        break;
      }

      case 0x7:
      {
        u32 carry = flags->c;
        dest = rhs + (~lhs) + carry;
        if (setflags)
          xtensa_calculate_flags_sub(flags, dest, rhs, lhs, carry);
        break;
      }

      case 0x8:
        store_result = false;
        xtensa_calculate_flags_logic(flags, lhs & rhs);
        break;

      case 0x9:
        store_result = false;
        xtensa_calculate_flags_logic(flags, lhs ^ rhs);
        break;

      case 0xA:
        store_result = false;
        dest = lhs - rhs;
        xtensa_calculate_flags_sub(flags, dest, lhs, rhs, 1);
        break;

      case 0xB:
        store_result = false;
        dest = lhs + rhs;
        flags->c = (dest < rhs);
        xtensa_calculate_flags_add(flags, dest, lhs, rhs);
        break;

      case 0xC:
        dest = lhs | rhs;
        if (setflags)
          xtensa_calculate_flags_logic(flags, dest);
        break;

      case 0xD:
        dest = rhs;
        if (setflags)
          xtensa_calculate_flags_logic(flags, dest);
        break;

      case 0xE:
        dest = lhs & (~rhs);
        if (setflags)
          xtensa_calculate_flags_logic(flags, dest);
        break;

      case 0xF:
        dest = ~rhs;
        if (setflags)
          xtensa_calculate_flags_logic(flags, dest);
        break;
    }

    if (store_result)
    {
      if (rd == REG_PC)
      {
        reg[REG_PC] = dest;
        if (setflags)
          xtensa_restore_spsr(flags, cpu_alert);
        reg[REG_PC] = xtensa_align_pc_for_current_mode(reg[REG_PC]);
      }
      else
      {
        reg[rd] = dest;
        reg[REG_PC] = next_pc;
      }
    }
    else
    {
      reg[REG_PC] = next_pc;
    }

    return true;
  }

  xtensa_unsupported_opcodes++;
  printf("xtensa-arm unsupported pc=%08x opcode=%08x\n", current_pc, opcode);
  return false;
}

static u32 xtensa_read_thumb_reg(u32 current_pc, u32 reg_num)
{
  if (reg_num == REG_PC)
    return current_pc + 4;
  return reg[reg_num];
}

static void xtensa_thumb_logic_flags(xtensa_arm_flags *flags, u32 dest)
{
  xtensa_calculate_flags_logic(flags, dest);
}

static u32 xtensa_thumb_add_flags(xtensa_arm_flags *flags, u32 lhs, u32 rhs,
                                  u32 carry)
{
  u32 dest = lhs + rhs;
  flags->c = (dest < rhs);
  dest += carry;
  flags->c |= (dest < carry);
  xtensa_calculate_flags_add(flags, dest, lhs, rhs);
  return dest;
}

static u32 xtensa_thumb_sub_flags(xtensa_arm_flags *flags, u32 lhs, u32 rhs,
                                  u32 carry)
{
  u32 dest = lhs + (~rhs) + carry;
  xtensa_calculate_flags_sub(flags, dest, lhs, rhs, carry);
  return dest;
}

static u32 xtensa_thumb_shift_imm(u16 opcode, xtensa_arm_flags *flags)
{
  u32 imm = (opcode >> 6) & 0x1F;
  u32 rs = (opcode >> 3) & 0x07;
  u32 op = (opcode >> 11) & 0x03;
  u32 dest = reg[rs];

  switch (op)
  {
    case 0:
      if (imm != 0)
      {
        flags->c = (dest >> (32 - imm)) & 0x01;
        dest <<= imm;
      }
      break;

    case 1:
      if (imm == 0)
      {
        flags->c = dest >> 31;
        dest = 0;
      }
      else
      {
        flags->c = (dest >> (imm - 1)) & 0x01;
        dest >>= imm;
      }
      break;

    default:
      if (imm == 0)
      {
        dest = (u32)((s32)dest >> 31);
        flags->c = dest & 0x01;
      }
      else
      {
        flags->c = (dest >> (imm - 1)) & 0x01;
        dest = (u32)((s32)dest >> imm);
      }
      break;
  }

  xtensa_thumb_logic_flags(flags, dest);
  return dest;
}

static u32 xtensa_thumb_shift_reg(u16 opcode, xtensa_arm_flags *flags)
{
  u32 rs = (opcode >> 3) & 0x07;
  u32 rd = opcode & 0x07;
  u32 op = (opcode >> 6) & 0x0F;
  u32 shift = reg[rs];
  u32 dest = reg[rd];

  if (shift != 0)
  {
    switch (op)
    {
      case 0x2:
        if (shift > 31)
        {
          flags->c = (shift == 32) ? (dest & 0x01) : 0;
          dest = 0;
        }
        else
        {
          flags->c = (dest >> (32 - shift)) & 0x01;
          dest <<= shift;
        }
        break;

      case 0x3:
        if (shift > 31)
        {
          flags->c = (shift == 32) ? (dest >> 31) : 0;
          dest = 0;
        }
        else
        {
          flags->c = (dest >> (shift - 1)) & 0x01;
          dest >>= shift;
        }
        break;

      case 0x4:
        if (shift > 31)
        {
          dest = (u32)((s32)dest >> 31);
          flags->c = dest & 0x01;
        }
        else
        {
          flags->c = (dest >> (shift - 1)) & 0x01;
          dest = (u32)((s32)dest >> shift);
        }
        break;

      case 0x7:
        flags->c = (dest >> ((shift - 1) & 31)) & 0x01;
        dest = xtensa_ror32(dest, shift);
        break;

      default:
        xtensa_unsupported_opcodes++;
        break;
    }
  }

  xtensa_thumb_logic_flags(flags, dest);
  return dest;
}

static u32 xtensa_thumb_load_value(u32 address, u32 mem_type,
                                   s32 *cycles_remaining)
{
  switch (mem_type)
  {
    case 0:
      return xtensa_load_u8(address, cycles_remaining);
    case 1:
      return xtensa_load_u16(address, cycles_remaining);
    case 2:
      return xtensa_load_u32(address, cycles_remaining);
    case 3:
      return xtensa_load_s8(address, cycles_remaining);
    default:
      return xtensa_load_s16(address, cycles_remaining);
  }
}

static cpu_alert_type xtensa_thumb_store_value(u32 address, u32 value,
                                               u32 mem_type,
                                               s32 *cycles_remaining)
{
  switch (mem_type)
  {
    case 0:
      return xtensa_mem_store_u8(address, value, cycles_remaining);
    case 1:
      return xtensa_mem_store_u16(address, value, cycles_remaining);
    default:
      return xtensa_mem_store_u32(address, value, cycles_remaining);
  }
}

static bool xtensa_exec_thumb_instruction(u16 opcode, xtensa_arm_flags *flags,
                                          s32 *cycles_remaining,
                                          cpu_alert_type *cpu_alert)
{
  u32 current_pc = reg[REG_PC] & ~1u;
  u32 next_pc = current_pc + 2;
  u32 high = (opcode >> 8) & 0xFF;
  u32 rd;
  u32 rs;
  u32 rn;
  u32 imm;
  u32 value;
  u32 address;

  switch (high)
  {
    case 0x00 ... 0x17:
      rd = opcode & 0x07;
      reg[rd] = xtensa_thumb_shift_imm(opcode, flags);
      reg[REG_PC] = next_pc;
      return true;

    case 0x18:
    case 0x19:
      rn = (opcode >> 6) & 0x07;
      rs = (opcode >> 3) & 0x07;
      rd = opcode & 0x07;
      reg[rd] = xtensa_thumb_add_flags(flags, reg[rs], reg[rn], 0);
      reg[REG_PC] = next_pc;
      return true;

    case 0x1A:
    case 0x1B:
      rn = (opcode >> 6) & 0x07;
      rs = (opcode >> 3) & 0x07;
      rd = opcode & 0x07;
      reg[rd] = xtensa_thumb_sub_flags(flags, reg[rs], reg[rn], 1);
      reg[REG_PC] = next_pc;
      return true;

    case 0x1C:
    case 0x1D:
      imm = (opcode >> 6) & 0x07;
      rs = (opcode >> 3) & 0x07;
      rd = opcode & 0x07;
      reg[rd] = xtensa_thumb_add_flags(flags, reg[rs], imm, 0);
      reg[REG_PC] = next_pc;
      return true;

    case 0x1E:
    case 0x1F:
      imm = (opcode >> 6) & 0x07;
      rs = (opcode >> 3) & 0x07;
      rd = opcode & 0x07;
      reg[rd] = xtensa_thumb_sub_flags(flags, reg[rs], imm, 1);
      reg[REG_PC] = next_pc;
      return true;

    case 0x20 ... 0x27:
      rd = (opcode >> 8) & 0x07;
      value = opcode & 0xFF;
      reg[rd] = value;
      xtensa_thumb_logic_flags(flags, value);
      reg[REG_PC] = next_pc;
      return true;

    case 0x28 ... 0x2F:
      rd = (opcode >> 8) & 0x07;
      imm = opcode & 0xFF;
      (void)xtensa_thumb_sub_flags(flags, reg[rd], imm, 1);
      reg[REG_PC] = next_pc;
      return true;

    case 0x30 ... 0x37:
      rd = (opcode >> 8) & 0x07;
      imm = opcode & 0xFF;
      reg[rd] = xtensa_thumb_add_flags(flags, reg[rd], imm, 0);
      reg[REG_PC] = next_pc;
      return true;

    case 0x38 ... 0x3F:
      rd = (opcode >> 8) & 0x07;
      imm = opcode & 0xFF;
      reg[rd] = xtensa_thumb_sub_flags(flags, reg[rd], imm, 1);
      reg[REG_PC] = next_pc;
      return true;

    case 0x40 ... 0x43:
    {
      u32 alu_op = (opcode >> 6) & 0x0F;
      rs = (opcode >> 3) & 0x07;
      rd = opcode & 0x07;

      switch (alu_op)
      {
        case 0x0:
          value = reg[rd] & reg[rs];
          break;
        case 0x1:
          value = reg[rd] ^ reg[rs];
          break;
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x7:
          reg[rd] = xtensa_thumb_shift_reg(opcode, flags);
          reg[REG_PC] = next_pc;
          return true;
        case 0x5:
          reg[rd] = xtensa_thumb_add_flags(flags, reg[rd], reg[rs], flags->c);
          reg[REG_PC] = next_pc;
          return true;
        case 0x6:
          reg[rd] = xtensa_thumb_sub_flags(flags, reg[rd], reg[rs], flags->c);
          reg[REG_PC] = next_pc;
          return true;
        case 0x8:
          xtensa_thumb_logic_flags(flags, reg[rd] & reg[rs]);
          reg[REG_PC] = next_pc;
          return true;
        case 0x9:
          value = 0 - reg[rs];
          xtensa_calculate_flags_sub(flags, value, 0, reg[rs], 1);
          reg[rd] = value;
          reg[REG_PC] = next_pc;
          return true;
        case 0xA:
          (void)xtensa_thumb_sub_flags(flags, reg[rd], reg[rs], 1);
          reg[REG_PC] = next_pc;
          return true;
        case 0xB:
          (void)xtensa_thumb_add_flags(flags, reg[rd], reg[rs], 0);
          reg[REG_PC] = next_pc;
          return true;
        case 0xC:
          value = reg[rd] | reg[rs];
          break;
        case 0xD:
          value = reg[rd] * reg[rs];
          break;
        case 0xE:
          value = reg[rd] & ~reg[rs];
          break;
        default:
          value = ~reg[rs];
          break;
      }

      reg[rd] = value;
      xtensa_thumb_logic_flags(flags, value);
      reg[REG_PC] = next_pc;
      return true;
    }

    case 0x44:
    case 0x46:
    {
      rs = (opcode >> 3) & 0x0F;
      rd = ((opcode >> 4) & 0x08) | (opcode & 0x07);
      value = (high == 0x44) ?
        (xtensa_read_thumb_reg(current_pc, rd) +
         xtensa_read_thumb_reg(current_pc, rs)) :
        xtensa_read_thumb_reg(current_pc, rs);

      if (rd == REG_PC)
        reg[REG_PC] = value & ~1u;
      else
      {
        reg[rd] = value;
        reg[REG_PC] = next_pc;
      }
      return true;
    }

    case 0x45:
      rs = (opcode >> 3) & 0x0F;
      rd = ((opcode >> 4) & 0x08) | (opcode & 0x07);
      value = xtensa_read_thumb_reg(current_pc, rd) -
              xtensa_read_thumb_reg(current_pc, rs);
      xtensa_calculate_flags_sub(flags, value,
                                 xtensa_read_thumb_reg(current_pc, rd),
                                 xtensa_read_thumb_reg(current_pc, rs), 1);
      reg[REG_PC] = next_pc;
      return true;

    case 0x47:
      rs = (opcode >> 3) & 0x0F;
      value = xtensa_read_thumb_reg(current_pc, rs);
      if (value & 0x01)
      {
        reg[REG_PC] = value - 1;
      }
      else
      {
        reg[REG_PC] = value;
        reg[REG_CPSR] &= ~0x20;
      }
      return true;

    case 0x48 ... 0x4F:
      rd = (opcode >> 8) & 0x07;
      imm = opcode & 0xFF;
      address = (current_pc & ~2u) + (imm * 4) + 4;
      reg[rd] = xtensa_load_u32(address, cycles_remaining);
      reg[REG_PC] = next_pc;
      return true;

    case 0x50 ... 0x5F:
    {
      u32 mem_type;
      u32 op = (high - 0x50) >> 1;
      u32 ro = (opcode >> 6) & 0x07;
      u32 rb = (opcode >> 3) & 0x07;
      rd = opcode & 0x07;
      address = reg[rb] + reg[ro];

      if (op < 3)
      {
        mem_type = (op == 1) ? 1 : (op == 2) ? 0 : 2;
        *cpu_alert |= xtensa_thumb_store_value(address, reg[rd], mem_type,
                                               cycles_remaining);
      }
      else
      {
        mem_type = (op == 3) ? 3 : (op == 4) ? 2 :
                   (op == 5) ? 1 : (op == 6) ? 0 : 4;
        reg[rd] = xtensa_thumb_load_value(address, mem_type, cycles_remaining);
      }

      reg[REG_PC] = next_pc;
      return true;
    }

    case 0x60 ... 0x8F:
    {
      u32 rb = (opcode >> 3) & 0x07;
      u32 mem_type;
      u32 load;
      rd = opcode & 0x07;
      imm = (opcode >> 6) & 0x1F;

      if (high < 0x70)
      {
        load = high >= 0x68;
        mem_type = 2;
        address = reg[rb] + (imm * 4);
      }
      else if (high < 0x80)
      {
        load = high >= 0x78;
        mem_type = 0;
        address = reg[rb] + imm;
      }
      else
      {
        load = high >= 0x88;
        mem_type = 1;
        address = reg[rb] + (imm * 2);
      }

      if (load)
        reg[rd] = xtensa_thumb_load_value(address, mem_type, cycles_remaining);
      else
        *cpu_alert |= xtensa_thumb_store_value(address, reg[rd], mem_type,
                                               cycles_remaining);

      reg[REG_PC] = next_pc;
      return true;
    }

    case 0x90 ... 0x9F:
      rd = (opcode >> 8) & 0x07;
      imm = opcode & 0xFF;
      address = reg[REG_SP] + (imm * 4);
      if (high < 0x98)
        *cpu_alert |= xtensa_mem_store_u32(address, reg[rd], cycles_remaining);
      else
        reg[rd] = xtensa_load_u32(address, cycles_remaining);
      reg[REG_PC] = next_pc;
      return true;

    case 0xA0 ... 0xA7:
      rd = (opcode >> 8) & 0x07;
      imm = opcode & 0xFF;
      reg[rd] = (current_pc & ~2u) + 4 + (imm * 4);
      reg[REG_PC] = next_pc;
      return true;

    case 0xA8 ... 0xAF:
      rd = (opcode >> 8) & 0x07;
      imm = opcode & 0xFF;
      reg[rd] = reg[REG_SP] + (imm * 4);
      reg[REG_PC] = next_pc;
      return true;

    case 0xB0:
    case 0xB1:
    case 0xB2:
    case 0xB3:
      imm = opcode & 0x7F;
      if ((opcode >> 7) & 0x01)
        reg[REG_SP] -= imm * 4;
      else
        reg[REG_SP] += imm * 4;
      reg[REG_PC] = next_pc;
      return true;

    case 0xB4:
      *cpu_alert |= xtensa_exec_thumb_block_transfer(current_pc, REG_SP,
        opcode & 0xFF, XTENSA_ACC_STORE, XTENSA_ADDR_PRE_DEC,
        cycles_remaining);
      return true;

    case 0xB5:
      *cpu_alert |= xtensa_exec_thumb_block_transfer(current_pc, REG_SP,
        (opcode & 0xFF) | (1u << REG_LR), XTENSA_ACC_STORE,
        XTENSA_ADDR_PRE_DEC, cycles_remaining);
      return true;

    case 0xBC:
      *cpu_alert |= xtensa_exec_thumb_block_transfer(current_pc, REG_SP,
        opcode & 0xFF, XTENSA_ACC_LOAD, XTENSA_ADDR_POST_INC,
        cycles_remaining);
      return true;

    case 0xBD:
      *cpu_alert |= xtensa_exec_thumb_block_transfer(current_pc, REG_SP,
        (opcode & 0xFF) | (1u << REG_PC), XTENSA_ACC_LOAD,
        XTENSA_ADDR_POST_INC, cycles_remaining);
      return true;

    case 0xC0 ... 0xC7:
      rn = (opcode >> 8) & 0x07;
      *cpu_alert |= xtensa_exec_thumb_block_transfer(current_pc, rn,
        opcode & 0xFF, XTENSA_ACC_STORE, XTENSA_ADDR_POST_INC,
        cycles_remaining);
      return true;

    case 0xC8 ... 0xCF:
      rn = (opcode >> 8) & 0x07;
      *cpu_alert |= xtensa_exec_thumb_block_transfer(current_pc, rn,
        opcode & 0xFF, XTENSA_ACC_LOAD, XTENSA_ADDR_POST_INC,
        cycles_remaining);
      return true;

    case 0xD0 ... 0xDD:
    {
      s32 offset = (s8)(opcode & 0xFF);
      bool take = xtensa_condition_passed(high & 0x0F, flags);
      reg[REG_PC] = take ? (current_pc + (offset * 2) + 4) : next_pc;
      *cycles_remaining -= ws_cyc_nseq[reg[REG_PC] >> 24][0];
      return true;
    }

    case 0xDF:
      if (((opcode & 0xFF) & 0xFE) == 0x06)
      {
        xtensa_exec_hle_div((opcode & 0xFF) == 0x07);
        reg[REG_PC] = next_pc;
        *cycles_remaining -= 64;
        return true;
      }

      xtensa_collapse_flags(flags);
      REG_MODE(MODE_SUPERVISOR)[6] = current_pc + 2;
      REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];
      reg[REG_PC] = 0x00000008;
      reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3F) | 0x13 | 0x80;
      set_cpu_mode(MODE_SUPERVISOR);
      reg[REG_BUS_VALUE] = 0xe3a02004;
      xtensa_extract_flags(flags);
      return true;

    case 0xE0 ... 0xE7:
    {
      s32 offset = ((s32)((opcode & 0x07FF) << 21) >> 20) + 4;
      reg[REG_PC] = current_pc + offset;
      *cycles_remaining -= ws_cyc_nseq[reg[REG_PC] >> 24][0];
      return true;
    }

    case 0xF0 ... 0xF7:
      reg[REG_LR] = current_pc + 4 +
                    ((s32)((opcode & 0x07FF) << 21) >> 9);
      reg[REG_PC] = next_pc;
      return true;

    case 0xF8 ... 0xFF:
    {
      u16 low_opcode = xtensa_fetch_thumb_opcode(current_pc - 2);
      u32 newlr = (current_pc + 2) | 0x01;
      u32 branch_base = reg[REG_LR];
      u32 newpc;

      if (low_opcode >= 0xF000 && low_opcode < 0xF800)
      {
        s32 high_offset = ((s32)((low_opcode & 0x07FF) << 21) >> 9);
        branch_base = (current_pc - 2) + 4 + high_offset;
      }

      newpc = branch_base + ((opcode & 0x07FF) * 2);
      reg[REG_LR] = newlr;
      reg[REG_PC] = newpc;
      *cycles_remaining -= ws_cyc_nseq[newpc >> 24][0];
      return true;
    }

    case 0xB6 ... 0xBB:
    case 0xBE ... 0xBF:
    case 0xDE:
    case 0xE8 ... 0xEF:
      reg[REG_PC] = next_pc;
      return true;

    default:
      break;
  }

  xtensa_unsupported_opcodes++;
  printf("xtensa-thumb unsupported pc=%08x opcode=%04x\n", current_pc, opcode);
  return false;
}

static bool xtensa_run_arm_block(const xtensa_jit_block_meta *meta)
{
  xtensa_arm_flags flags;

  xtensa_interpreter_blocks_executed++;

  if (reg[REG_CPSR] & 0x20)
  {
    xtensa_thumb_blocks++;
    return false;
  }

  xtensa_extract_flags(&flags);

  while (xtensa_cycles_remaining > 0 &&
         reg[CPU_HALT_STATE] == CPU_ACTIVE &&
         ((reg[REG_PC] & ~0x03) >= meta->start_pc) &&
         ((reg[REG_PC] & ~0x03) < meta->end_pc))
  {
    u32 opcode;

    reg[REG_PC] &= ~0x03;
    opcode = xtensa_fetch_arm_opcode(reg[REG_PC]);
    gpsp_debug_trace_cpu(reg[REG_PC], opcode, 0);
    if (gpsp_debug_cpu_should_break(reg[REG_PC], opcode, 0))
    {
      xtensa_collapse_flags(&flags);
      return true;
    }

    if (!xtensa_exec_arm_instruction(meta, opcode, &flags,
                                     &xtensa_cycles_remaining,
                                     &xtensa_cpu_alert))
    {
      xtensa_collapse_flags(&flags);
      return false;
    }

    xtensa_cycles_remaining -= ws_cyc_seq[(reg[REG_PC] >> 24) & 0x0F][1];

    if (reg[REG_PC] == idle_loop_target_pc && xtensa_cycles_remaining > 0)
      xtensa_cycles_remaining = 0;

    if (gpsp_debug_cpu_stop_requested())
    {
      xtensa_collapse_flags(&flags);
      return true;
    }

    if (xtensa_cpu_alert & (CPU_ALERT_HALT | CPU_ALERT_IRQ))
      break;
  }

  xtensa_collapse_flags(&flags);
  return true;
}

static u8 *xtensa_jit_run_block_common(const xtensa_jit_block_meta *meta)
{
  if (meta->thumb)
  {
    xtensa_thumb_blocks++;
    xtensa_unsupported_opcodes++;
  }
  else if (!xtensa_run_arm_block(meta))
  {
    xtensa_generic_fallbacks++;
  }
  return NULL;
}

u8 *xtensa_jit_run_block_arm(const xtensa_jit_block_meta *meta)
{
  return xtensa_jit_run_block_common(meta);
}

u8 *xtensa_jit_run_block_thumb(const xtensa_jit_block_meta *meta)
{
  return xtensa_jit_run_block_common(meta);
}

u32 execute_arm_translate(u32 cycles)
{
  return execute_arm_translate_internal(cycles, &reg[0]);
}

u32 xtensa_jit_register_arm_instruction(u32 opcode, u32 pc, bool ram_region)
{
  u32 *count = ram_region ? &xtensa_compiled_ram_arm_insn_count :
                            &xtensa_compiled_rom_arm_insn_count;
  xtensa_compiled_arm_insn *insns = ram_region ? xtensa_compiled_ram_arm_insns :
                                                 xtensa_compiled_rom_arm_insns;
  u32 index = *count;

  if (index >= XTENSA_MAX_COMPILED_ARM_INSNS)
  {
    xtensa_unsupported_opcodes++;
    return XTENSA_COMPILED_INSN_INVALID;
  }

  insns[index].opcode = opcode;
  insns[index].pc = pc;
  *count = index + 1;
  return ram_region ? (index | XTENSA_COMPILED_INSN_RAM_BIT) : index;
}

u32 xtensa_jit_register_thumb_instruction(u32 opcode, u32 pc, bool ram_region)
{
  u32 *count = ram_region ? &xtensa_compiled_ram_thumb_insn_count :
                            &xtensa_compiled_rom_thumb_insn_count;
  xtensa_compiled_thumb_insn *insns =
    ram_region ? xtensa_compiled_ram_thumb_insns :
                 xtensa_compiled_rom_thumb_insns;
  u32 index = *count;

  if (index >= XTENSA_MAX_COMPILED_THUMB_INSNS)
  {
    xtensa_unsupported_opcodes++;
    return XTENSA_COMPILED_INSN_INVALID;
  }

  insns[index].opcode = (u16)opcode;
  insns[index].reserved = 0;
  insns[index].pc = pc;
  *count = index + 1;
  return ram_region ? (index | XTENSA_COMPILED_INSN_RAM_BIT) : index;
}

void xtensa_jit_flush_rom_cache(void)
{
  xtensa_compiled_rom_arm_insn_count = xtensa_compiled_rom_arm_insn_watermark;
  xtensa_compiled_rom_thumb_insn_count =
    xtensa_compiled_rom_thumb_insn_watermark;
}

void xtensa_jit_flush_ram_cache(void)
{
  xtensa_compiled_ram_arm_insn_count = 0;
  xtensa_compiled_ram_thumb_insn_count = 0;
}

static u32 xtensa_jit_exec_compiled_arm_global(u32 insn_index)
{
  xtensa_arm_flags flags;
  const xtensa_compiled_arm_insn *insn;
  u32 index = insn_index & XTENSA_COMPILED_INSN_INDEX_MASK;
  u32 count = (insn_index & XTENSA_COMPILED_INSN_RAM_BIT) ?
    xtensa_compiled_ram_arm_insn_count : xtensa_compiled_rom_arm_insn_count;
  const xtensa_compiled_arm_insn *insns =
    (insn_index & XTENSA_COMPILED_INSN_RAM_BIT) ?
      xtensa_compiled_ram_arm_insns : xtensa_compiled_rom_arm_insns;
  u32 expected_pc;

  if (index >= count)
  {
    xtensa_unsupported_opcodes++;
    xtensa_generic_fallbacks++;
    return 1;
  }

  if (reg[REG_CPSR] & 0x20)
  {
    xtensa_thumb_blocks++;
    xtensa_unsupported_opcodes++;
    xtensa_generic_fallbacks++;
    return 1;
  }

  insn = &insns[index];
  if ((reg[REG_PC] & ~0x03) != (insn->pc & ~0x03))
  {
    if (xtensa_pc_guard_mismatches < 16)
      printf("xtensa guard arm live_pc=%08x insn_pc=%08x cpsr=%08x cycles=%d\n",
             reg[REG_PC], insn->pc, reg[REG_CPSR], xtensa_cycles_remaining);
    xtensa_pc_guard_mismatches++;
    xtensa_cycles_remaining--;
    return 1;
  }

  expected_pc = insn->pc + 4;
  reg[REG_PC] = insn->pc & ~0x03;
  xtensa_helper_arm_insns_executed++;
  gpsp_debug_trace_cpu(insn->pc, insn->opcode, 0);
  if (gpsp_debug_cpu_should_break(insn->pc, insn->opcode, 0))
    return 1;

  xtensa_extract_flags(&flags);
  if (!xtensa_exec_arm_instruction(NULL, insn->opcode, &flags,
                                   &xtensa_cycles_remaining,
                                   &xtensa_cpu_alert))
  {
    xtensa_collapse_flags(&flags);
    xtensa_generic_fallbacks++;
    return 1;
  }

  xtensa_cycles_remaining -= ws_cyc_seq[(reg[REG_PC] >> 24) & 0x0F][1];

  if (reg[REG_PC] == idle_loop_target_pc && xtensa_cycles_remaining > 0)
    xtensa_cycles_remaining = 0;

  if (gpsp_debug_cpu_stop_requested())
  {
    xtensa_collapse_flags(&flags);
    return 1;
  }

  xtensa_collapse_flags(&flags);

  if (xtensa_cycles_remaining <= 0 ||
      reg[CPU_HALT_STATE] != CPU_ACTIVE ||
      (xtensa_cpu_alert & (CPU_ALERT_HALT | CPU_ALERT_IRQ)) ||
      ((reg[REG_PC] & ~0x03) != expected_pc))
  {
    return 1;
  }

  return 0;
}

u32 xtensa_jit_exec_compiled_arm(xtensa_jit_state *state, u32 insn_index)
{
  u32 result;

  xtensa_state_sync_to_globals(state);
  result = xtensa_jit_exec_compiled_arm_global(insn_index);
  xtensa_state_sync_from_globals(state);
  return result;
}

static u32 xtensa_jit_exec_compiled_thumb_global(u32 insn_index)
{
  xtensa_arm_flags flags;
  const xtensa_compiled_thumb_insn *insn;
  u32 index = insn_index & XTENSA_COMPILED_INSN_INDEX_MASK;
  u32 count = (insn_index & XTENSA_COMPILED_INSN_RAM_BIT) ?
    xtensa_compiled_ram_thumb_insn_count :
    xtensa_compiled_rom_thumb_insn_count;
  const xtensa_compiled_thumb_insn *insns =
    (insn_index & XTENSA_COMPILED_INSN_RAM_BIT) ?
      xtensa_compiled_ram_thumb_insns : xtensa_compiled_rom_thumb_insns;
  u32 expected_pc;

  if (index >= count)
  {
    xtensa_unsupported_opcodes++;
    xtensa_generic_fallbacks++;
    return 1;
  }

  if ((reg[REG_CPSR] & 0x20) == 0)
  {
    xtensa_unsupported_opcodes++;
    xtensa_generic_fallbacks++;
    return 1;
  }

  insn = &insns[index];
  if ((reg[REG_PC] & ~1u) != (insn->pc & ~1u) &&
      !xtensa_thumb_bl_pair_entry(reg[REG_PC] & ~1u, insn))
  {
    if (xtensa_pc_guard_mismatches < 16)
      printf("xtensa guard thumb live_pc=%08x insn_pc=%08x cpsr=%08x cycles=%d\n",
             reg[REG_PC], insn->pc, reg[REG_CPSR], xtensa_cycles_remaining);
    xtensa_pc_guard_mismatches++;
    xtensa_cycles_remaining--;
    return 1;
  }

  expected_pc = insn->pc + 2;
  reg[REG_PC] = insn->pc & ~1u;
  xtensa_helper_thumb_insns_executed++;
  gpsp_debug_trace_cpu(insn->pc, insn->opcode, 1);
  if (gpsp_debug_cpu_should_break(insn->pc, insn->opcode, 1))
    return 1;

  xtensa_extract_flags(&flags);
  if (!xtensa_exec_thumb_instruction(insn->opcode, &flags,
                                     &xtensa_cycles_remaining,
                                     &xtensa_cpu_alert))
  {
    xtensa_collapse_flags(&flags);
    xtensa_generic_fallbacks++;
    return 1;
  }

  xtensa_cycles_remaining -= ws_cyc_seq[(reg[REG_PC] >> 24) & 0x0F][0];

  if (reg[REG_PC] == idle_loop_target_pc && xtensa_cycles_remaining > 0)
    xtensa_cycles_remaining = 0;

  if (gpsp_debug_cpu_stop_requested())
  {
    xtensa_collapse_flags(&flags);
    return 1;
  }

  xtensa_collapse_flags(&flags);

  if (xtensa_cycles_remaining <= 0 ||
      reg[CPU_HALT_STATE] != CPU_ACTIVE ||
      (xtensa_cpu_alert & (CPU_ALERT_HALT | CPU_ALERT_IRQ)) ||
      ((reg[REG_PC] & ~1u) != expected_pc) ||
      ((reg[REG_CPSR] & 0x20) == 0))
  {
    return 1;
  }

  return 0;
}

u32 xtensa_jit_exec_compiled_thumb(xtensa_jit_state *state, u32 insn_index)
{
  u32 result;

  xtensa_state_sync_to_globals(state);
  result = xtensa_jit_exec_compiled_thumb_global(insn_index);
  xtensa_state_sync_from_globals(state);
  return result;
}

bool xtensa_emit_native_arm_data_proc(u8 **translation_ptr, u8 *literal_base,
                                      u8 **literal_cursor, u32 opcode,
                                      u32 pc, u32 cycles)
{
  if (!xtensa_emit_native_arm_data_proc_body(translation_ptr, literal_base,
                                             literal_cursor, opcode, pc,
                                             cycles))
    return false;

  xtensa_native_arm_insn_count++;
  return true;
}

void xtensa_emit_block_prologue(u8 **translation_ptr, u8 **literal_base,
                                u8 **literal_cursor)
{
  *translation_ptr = xtensa_align_ptr(*translation_ptr);
  *literal_base = *translation_ptr;
  xtensa_store_u32(*literal_base + XTENSA_LITERAL_HELPER,
                   (u32)(uintptr_t)xtensa_jit_exec_compiled_arm);
  xtensa_store_u32(*literal_base + XTENSA_LITERAL_STATE,
                   (u32)(uintptr_t)&xtensa_state);
  xtensa_store_u32(*literal_base + XTENSA_LITERAL_RESERVED0, 0);
  xtensa_store_u32(*literal_base + XTENSA_LITERAL_RESERVED1, 0);
  *literal_cursor = *literal_base + XTENSA_BLOCK_FIXED_LITERAL_BYTES;
  *translation_ptr += block_prologue_size;
  xtensa_emit_native_block_prologue(translation_ptr, *literal_base);
}

void xtensa_emit_block_finalize(u8 *literal_base, u8 **translation_ptr,
                                u32 block_start_pc, u32 block_end_pc,
                                bool thumb_mode)
{
  u8 *ptr = *translation_ptr;
  (void)block_start_pc;
  (void)block_end_pc;

  xtensa_store_u32(literal_base + XTENSA_LITERAL_HELPER,
                   (u32)(uintptr_t)(thumb_mode ?
                     xtensa_jit_exec_compiled_thumb :
                     xtensa_jit_exec_compiled_arm));

  xtensa_native_emit_spill_pc(&ptr);
  xtensa_emit_retw_n(&ptr);
  ptr = xtensa_align_ptr(ptr);
  *translation_ptr = ptr;
  xtensa_blocks_emitted++;
}

void init_emitter(bool must_swap)
{
  (void)must_swap;
  xtensa_blocks_emitted = 0;
  xtensa_blocks_executed = 0;
  xtensa_generic_fallbacks = 0;
  xtensa_unsupported_opcodes = 0;
  xtensa_thumb_blocks = 0;
  xtensa_interpreter_blocks_executed = 0;
  xtensa_compiled_rom_arm_insn_count = 0;
  xtensa_compiled_rom_thumb_insn_count = 0;
  xtensa_compiled_rom_arm_insn_watermark = 0;
  xtensa_compiled_rom_thumb_insn_watermark = 0;
  xtensa_compiled_ram_arm_insn_count = 0;
  xtensa_compiled_ram_thumb_insn_count = 0;
  xtensa_native_arm_insn_count = 0;
  xtensa_helper_arm_insns_executed = 0;
  xtensa_helper_thumb_insns_executed = 0;
  xtensa_pc_guard_mismatches = 0;
  xtensa_cycles_remaining = 0;
  xtensa_cpu_alert = CPU_ALERT_NONE;
  memset(&xtensa_state, 0, sizeof(xtensa_state));
  rom_cache_watermark = XTENSA_INITIAL_ROM_WATERMARK;
  init_bios_hooks();
  xtensa_compiled_rom_arm_insn_watermark = xtensa_compiled_rom_arm_insn_count;
  xtensa_compiled_rom_thumb_insn_watermark =
    xtensa_compiled_rom_thumb_insn_count;
}

u32 execute_arm_translate_internal(u32 cycles, void *regptr)
{
  xtensa_jit_block_fn entry;
  u8 *entry_data;
  u32 update_ret;

  (void)regptr;
  xtensa_cycles_remaining = (s32)cycles;
  xtensa_cpu_alert = CPU_ALERT_NONE;
  xtensa_state_sync_from_globals(&xtensa_state);
  clear_gamepak_stickybits();

  while (1)
  {
    if (reg[CPU_HALT_STATE] != CPU_ACTIVE)
    {
      update_ret = update_gba(xtensa_cycles_remaining);
      if (completed_frame(update_ret))
        return 0;
      xtensa_cycles_remaining = cycles_to_run(update_ret);
      xtensa_state_sync_from_globals(&xtensa_state);
    }

    xtensa_cpu_alert = CPU_ALERT_NONE;
    xtensa_state.jit_alert = CPU_ALERT_NONE;
    xtensa_state.jit_cycles = xtensa_cycles_remaining;

    while (xtensa_cycles_remaining > 0)
    {
      if (gpsp_debug_cpu_stop_requested())
        return 0;

      if (reg[REG_PC] == cheat_master_hook)
        process_cheats();

      if (reg[REG_CPSR] & 0x20)
      {
        entry_data = block_lookup_address_thumb(reg[REG_PC] & ~1u);
        xtensa_thumb_blocks++;
      }
      else
      {
        entry_data = block_lookup_address_arm(reg[REG_PC] & ~0x03);
      }
      if (!entry_data)
      {
        xtensa_generic_fallbacks++;
        return 0;
      }

      if (entry_data == XTENSA_INVALID_BLOCK_ENTRY)
      {
        xtensa_unsupported_opcodes++;
        xtensa_generic_fallbacks++;
        printf("xtensa invalid block entry pc=%08x cpsr=%08x cycles=%d\n",
               reg[REG_PC], reg[REG_CPSR], xtensa_cycles_remaining);
        gpsp_debug_dump_recent_cpu_trace();
        return 0;
      }

      entry = (xtensa_jit_block_fn)esp32s3_jit_data_to_exec(entry_data);
      xtensa_blocks_executed++;
      (void)entry();
      xtensa_state_sync_to_globals(&xtensa_state);

      if (gpsp_debug_cpu_stop_requested())
        return 0;

      if (xtensa_generic_fallbacks != 0)
        return 0;

      if (xtensa_cpu_alert & (CPU_ALERT_HALT | CPU_ALERT_IRQ))
        break;
    }

    update_ret = update_gba(xtensa_cycles_remaining);
    if (completed_frame(update_ret))
      return 0;
    xtensa_cycles_remaining = cycles_to_run(update_ret);
    xtensa_state_sync_from_globals(&xtensa_state);
  }
}

u32 xtensa_jit_get_blocks_emitted(void)
{
  return xtensa_blocks_emitted;
}

u32 xtensa_jit_get_blocks_executed(void)
{
  return xtensa_blocks_executed;
}

u32 xtensa_jit_get_compiled_arm_instructions(void)
{
  return xtensa_compiled_rom_arm_insn_count +
         xtensa_compiled_ram_arm_insn_count +
         xtensa_native_arm_insn_count;
}

u32 xtensa_jit_get_compiled_thumb_instructions(void)
{
  return xtensa_compiled_rom_thumb_insn_count +
         xtensa_compiled_ram_thumb_insn_count;
}

u32 xtensa_jit_get_helper_arm_instructions_executed(void)
{
  return xtensa_helper_arm_insns_executed;
}

u32 xtensa_jit_get_helper_thumb_instructions_executed(void)
{
  return xtensa_helper_thumb_insns_executed;
}

u32 xtensa_jit_get_interpreter_blocks_executed(void)
{
  return xtensa_interpreter_blocks_executed;
}

u32 xtensa_jit_get_generic_fallbacks(void)
{
  return xtensa_generic_fallbacks;
}

u32 xtensa_jit_get_unsupported_opcodes(void)
{
  return xtensa_unsupported_opcodes;
}

u32 xtensa_jit_get_thumb_blocks(void)
{
  return xtensa_thumb_blocks;
}
