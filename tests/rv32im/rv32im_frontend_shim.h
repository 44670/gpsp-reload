/* Freestanding declarations for qemu-user tests that link cpu_threaded.c.
 *
 * This intentionally replaces common.h only for test builds that pass it via
 * -include. Production frontend builds keep using common.h and the normal
 * emulator headers.
 */

#ifndef RV32IM_FRONTEND_SHIM_H
#define RV32IM_FRONTEND_SHIM_H

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

#define MAX_TRANSLATION_GATES 8

extern u8 *memory_map_read[8 * 1024];
extern u8 ewram[1024 * 256 * 2];
extern u8 iwram[1024 * 32 * 2];
extern u16 io_registers[512];
extern u32 translation_gate_targets;
extern u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
extern u32 cheat_master_hook;
extern u32 flush_ram_count;
extern u8 *rom_translation_cache;
extern u8 *ram_translation_cache;
extern u8 *rom_translation_ptr;
extern u8 *ram_translation_ptr;
extern u8 *last_rom_translation_ptr;
extern u8 *last_ram_translation_ptr;
extern const u32 def_seq_cycles[16][2];

u8 *load_gamepak_page(u32 physical_index);
void init_dynarec_caches(void);
void flush_translation_cache_rom(void);
bool translate_block_arm(u32 pc, bool ram_region);
bool translate_block_thumb(u32 pc, bool ram_region);

static inline void touch_gamepak_page(u32 physical_index)
{
  (void)physical_index;
}

void *memset(void *dst, int value, unsigned int size);
int printf(const char *fmt, ...);
int fflush(void *stream);
extern void *stdout;

#endif
