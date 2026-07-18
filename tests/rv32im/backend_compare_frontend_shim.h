/* Freestanding common.h replacement for the RV32IM/MIPS qemu-user A/B.
 * Both builds compile the production cpu_threaded.c frontend. */

#ifndef BACKEND_COMPARE_FRONTEND_SHIM_H
#define BACKEND_COMPARE_FRONTEND_SHIM_H

#define COMMON_H

#include "riscv/riscv_runtime_test_shim.h"
#include "gpsp_config.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define GPSP_EXT_RAM_BSS

typedef signed int intptr_t;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ror(dest, value, shift)                                               \
  dest = ((value) >> (shift)) | ((value) << (32 - (shift)))

#define address8(base, offset)                                                \
  *((u8 *)((u8 *)(base) + (offset)))
#define address16(base, offset)                                               \
  *((u16 *)((u8 *)(base) + (offset)))
#define address32(base, offset)                                               \
  *((u32 *)((u8 *)(base) + (offset)))

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define eswap16(value) __builtin_bswap16(value)
#define eswap32(value) __builtin_bswap32(value)
#else
#define eswap16(value) (value)
#define eswap32(value) (value)
#endif

#define readaddress16(base, offset) eswap16(address16((base), (offset)))
#define readaddress32(base, offset) eswap32(address32((base), (offset)))
#define read_ioreg(regnum) eswap16(io_registers[(regnum)])
#define write_ioreg(regnum, value)                                            \
  io_registers[(regnum)] = eswap16(value)

#define MAX_TRANSLATION_GATES 8

/* riscv_runtime_test_shim.h only names the architectural state.  The MIPS
 * emitter also addresses gpSP's dynarec spill slots and interrupt registers. */
#define REG_N_FLAG 20
#define REG_Z_FLAG 21
#define REG_C_FLAG 22
#define REG_V_FLAG 23
#define REG_SLEEP_CYCLES 24
#define OAM_UPDATED 25
#define REG_SAVE 26
#define REG_SAVE2 27
#define REG_SAVE3 28
#define REG_SAVE4 29
#define REG_SAVE5 30
#define REG_SAVE6 31
#define REG_IE 0x100
#define REG_IF 0x101
#define REG_IME 0x104

extern u8 *memory_map_read[8 * 1024];
extern u8 ewram[1024 * 256 * 2];
extern u8 iwram[1024 * 32 * 2];
extern u8 vram[1024 * 96];
extern u16 palette_ram[512];
extern u16 palette_ram_converted[512];
extern u16 oam_ram[512];
extern u16 io_registers[512];
extern u8 bios_rom[1024 * 16];
extern u32 translation_gate_targets;
extern u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
extern u32 cheat_master_hook;
extern u32 flush_ram_count;
extern u8 *rom_translation_ptr;
extern u8 *ram_translation_ptr;
extern u8 *last_rom_translation_ptr;
extern u8 *last_ram_translation_ptr;
extern const u32 def_seq_cycles[16][2];
extern const u8 bit_count[256];
extern const u32 cpu_modes[16];
extern const u32 cpsr_masks[4][2];
extern const u32 spsr_masks[4];

#if defined(MIPS_ARCH)
extern u8 rom_translation_cache[ROM_TRANSLATION_CACHE_SIZE];
extern u8 ram_translation_cache[RAM_TRANSLATION_CACHE_SIZE];
extern u32 tmemld[11][16];
extern u32 tmemst[4][16];
#else
extern u8 *rom_translation_cache;
extern u8 *ram_translation_cache;
#endif

u8 *load_gamepak_page(u32 physical_index);
void init_dynarec_caches(void);
void flush_translation_cache_rom(void);
bool translate_block_arm(u32 pc, bool ram_region);
bool translate_block_thumb(u32 pc, bool ram_region);
u32 execute_arm_translate_internal(u32 cycles, void *regptr);
void init_emitter(bool must_swap);

cpu_alert_type function_cc write_io_register8(u32 address, u32 value);
cpu_alert_type function_cc write_io_register16(u32 address, u32 value);
cpu_alert_type function_cc write_io_register32(u32 address, u32 value);
u8 read_backup(u32 address);
void function_cc write_backup(u32 address, u32 value);
u32 function_cc read_eeprom(void);
void function_cc write_eeprom(u32 address, u32 value);
void function_cc write_gpio(u32 address, u32 value);
void process_cheats(void);

static inline void touch_gamepak_page(u32 physical_index)
{
  (void)physical_index;
}

void *memset(void *dst, int value, unsigned int size);
void *memcpy(void *dst, const void *src, unsigned int size);
int printf(const char *fmt, ...);
int fflush(void *stream);
extern void *stdout;

#endif
