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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef u8 *(*xtensa_jit_block_fn)(void);

extern u32 rom_cache_watermark;

enum
{
  XTENSA_INITIAL_ROM_WATERMARK = 16
};

static u32 xtensa_blocks_emitted;
static u32 xtensa_blocks_executed;
static u32 xtensa_generic_fallbacks;
static u32 xtensa_unsupported_opcodes;
static u32 xtensa_thumb_blocks;
static s32 xtensa_cycles_remaining;
static cpu_alert_type xtensa_cpu_alert;

typedef struct xtensa_arm_flags
{
  u32 n;
  u32 z;
  u32 c;
  u32 v;
} xtensa_arm_flags;

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

static u32 xtensa_load_u8(u32 address, s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 8);
  return read_memory8(address);
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

static cpu_alert_type xtensa_mem_store_u32(u32 address, u32 value,
                                           s32 *cycles_remaining)
{
  xtensa_account_nseq_cycles(cycles_remaining, address, 32);
  return write_memory32(address, value);
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

static void xtensa_restore_spsr(xtensa_arm_flags *flags)
{
  if (reg[CPU_MODE] != MODE_USER && reg[CPU_MODE] != MODE_SYSTEM)
  {
    reg[REG_CPSR] = REG_SPSR(reg[CPU_MODE]);
    xtensa_extract_flags(flags);
    set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);
  }
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
    reg[REG_BUS_VALUE] = 0xe3a02004;
    REG_MODE(MODE_SUPERVISOR)[6] = current_pc + 4;
    REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];
    reg[REG_PC] = 0x00000008;
    reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3F) | 0x13 | 0x80;
    set_cpu_mode(MODE_SUPERVISOR);
    xtensa_extract_flags(flags);
    return true;
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
        reg[REG_PC] = dest & ~0x03;
        if (setflags)
          xtensa_restore_spsr(flags);
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

static bool xtensa_run_arm_block(const xtensa_jit_block_meta *meta)
{
  xtensa_arm_flags flags;

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

    if (xtensa_cpu_alert & (CPU_ALERT_HALT | CPU_ALERT_IRQ))
      break;
  }

  xtensa_collapse_flags(&flags);
  return true;
}

static u8 *xtensa_jit_run_block_common(const xtensa_jit_block_meta *meta)
{
  xtensa_blocks_executed++;
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

void xtensa_emit_block_prologue(u8 **translation_ptr, u8 **literal_base)
{
  *translation_ptr = xtensa_align_ptr(*translation_ptr);
  *literal_base = *translation_ptr;
  xtensa_store_u32(*literal_base + 0, 0);
  xtensa_store_u32(*literal_base + 4, 0);
  *translation_ptr += block_prologue_size;
}

void xtensa_emit_block_finalize(u8 *literal_base, u8 **translation_ptr,
                                u32 block_start_pc, u32 block_end_pc,
                                bool thumb_mode)
{
  u8 *ptr = *translation_ptr;
  xtensa_jit_block_meta *meta;
  uintptr_t helper = (uintptr_t)(thumb_mode ? xtensa_jit_run_block_thumb
                                            : xtensa_jit_run_block_arm);

  xtensa_emit_block_stub(&ptr);

  ptr = xtensa_align_ptr(ptr);
  meta = (xtensa_jit_block_meta *)ptr;
  meta->start_pc = block_start_pc;
  meta->end_pc = block_end_pc;
  meta->thumb = thumb_mode ? 1 : 0;
  ptr += sizeof(*meta);

  xtensa_store_u32(literal_base + 0, (u32)helper);
  xtensa_store_u32(literal_base + 4, (u32)(uintptr_t)meta);

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
  xtensa_cycles_remaining = 0;
  xtensa_cpu_alert = CPU_ALERT_NONE;
  rom_cache_watermark = XTENSA_INITIAL_ROM_WATERMARK;
  init_bios_hooks();
}

u32 execute_arm_translate_internal(u32 cycles, void *regptr)
{
  xtensa_jit_block_fn entry;
  u32 update_ret;

  (void)regptr;
  xtensa_cycles_remaining = (s32)cycles;
  clear_gamepak_stickybits();

  while (1)
  {
    if (reg[CPU_HALT_STATE] != CPU_ACTIVE)
    {
      update_ret = update_gba(xtensa_cycles_remaining);
      if (completed_frame(update_ret))
        return 0;
      xtensa_cycles_remaining = cycles_to_run(update_ret);
    }

    xtensa_cpu_alert = CPU_ALERT_NONE;

    while (xtensa_cycles_remaining > 0)
    {
      if (reg[REG_PC] == cheat_master_hook)
        process_cheats();

      if (reg[REG_CPSR] & 0x20)
      {
        xtensa_thumb_blocks++;
        xtensa_unsupported_opcodes++;
        xtensa_generic_fallbacks++;
        return 0;
      }

      entry = (xtensa_jit_block_fn)block_lookup_address_arm(reg[REG_PC] & ~0x03);
      if (!entry)
      {
        xtensa_generic_fallbacks++;
        return 0;
      }

      (void)entry();

      if (xtensa_generic_fallbacks != 0)
        return 0;

      if (xtensa_cpu_alert & (CPU_ALERT_HALT | CPU_ALERT_IRQ))
        break;
    }

    update_ret = update_gba(xtensa_cycles_remaining);
    if (completed_frame(update_ret))
      return 0;
    xtensa_cycles_remaining = cycles_to_run(update_ret);
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
