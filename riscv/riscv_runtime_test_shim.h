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

typedef u8 cpu_alert_type;

#define CPU_ALERT_NONE 0
#define CPU_ALERT_HALT (1 << 0)
#define CPU_ALERT_SMC (1 << 1)
#define CPU_ALERT_IRQ (1 << 2)

typedef enum
{
  REG_LR = 14,
  REG_PC = 15,
  REG_CPSR = 16,
  CPU_MODE = 17,
  CPU_HALT_STATE = 18,
  REG_BUS_VALUE = 19,
  REG_MAX = 64
} riscv_test_reg_numbers;

#define MODE_USER 0x00
#define MODE_SYSTEM 0x10
#define MODE_IRQ 0x11
#define MODE_FIQ 0x12
#define MODE_SUPERVISOR 0x13
#define MODE_ABORT 0x14
#define MODE_UNDEFINED 0x15
#define MODE_INVALID 0x16
#define REG_MODE(m) (reg_mode[(m) & 0xf])
#define REG_SPSR(m) (spsr[(m) & 0xf])
#define PRIVMODE(m) ((m) >> 4)

#define CPU_ACTIVE 0
#define CPU_HALT 1
#define cycles_to_run(c) ((c) & 0x7FFF)
#define completed_frame(c) ((c) & 0x80000000)

extern u32 reg[REG_MAX];
extern u32 spsr[6];
extern u32 reg_mode[7][7];
extern u32 idle_loop_target_pc;
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
u32 function_cc read_memory8s(u32 address);
u32 function_cc read_memory16(u32 address);
u32 function_cc read_memory16s(u32 address);
u32 function_cc read_memory32(u32 address);
cpu_alert_type function_cc write_memory8(u32 address, u8 value);
cpu_alert_type function_cc write_memory16(u32 address, u16 value);
cpu_alert_type function_cc write_memory32(u32 address, u32 value);
u32 check_and_raise_interrupts(void);
void flush_translation_cache_ram(void);
void set_cpu_mode(u32 new_mode);
u32 function_cc update_gba(int remaining_cycles);
u8 function_cc *block_lookup_address_arm(u32 pc);
u8 function_cc *block_lookup_address_thumb(u32 pc);
void init_bios_hooks(void);

#endif
