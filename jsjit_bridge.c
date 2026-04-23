/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "common.h"
#include "cpu.h"
#include "gba_memory.h"
#include "main.h"
#include "jsjit_bridge.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define JSJIT_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define JSJIT_KEEPALIVE
#endif

#define JSJIT_PAGE_SHIFT 15
#define JSJIT_PAGE_SIZE (1u << JSJIT_PAGE_SHIFT)
#define JSJIT_EWRAM_DATA_BYTES (1024u * 256u)
#define JSJIT_IWRAM_DATA_OFFSET (1024u * 32u)
#define JSJIT_IWRAM_DATA_BYTES (1024u * 32u)

static cpu_alert_type jsjit_pending_alert = CPU_ALERT_NONE;

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_reg_ptr(void)
{
  return (uintptr_t)reg;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_spsr_ptr(void)
{
  return (uintptr_t)spsr;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_reg_mode_ptr(void)
{
  return (uintptr_t)reg_mode;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_memory_map_read_ptr(void)
{
  return (uintptr_t)memory_map_read;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_bios_rom_ptr(void)
{
  return (uintptr_t)bios_rom;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_open_bios_rom_ptr(void)
{
  return (uintptr_t)open_gba_bios_rom;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_ewram_ptr(void)
{
  return (uintptr_t)ewram;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_ewram_shadow_ptr(void)
{
  return (uintptr_t)(ewram + JSJIT_EWRAM_DATA_BYTES);
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_iwram_ptr(void)
{
  return (uintptr_t)iwram;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_iwram_data_ptr(void)
{
  return (uintptr_t)(iwram + JSJIT_IWRAM_DATA_OFFSET);
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_iwram_shadow_ptr(void)
{
  return (uintptr_t)iwram;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_vram_ptr(void)
{
  return (uintptr_t)vram;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_palette_ram_ptr(void)
{
  return (uintptr_t)palette_ram;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_oam_ram_ptr(void)
{
  return (uintptr_t)oam_ram;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_io_registers_ptr(void)
{
  return (uintptr_t)io_registers;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_execute_cycles_ptr(void)
{
  return (uintptr_t)&execute_cycles;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_cpu_ticks_ptr(void)
{
  return (uintptr_t)&cpu_ticks;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_frame_counter_ptr(void)
{
  return (uintptr_t)&frame_counter;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_ws_cyc_seq_ptr(void)
{
  return (uintptr_t)ws_cyc_seq;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_ws_cyc_nseq_ptr(void)
{
  return (uintptr_t)ws_cyc_nseq;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_idle_loop_target_pc_ptr(void)
{
  return (uintptr_t)&idle_loop_target_pc;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_translation_gate_targets_ptr(void)
{
  return (uintptr_t)&translation_gate_targets;
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_translation_gate_target_pc_ptr(void)
{
  return (uintptr_t)translation_gate_target_pc;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_pointer_size(void)
{
  return (u32)sizeof(void *);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_reg_word_count(void)
{
  return (u32)(sizeof(reg) / sizeof(reg[0]));
}

JSJIT_KEEPALIVE u32 jsjit_bridge_reg_arch_count(void)
{
  return REG_ARCH_COUNT;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_spsr_word_count(void)
{
  return (u32)(sizeof(spsr) / sizeof(spsr[0]));
}

JSJIT_KEEPALIVE u32 jsjit_bridge_reg_mode_word_count(void)
{
  return (u32)(sizeof(reg_mode) / sizeof(reg_mode[0][0]));
}

JSJIT_KEEPALIVE u32 jsjit_bridge_memory_map_read_count(void)
{
  return (u32)(sizeof(memory_map_read) / sizeof(memory_map_read[0]));
}

JSJIT_KEEPALIVE u32 jsjit_bridge_bios_rom_bytes(void)
{
  return (u32)sizeof(bios_rom);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_open_bios_rom_bytes(void)
{
  return (u32)sizeof(open_gba_bios_rom);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_ewram_bytes(void)
{
  return (u32)sizeof(ewram);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_ewram_data_bytes(void)
{
  return JSJIT_EWRAM_DATA_BYTES;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_ewram_shadow_offset(void)
{
  return JSJIT_EWRAM_DATA_BYTES;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_iwram_bytes(void)
{
  return (u32)sizeof(iwram);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_iwram_data_bytes(void)
{
  return JSJIT_IWRAM_DATA_BYTES;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_iwram_data_offset(void)
{
  return JSJIT_IWRAM_DATA_OFFSET;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_vram_bytes(void)
{
  return (u32)sizeof(vram);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_palette_ram_bytes(void)
{
  return (u32)sizeof(palette_ram);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_oam_ram_bytes(void)
{
  return (u32)sizeof(oam_ram);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_io_registers_bytes(void)
{
  return (u32)sizeof(io_registers);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_ws_table_bytes(void)
{
  return (u32)sizeof(ws_cyc_seq);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_page_shift(void)
{
  return JSJIT_PAGE_SHIFT;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_page_size(void)
{
  return JSJIT_PAGE_SIZE;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_translation_gate_capacity(void)
{
  return MAX_TRANSLATION_GATES;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_read_memory8(u32 address)
{
  return read_memory8(address);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_read_memory16(u32 address)
{
  return read_memory16(address);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_read_memory32(u32 address)
{
  return read_memory32(address);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_write_memory8(u32 address, u32 value)
{
  const cpu_alert_type alert = write_memory8(address, (u8)value);
  jsjit_pending_alert |= alert;
  return (u32)alert;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_write_memory16(u32 address, u32 value)
{
  const cpu_alert_type alert = write_memory16(address, (u16)value);
  jsjit_pending_alert |= alert;
  return (u32)alert;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_write_memory32(u32 address, u32 value)
{
  const cpu_alert_type alert = write_memory32(address, value);
  jsjit_pending_alert |= alert;
  return (u32)alert;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_take_pending_alert(void)
{
  const cpu_alert_type alert = jsjit_pending_alert;
  jsjit_pending_alert = CPU_ALERT_NONE;
  return (u32)alert;
}

JSJIT_KEEPALIVE u32 jsjit_bridge_update_gba(int remaining_cycles)
{
  return update_gba(remaining_cycles);
}

JSJIT_KEEPALIVE u32 jsjit_bridge_check_and_raise_interrupts(void)
{
  return check_and_raise_interrupts();
}

JSJIT_KEEPALIVE u32 jsjit_bridge_flag_interrupt(u32 irq_mask)
{
  return (u32)flag_interrupt((irq_type)irq_mask);
}

JSJIT_KEEPALIVE void jsjit_bridge_set_cpu_mode(u32 new_mode)
{
  set_cpu_mode((cpu_mode_type)new_mode);
}

JSJIT_KEEPALIVE uintptr_t jsjit_bridge_load_gamepak_page(u32 physical_index)
{
  return (uintptr_t)load_gamepak_page(physical_index);
}

JSJIT_KEEPALIVE void jsjit_bridge_execute_arm(u32 cycles)
{
  clear_gamepak_stickybits();
  execute_arm(cycles);
}
