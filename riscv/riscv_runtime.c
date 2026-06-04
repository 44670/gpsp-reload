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
#include "cpu.h"
#include "gba_memory.h"
#include "riscv/riscv_emit.h"

#include <stddef.h>
#include <stdint.h>

typedef u8 *(*riscv_jit_block_fn)(void);

extern u32 rom_cache_watermark;

enum
{
  RISCV_INITIAL_ROM_WATERMARK = 16
};

#define RISCV_INVALID_BLOCK_ENTRY ((u8 *)(uintptr_t)~(uintptr_t)0)

static s32 riscv_cycles_remaining;
static u32 riscv_blocks_emitted;
static u32 riscv_blocks_executed;
static u32 riscv_interpreter_fallbacks;

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

static u8 *riscv_jit_run_block(const riscv_jit_block_meta *meta)
{
  (void)meta;

  riscv_blocks_executed++;
  riscv_interpreter_fallbacks++;

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
  (*meta)->reserved = 0;

  *translation_ptr = ptr + block_prologue_size;
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
  meta->reserved = 0;

  riscv_emit_li(&ptr, riscv_reg_a0, (u32)(uintptr_t)meta);
  riscv_emit_li(&ptr, riscv_reg_t0, (u32)(uintptr_t)riscv_jit_run_block);
  {
    u8 *translation_ptr = ptr;
    riscv_emit_jalr(riscv_reg_ra, riscv_reg_t0, 0);
    riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);
    ptr = translation_ptr;
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
