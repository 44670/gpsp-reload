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

#ifndef JSJIT_BRIDGE_H
#define JSJIT_BRIDGE_H

#include <stdint.h>

#include "common.h"

uintptr_t jsjit_bridge_reg_ptr(void);
uintptr_t jsjit_bridge_spsr_ptr(void);
uintptr_t jsjit_bridge_reg_mode_ptr(void);
uintptr_t jsjit_bridge_memory_map_read_ptr(void);
uintptr_t jsjit_bridge_bios_rom_ptr(void);
uintptr_t jsjit_bridge_open_bios_rom_ptr(void);
uintptr_t jsjit_bridge_ewram_ptr(void);
uintptr_t jsjit_bridge_ewram_shadow_ptr(void);
uintptr_t jsjit_bridge_iwram_ptr(void);
uintptr_t jsjit_bridge_iwram_data_ptr(void);
uintptr_t jsjit_bridge_iwram_shadow_ptr(void);
uintptr_t jsjit_bridge_vram_ptr(void);
uintptr_t jsjit_bridge_palette_ram_ptr(void);
uintptr_t jsjit_bridge_oam_ram_ptr(void);
uintptr_t jsjit_bridge_io_registers_ptr(void);
uintptr_t jsjit_bridge_execute_cycles_ptr(void);
uintptr_t jsjit_bridge_cpu_ticks_ptr(void);
uintptr_t jsjit_bridge_frame_counter_ptr(void);
uintptr_t jsjit_bridge_ws_cyc_seq_ptr(void);
uintptr_t jsjit_bridge_ws_cyc_nseq_ptr(void);
uintptr_t jsjit_bridge_idle_loop_target_pc_ptr(void);
uintptr_t jsjit_bridge_translation_gate_targets_ptr(void);
uintptr_t jsjit_bridge_translation_gate_target_pc_ptr(void);

u32 jsjit_bridge_pointer_size(void);
u32 jsjit_bridge_reg_word_count(void);
u32 jsjit_bridge_reg_arch_count(void);
u32 jsjit_bridge_spsr_word_count(void);
u32 jsjit_bridge_reg_mode_word_count(void);
u32 jsjit_bridge_memory_map_read_count(void);
u32 jsjit_bridge_bios_rom_bytes(void);
u32 jsjit_bridge_open_bios_rom_bytes(void);
u32 jsjit_bridge_ewram_bytes(void);
u32 jsjit_bridge_ewram_data_bytes(void);
u32 jsjit_bridge_ewram_shadow_offset(void);
u32 jsjit_bridge_iwram_bytes(void);
u32 jsjit_bridge_iwram_data_bytes(void);
u32 jsjit_bridge_iwram_data_offset(void);
u32 jsjit_bridge_vram_bytes(void);
u32 jsjit_bridge_palette_ram_bytes(void);
u32 jsjit_bridge_oam_ram_bytes(void);
u32 jsjit_bridge_io_registers_bytes(void);
u32 jsjit_bridge_ws_table_bytes(void);
u32 jsjit_bridge_page_shift(void);
u32 jsjit_bridge_page_size(void);
u32 jsjit_bridge_translation_gate_capacity(void);

u32 jsjit_bridge_read_memory8(u32 address);
u32 jsjit_bridge_read_memory16(u32 address);
u32 jsjit_bridge_read_memory32(u32 address);
u32 jsjit_bridge_write_memory8(u32 address, u32 value);
u32 jsjit_bridge_write_memory16(u32 address, u32 value);
u32 jsjit_bridge_write_memory32(u32 address, u32 value);
u32 jsjit_bridge_update_gba(int remaining_cycles);
u32 jsjit_bridge_check_and_raise_interrupts(void);
u32 jsjit_bridge_flag_interrupt(u32 irq_mask);
void jsjit_bridge_set_cpu_mode(u32 new_mode);
void jsjit_bridge_execute_arm(u32 cycles);
uintptr_t jsjit_bridge_load_gamepak_page(u32 physical_index);

#endif
