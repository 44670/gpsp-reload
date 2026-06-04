/* Minimal declarations for qemu-user tests of riscv_runtime.c.
 *
 * This shim is intentionally smaller than common.h so the RV32 freestanding
 * tests do not need a RISC-V libc sysroot.
 */

#ifndef RISCV_RUNTIME_TEST_SHIM_H
#define RISCV_RUNTIME_TEST_SHIM_H

#include <stdbool.h>

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long long u64;
typedef signed long long s64;

#define function_cc

typedef enum
{
  REG_PC = 15,
  REG_CPSR = 16,
  CPU_HALT_STATE = 18,
  REG_MAX = 64
} riscv_test_reg_numbers;

#define CPU_ACTIVE 0
#define cycles_to_run(c) ((c) & 0x7FFF)
#define completed_frame(c) ((c) & 0x80000000)

extern u32 reg[REG_MAX];
extern u32 rom_cache_watermark;
extern u32 gamepak_sticky_bit[1024 / 32];

static inline void clear_gamepak_stickybits(void)
{
  unsigned i;

  for (i = 0; i < (1024 / 32); i++)
    gamepak_sticky_bit[i] = 0;
}

void execute_arm(u32 cycles);
u32 function_cc read_memory8(u32 address);
u32 function_cc read_memory32(u32 address);
u32 function_cc update_gba(int remaining_cycles);
u8 function_cc *block_lookup_address_arm(u32 pc);
u8 function_cc *block_lookup_address_thumb(u32 pc);
void init_bios_hooks(void);

#endif
