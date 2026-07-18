#include "backend_compare_frontend_shim.h"

#if defined(BACKEND_COMPARE_RV32IM)
#include "riscv/riscv_emit.h"
#endif

typedef unsigned int usize;

#define ROM_BASE 0x08000000u
#define ROM_BYTES (512u * 1024u)
#define ROM_PAGE_BYTES (32u * 1024u)
#define WORKLOAD_PC 0x08010000u
#define EWRAM_BASE 0x02000000u
#define FRAME_COMPLETE 0x80000000u
#define HASH_INIT 2166136261u
#define WARM_ITERATIONS 64u
#define RUN_CYCLE_BUDGET 0x7fffu
#define MAPPED_ALU_WORDS 65u
#define MAPPED_ALU_CYCLES (MAPPED_ALU_WORDS * 6u + 8u * 2u)
#define MEMORY_READ_WORDS 49u
#define MEMORY_READ_CYCLES (MEMORY_READ_WORDS * 6u + 48u * 2u)
#define MEMORY_WRITE_WORDS 16u
#define MEMORY_WRITE_CYCLES (MEMORY_WRITE_WORDS * 6u + 15u)

#if defined(BACKEND_COMPARE_RV32IM)
#define BACKEND_NAME "rv32im"
#define HARNESS_MODE "cpu_threaded_frontend_rv32im"
#define SYS_WRITE 64
#define SYS_EXIT 93
#define SYS_MMAP 222
#define SYS_RISCV_FLUSH_ICACHE 259
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32
#elif defined(BACKEND_COMPARE_MIPS)
#define BACKEND_NAME "mips"
#define HARNESS_MODE "cpu_threaded_frontend_mips"
#define SYS_WRITE 4004
#define SYS_EXIT 4001
#define SYS_CACHEFLUSH 4147
#define MIPS_CACHE_BOTH 3
#else
#error "select one backend comparison target"
#endif

#define STDOUT_FD 1

#if defined(BACKEND_COMPARE_RV32IM)
u32 reg[REG_MAX];
u32 spsr[6];
u32 reg_mode[7][7];
u8 *memory_map_read[8 * 1024];
u8 ewram[1024 * 256 * 2] __attribute__((aligned(4)));
u8 iwram[1024 * 32 * 2] __attribute__((aligned(4)));
u8 vram[1024 * 96];
u16 palette_ram[512];
u16 palette_ram_converted[512];
u16 oam_ram[512];
u16 io_registers[512];
#endif

u32 idle_loop_target_pc = 0xffffffffu;
u32 translation_gate_targets;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 cheat_master_hook = 0xffffffffu;
u32 flush_ram_count;
void *stdout;

#if !defined(BACKEND_COMPARE_REAL_GBA_MEMORY)
u8 bios_rom[1024 * 16];
u32 gamepak_sticky_bit[1024 / 32];
const u32 def_seq_cycles[16][2] =
{
  { 1, 1 }, { 1, 1 }, { 3, 6 }, { 1, 1 },
  { 1, 1 }, { 1, 2 }, { 1, 2 }, { 1, 2 },
  { 3, 6 }, { 3, 6 }, { 5, 9 }, { 5, 9 },
  { 9, 17 }, { 9, 17 }, { 1, 1 }, { 1, 1 },
};
#else
extern u32 gamepak_size;
#endif

const u8 bit_count[256] =
{
  0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
};

const u32 cpu_modes[16] =
{
  MODE_USER, MODE_FIQ, MODE_IRQ, MODE_SUPERVISOR,
  MODE_INVALID, MODE_INVALID, MODE_INVALID, MODE_ABORT,
  MODE_INVALID, MODE_INVALID, MODE_INVALID, MODE_UNDEFINED,
  MODE_INVALID, MODE_INVALID, MODE_INVALID, MODE_SYSTEM,
};
const u32 cpsr_masks[4][2] =
{
  { 0x00000000u, 0x00000000u }, { 0x00000020u, 0x000000efu },
  { 0xf0000000u, 0xf0000000u }, { 0xf0000020u, 0xf00000efu },
};
const u32 spsr_masks[4] =
  { 0x00000000u, 0x000000efu, 0xf0000000u, 0xf00000efu };

static u8 g_rom[ROM_BYTES];
static u8 g_open_bus[ROM_PAGE_BYTES];
static u32 g_update_calls;
static s32 g_update_cycles;
static u32 g_execute_arm_calls;
static u32 g_restart_pc;
static u32 g_io_event_count;
static u32 g_io_event_hash;
static u32 g_alert_event_count;
static u32 g_alert_event_hash;
static u32 g_irq_checks;

extern u32 iwram_code_min;
extern u32 iwram_code_max;
extern u32 ewram_code_min;
extern u32 ewram_code_max;

#if defined(BACKEND_COMPARE_RV32IM)
static long syscall1(long number, long arg0)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
  return a0;
}

static long syscall3(long number, long arg0, long arg1, long arg2)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a7)
                   : "memory");
  return a0;
}

static long syscall6(long number, long arg0, long arg1, long arg2,
                     long arg3, long arg4, long arg5)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  register long a4 __asm__("a4") = arg4;
  register long a5 __asm__("a5") = arg5;
  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                     "r"(a7)
                   : "memory");
  return a0;
}
#else
static long syscall1(long number, long arg0)
{
  register long v0 __asm__("$2") = number;
  register long a0 __asm__("$4") = arg0;
  register long a3 __asm__("$7") = 0;
  __asm__ volatile("syscall"
                   : "+r"(v0), "+r"(a3)
                   : "r"(a0)
                   : "memory");
  return a3 ? -v0 : v0;
}

static long syscall3(long number, long arg0, long arg1, long arg2)
{
  register long v0 __asm__("$2") = number;
  register long a0 __asm__("$4") = arg0;
  register long a1 __asm__("$5") = arg1;
  register long a2 __asm__("$6") = arg2;
  register long a3 __asm__("$7") = 0;
  __asm__ volatile("syscall"
                   : "+r"(v0), "+r"(a3)
                   : "r"(a0), "r"(a1), "r"(a2)
                   : "memory");
  return a3 ? -v0 : v0;
}
#endif

static void sys_exit(int code)
{
  syscall1(SYS_EXIT, code);
  for (;;)
    ;
}

static void put_raw(const char *text)
{
  const char *end = text;

  while (*end)
    end++;
  syscall3(SYS_WRITE, STDOUT_FD, (long)text, end - text);
}

static void put_u32_dec(u32 value)
{
  char buffer[11];
  u32 pos = sizeof(buffer);

  if (!value)
  {
    put_raw("0");
    return;
  }
  while (value)
  {
    buffer[--pos] = (char)('0' + value % 10u);
    value /= 10u;
  }
  syscall3(SYS_WRITE, STDOUT_FD, (long)&buffer[pos], sizeof(buffer) - pos);
}

static void put_u32_hex(u32 value)
{
  static const char hex[] = "0123456789abcdef";
  char buffer[10];
  u32 i;

  buffer[0] = '0';
  buffer[1] = 'x';
  for (i = 0; i < 8u; i++)
    buffer[2u + i] = hex[(value >> ((7u - i) * 4u)) & 0x0fu];
  syscall3(SYS_WRITE, STDOUT_FD, (long)buffer, sizeof(buffer));
}

void *memset(void *dst, int value, unsigned int size)
{
  u8 *out = (u8 *)dst;

  while (size--)
    *out++ = (u8)value;
  return dst;
}

void *memcpy(void *dst, const void *src, unsigned int size)
{
  u8 *out = (u8 *)dst;
  const u8 *in = (const u8 *)src;

  while (size--)
    *out++ = *in++;
  return dst;
}

int printf(const char *fmt, ...)
{
  (void)fmt;
  return 0;
}

int fflush(void *stream)
{
  (void)stream;
  return 0;
}

void __clear_cache(void *start, void *end)
{
#if defined(BACKEND_COMPARE_RV32IM)
  syscall3(SYS_RISCV_FLUSH_ICACHE, (long)start, (long)end, 0);
#else
  syscall3(SYS_CACHEFLUSH, (long)start,
           (long)((u8 *)end - (u8 *)start), MIPS_CACHE_BOTH);
#endif
}

__attribute__((noinline))
void backend_compare_measure_begin(void)
{
#if defined(BACKEND_COMPARE_RV32IM)
  __asm__ volatile("addi zero, zero, 25" ::: "memory");
#else
  __asm__ volatile("sll $zero, $zero, 1" ::: "memory");
#endif
}

__attribute__((noinline))
void backend_compare_measure_end(void)
{
#if defined(BACKEND_COMPARE_RV32IM)
  __asm__ volatile("addi zero, zero, 27" ::: "memory");
#else
  __asm__ volatile("sll $zero, $zero, 2" ::: "memory");
#endif
}

static void store16(u8 *base, u32 offset, u16 value)
{
  base[offset] = (u8)value;
  base[offset + 1u] = (u8)(value >> 8);
}

static void store32(u8 *base, u32 offset, u32 value)
{
  base[offset] = (u8)value;
  base[offset + 1u] = (u8)(value >> 8);
  base[offset + 2u] = (u8)(value >> 16);
  base[offset + 3u] = (u8)(value >> 24);
}

static u16 load16(const u8 *base, u32 offset)
{
  return (u16)(base[offset] | ((u16)base[offset + 1u] << 8));
}

static u32 load32(const u8 *base, u32 offset)
{
  return (u32)base[offset] | ((u32)base[offset + 1u] << 8) |
         ((u32)base[offset + 2u] << 16) |
         ((u32)base[offset + 3u] << 24);
}

static u32 ror32(u32 value, u32 shift)
{
  return shift ? (value >> shift) | (value << (32u - shift)) : value;
}

static u32 hash_word(u32 hash, u32 value)
{
  u32 i;

  for (i = 0; i < 4u; i++)
  {
    hash ^= (value >> (i * 8u)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

static u32 state_hash(void)
{
  u32 hash = HASH_INIT;
  u32 i;

  for (i = 0; i < 16u; i++)
    hash = hash_word(hash, reg[i]);
  hash = hash_word(hash, reg[REG_CPSR]);
  hash = hash_word(hash, reg[CPU_MODE]);
  hash = hash_word(hash, reg[CPU_HALT_STATE]);
  hash = hash_word(hash, reg[REG_BUS_VALUE]);
  for (i = 0; i < 6u; i++)
    hash = hash_word(hash, spsr[i]);
  for (i = 0; i < 7u * 7u; i++)
    hash = hash_word(hash, ((u32 *)reg_mode)[i]);
  return hash;
}

static u32 memory_hash(void)
{
  u32 hash = HASH_INIT;
  u32 i;

  for (i = 0; i < 64u; i++)
  {
    hash ^= ewram[i];
    hash *= 16777619u;
  }
  return hash;
}

static u32 io_write_hash(void)
{
  const u8 *io = (const u8 *)io_registers;
  u32 hash = HASH_INIT;
  u32 i;

  for (i = 0; i < 16u; i++)
  {
    hash ^= io[i];
    hash *= 16777619u;
  }
  hash = hash_word(hash, g_io_event_count);
  return hash_word(hash, g_io_event_hash);
}

static u32 alert_hash(void)
{
  u32 hash = hash_word(HASH_INIT, g_alert_event_count);

  hash = hash_word(hash, g_alert_event_hash);
  return hash_word(hash, g_irq_checks);
}

static u32 smc_hash(void)
{
  return hash_word(HASH_INIT, flush_ram_count);
}

static u32 scheduler_hash(void)
{
  u32 hash = hash_word(HASH_INIT, g_update_calls);

  return hash_word(hash, (u32)g_update_cycles);
}

static void map_read_region(u32 start, u32 bytes, u8 *base, u32 mask)
{
  u32 address;

  for (address = start; address < start + bytes; address += ROM_PAGE_BYTES)
    memory_map_read[address >> 15] = base + (address & mask);
}

static void init_memory_map(void)
{
  u32 address;

  memset(memory_map_read, 0, sizeof(u8 *) * 8u * 1024u);
  map_read_region(EWRAM_BASE, 0x01000000u, ewram, 0x3ffffu);
  map_read_region(0x03000000u, 0x01000000u, iwram + 0x8000, 0x7fffu);
  for (address = ROM_BASE; address < 0x0e000000u; address += ROM_PAGE_BYTES)
  {
    u32 offset = (address - ROM_BASE) & 0x01ffffffu;
    memory_map_read[address >> 15] =
      offset < sizeof(g_rom) ? g_rom + offset : g_open_bus;
  }
}

#if defined(BACKEND_COMPARE_REAL_GBA_MEMORY)
u8 *__wrap_gba_memory_real_load_gamepak_page(u32 physical_index)
#else
u8 *load_gamepak_page(u32 physical_index)
#endif
{
  u32 offset = physical_index * ROM_PAGE_BYTES;

  return offset < sizeof(g_rom) ? g_rom + offset : g_open_bus;
}

static u8 *address_ptr(u32 address)
{
  switch (address >> 24)
  {
    case 0x02:
      return ewram + (address & 0x3ffffu);
    case 0x03:
      return iwram + 0x8000u + (address & 0x7fffu);
    case 0x04:
      return (u8 *)io_registers + (address & 0x3ffu);
    case 0x05:
      return (u8 *)palette_ram + (address & 0x3ffu);
    case 0x06:
      return vram + ((address & 0x1ffffu) % (1024u * 96u));
    case 0x07:
      return (u8 *)oam_ram + (address & 0x3ffu);
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:
      return g_rom + ((address - ROM_BASE) & (ROM_BYTES - 1u));
    default:
      return g_open_bus;
  }
}

#if !defined(BACKEND_COMPARE_REAL_GBA_MEMORY)
u32 function_cc read_memory8(u32 address)
{
  return *address_ptr(address);
}

u32 function_cc read_memory8s(u32 address)
{
  return (u32)(s32)(s8)read_memory8(address);
}

u32 function_cc read_memory16(u32 address)
{
  u32 value = load16(address_ptr(address & ~1u), 0);

  return (address & 1u) ? ror32(value, 8u) : value;
}

u32 function_cc read_memory16s(u32 address)
{
  if (address & 1u)
    return (u32)(s32)(s8)read_memory8(address);
  return (u32)(s32)(s16)read_memory16(address);
}

u32 function_cc read_memory32(u32 address)
{
  return ror32(load32(address_ptr(address & ~3u), 0),
               (address & 3u) * 8u);
}
#endif

static cpu_alert_type record_io_write(u32 width, u32 address, u32 value)
{
  cpu_alert_type alert = CPU_ALERT_NONE;

  if ((address & 0x3ffu) == 8u)
  {
    alert = CPU_ALERT_SMC | CPU_ALERT_IRQ;
    ewram_code_min = 0;
    ewram_code_max = 64u;
    iwram_code_min = ~0u;
    iwram_code_max = 0;
  }
  g_io_event_count++;
  g_io_event_hash = hash_word(g_io_event_hash, width);
  g_io_event_hash = hash_word(g_io_event_hash, address);
  g_io_event_hash = hash_word(g_io_event_hash, value);
  g_io_event_hash = hash_word(g_io_event_hash, alert);
  if (alert)
  {
    g_alert_event_count++;
    g_alert_event_hash = hash_word(g_alert_event_hash, alert);
  }
  return alert;
}

cpu_alert_type function_cc write_io_register8(u32 address, u32 value)
{
  value &= 0xffu;
  *((u8 *)io_registers + (address & 0x3ffu)) = (u8)value;
  return record_io_write(8u, address, value);
}

cpu_alert_type function_cc write_io_register16(u32 address, u32 value)
{
  value &= 0xffffu;
  store16((u8 *)io_registers + (address & 0x3ffu), 0, (u16)value);
  return record_io_write(16u, address, value);
}

cpu_alert_type function_cc write_io_register32(u32 address, u32 value)
{
  store32((u8 *)io_registers + (address & 0x3ffu), 0, value);
  return record_io_write(32u, address, value);
}

cpu_alert_type function_cc write_memory8(u32 address, u8 value)
{
  if ((address >> 24) == 0x04u)
    return write_io_register8(address, value);
  *address_ptr(address) = value;
  return CPU_ALERT_NONE;
}

cpu_alert_type function_cc write_memory16(u32 address, u16 value)
{
  if ((address >> 24) == 0x04u)
    return write_io_register16(address, value);
  store16(address_ptr(address), 0, value);
  return CPU_ALERT_NONE;
}

cpu_alert_type function_cc write_memory32(u32 address, u32 value)
{
  if ((address >> 24) == 0x04u)
    return write_io_register32(address, value);
  store32(address_ptr(address), 0, value);
  return CPU_ALERT_NONE;
}

#if !defined(BACKEND_COMPARE_REAL_GBA_MEMORY)
u8 read_backup(u32 address)
{
  (void)address;
  return 0xffu;
}

void function_cc write_backup(u32 address, u32 value)
{
  (void)address;
  (void)value;
}

u32 function_cc read_eeprom(void)
{
  return 1u;
}
#endif

void function_cc write_eeprom(u32 address, u32 value)
{
  (void)address;
  (void)value;
}

void function_cc write_gpio(u32 address, u32 value)
{
  (void)address;
  (void)value;
}

void process_cheats(void)
{
}

u32 check_and_raise_interrupts(void)
{
  g_irq_checks++;
  return 0;
}

void execute_arm(u32 cycles)
{
  (void)cycles;
  g_execute_arm_calls++;
}

void set_cpu_mode(u32 new_mode)
{
  u32 old_mode = reg[CPU_MODE];
  u32 i;

  if (old_mode == new_mode)
    return;
  if (new_mode == MODE_FIQ)
  {
    for (i = 8u; i < 15u; i++)
      reg_mode[old_mode & 0x0fu][i - 8u] = reg[i];
  }
  else
  {
    reg_mode[old_mode & 0x0fu][5] = reg[REG_SP];
    reg_mode[old_mode & 0x0fu][6] = reg[REG_LR];
  }

  if (old_mode == MODE_FIQ)
  {
    for (i = 8u; i < 15u; i++)
      reg[i] = reg_mode[new_mode & 0x0fu][i - 8u];
  }
  else
  {
    reg[REG_SP] = reg_mode[new_mode & 0x0fu][5];
    reg[REG_LR] = reg_mode[new_mode & 0x0fu][6];
  }
  reg[CPU_MODE] = new_mode;
}

u32 function_cc update_gba(int remaining_cycles)
{
  g_update_calls++;
  g_update_cycles = (s32)remaining_cycles;
  reg[REG_PC] = g_restart_pc;
  return FRAME_COMPLETE;
}

static void clear_cpu_and_memory(void)
{
  u32 i;

  memset(reg, 0, sizeof(u32) * REG_MAX);
  memset(spsr, 0, sizeof(u32) * 6u);
  memset(reg_mode, 0, sizeof(u32) * 7u * 7u);
  memset(ewram, 0, 1024u * 256u * 2u);
  memset(io_registers, 0, sizeof(u16) * 512u);
  for (i = 0; i < 64u; i++)
    ewram[i] = (u8)(i * 37u + 11u);
  for (i = 0; i < 6u; i++)
    spsr[i] = MODE_SYSTEM;
  reg[0] = 0x10203040u;
  reg[1] = 0x02000000u;
  reg[2] = 0x01020304u;
  reg[3] = 0x89abcdefu;
  reg[4] = 0x76543210u;
  reg[5] = 0x0f0f0f0fu;
  reg[6] = 0xf0f0f0f0u;
  reg[7] = 3u;
  reg[8] = 5u;
  reg[9] = 7u;
  reg[10] = 11u;
  reg[11] = 13u;
  reg[12] = 17u;
  reg[REG_SP] = 0x03007f00u;
  reg[REG_LR] = 0x08000000u;
  reg[REG_PC] = WORKLOAD_PC;
  reg[REG_CPSR] = 0x2000001fu;
  reg[CPU_MODE] = MODE_SYSTEM;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_mode[MODE_USER & 0x0fu][5] = 0x03007f00u;
  reg_mode[MODE_IRQ & 0x0fu][5] = 0x03007fa0u;
  reg_mode[MODE_FIQ & 0x0fu][5] = 0x03007fa0u;
  reg_mode[MODE_SUPERVISOR & 0x0fu][5] = 0x03007fe0u;
  g_update_calls = 0;
  g_update_cycles = (s32)0x7fffffffu;
  g_io_event_count = 0;
  g_io_event_hash = HASH_INIT;
  g_alert_event_count = 0;
  g_alert_event_hash = HASH_INIT;
  g_irq_checks = 0;
  flush_ram_count = 0;
}

static void install_base_rom(void)
{
  memset(g_rom, 0, sizeof(g_rom));
  memset(g_open_bus, 0, sizeof(g_open_bus));
  store32(g_rom, 8u, 0xeafffffeu);
}

static void install_mapped_alu(void)
{
  static const u32 operations[8] =
  {
    0xe0802001u, 0xe0223001u, 0xe0434000u, 0xe1845001u,
    0xe0056002u, 0xe1a07006u, 0xe0080197u, 0xe0899007u,
  };
  u32 i;

  install_base_rom();
  for (i = 0; i < 64u; i++)
    store32(g_rom, WORKLOAD_PC - ROM_BASE + i * 4u,
            operations[i & 7u]);
  store32(g_rom, WORKLOAD_PC - ROM_BASE + 64u * 4u, 0xeafffffeu);
}

static void install_memory_read(void)
{
  u32 i;

  install_base_rom();
  for (i = 0; i < 16u; i++)
  {
    u32 offset = (i * 4u) & 0x3cu;
    u32 halfword_offset = offset + 2u;
    store32(g_rom, WORKLOAD_PC - ROM_BASE + (i * 3u + 0u) * 4u,
            0xe5912000u | offset);
    store32(g_rom, WORKLOAD_PC - ROM_BASE + (i * 3u + 1u) * 4u,
            0xe5d13000u | ((offset + 1u) & 0xfffu));
    store32(g_rom, WORKLOAD_PC - ROM_BASE + (i * 3u + 2u) * 4u,
            0xe1d140b0u | ((halfword_offset & 0xf0u) << 4) |
              (halfword_offset & 0x0fu));
  }
  store32(g_rom, WORKLOAD_PC - ROM_BASE + 48u * 4u, 0xeafffffeu);
}

static void install_memory_write(void)
{
  u32 i;

  install_base_rom();
  for (i = 0; i < 4u; i++)
  {
    u32 offset = i * 12u;
    u32 halfword_offset = offset + 6u;
    store32(g_rom, WORKLOAD_PC - ROM_BASE + (i * 3u + 0u) * 4u,
            0xe5812000u | offset);
    store32(g_rom, WORKLOAD_PC - ROM_BASE + (i * 3u + 1u) * 4u,
            0xe5c13000u | (offset + 4u));
    store32(g_rom, WORKLOAD_PC - ROM_BASE + (i * 3u + 2u) * 4u,
            0xe1c140b0u | ((halfword_offset & 0xf0u) << 4) |
              (halfword_offset & 0x0fu));
  }
  store32(g_rom, WORKLOAD_PC - ROM_BASE + 12u * 4u, 0xe58a5000u);
  store32(g_rom, WORKLOAD_PC - ROM_BASE + 13u * 4u, 0xe5ca6004u);
  store32(g_rom, WORKLOAD_PC - ROM_BASE + 14u * 4u, 0xe1ca70b8u);
  store32(g_rom, WORKLOAD_PC - ROM_BASE + 15u * 4u, 0xeafffffeu);
}

static void prepare_memory_write(void)
{
  reg[10] = 0x04000000u;
}

static int print_phase(const char *workload, const char *phase,
                       u32 guest_insns, u32 guest_cycles,
                       u32 generated_bytes, u32 code_bytes_added,
                       u32 expected_updates, u32 io_events_per_run,
                       u32 alerts_per_run, u32 smc_flushes_per_run)
{
  u32 state = state_hash();
  u32 memory = memory_hash();
  u32 scheduler = scheduler_hash();
  u32 io_write = io_write_hash();
  u32 alerts = alert_hash();
  u32 smc = smc_hash();
  int passed = g_execute_arm_calls == 0u &&
               g_update_calls == expected_updates &&
               g_update_cycles == 0 && generated_bytes != 0u &&
               (phase[0] == 'c' || code_bytes_added == 0u) &&
               g_io_event_count == expected_updates * io_events_per_run &&
               g_alert_event_count == expected_updates * alerts_per_run &&
               g_irq_checks == expected_updates * alerts_per_run &&
               flush_ram_count == expected_updates * smc_flushes_per_run;

  put_raw("result=");
  put_raw(passed ? "PASS" : "FAIL");
  put_raw(" command=backend-compare-workload backend=" BACKEND_NAME
          " workload=");
  put_raw(workload);
  put_raw(" phase=");
  put_raw(phase);
  put_raw(" guest_insns=");
  put_u32_dec(guest_insns);
  put_raw(" guest_cycles=");
  put_u32_dec(guest_cycles);
  put_raw(" generated_bytes=");
  put_u32_dec(generated_bytes);
  put_raw(" code_bytes_added=");
  put_u32_dec(code_bytes_added);
  put_raw(" state_hash=");
  put_u32_hex(state);
  put_raw(" memory_hash=");
  put_u32_hex(memory);
  put_raw(" ram_write_hash=");
  put_u32_hex(memory);
  put_raw(" io_write_hash=");
  put_u32_hex(io_write);
  put_raw(" alert_hash=");
  put_u32_hex(alerts);
  put_raw(" smc_hash=");
  put_u32_hex(smc);
  put_raw(" io_events=");
  put_u32_dec(g_io_event_count);
  put_raw(" alert_events=");
  put_u32_dec(g_alert_event_count);
  put_raw(" irq_checks=");
  put_u32_dec(g_irq_checks);
  put_raw(" smc_flushes=");
  put_u32_dec(flush_ram_count);
  put_raw(" scheduler_hash=");
  put_u32_hex(scheduler);
  put_raw(" update_calls=");
  put_u32_dec(g_update_calls);
  put_raw(" update_cycles=");
  put_u32_hex((u32)g_update_cycles);
  put_raw(" execute_arm_calls=");
  put_u32_dec(g_execute_arm_calls);
  put_raw(" harness_mode=" HARNESS_MODE " reason=");
  put_raw(passed ? "real_frontend_state_stable" :
                   "real_frontend_state_failed");
  put_raw("\n");
  return passed;
}

static int run_workload(const char *name, void (*install)(void),
                        void (*prepare)(void), u32 words, u32 cycles,
                        u32 io_events_per_run, u32 alerts_per_run,
                        u32 smc_flushes_per_run)
{
  u32 cache_start;
  u32 cache_after_cold;
  u32 cache_after_warm;
  u32 i;
  int passed;

  idle_loop_target_pc = WORKLOAD_PC + (words - 1u) * 4u;
  g_restart_pc = WORKLOAD_PC;
  install();
  flush_translation_cache_rom();
  clear_cpu_and_memory();
  if (prepare)
    prepare();
  cache_start = (u32)(rom_translation_ptr - rom_translation_cache);
  backend_compare_measure_begin();
  execute_arm_translate_internal(RUN_CYCLE_BUDGET, &reg[0]);
  backend_compare_measure_end();
  cache_after_cold = (u32)(rom_translation_ptr - rom_translation_cache);
  passed = print_phase(name, "cold", words, cycles,
                       cache_after_cold - cache_start,
                       cache_after_cold - cache_start, 1u,
                       io_events_per_run, alerts_per_run,
                       smc_flushes_per_run);

  clear_cpu_and_memory();
  if (prepare)
    prepare();
  backend_compare_measure_begin();
  for (i = 0; i < WARM_ITERATIONS; i++)
    execute_arm_translate_internal(RUN_CYCLE_BUDGET, &reg[0]);
  backend_compare_measure_end();
  cache_after_warm = (u32)(rom_translation_ptr - rom_translation_cache);
  passed &= print_phase(name, "warm", words * WARM_ITERATIONS,
                        cycles * WARM_ITERATIONS,
                        cache_after_cold - cache_start,
                        cache_after_warm - cache_after_cold,
                        WARM_ITERATIONS, io_events_per_run,
                        alerts_per_run, smc_flushes_per_run);
  return passed;
}

static void init_backend(void)
{
#if defined(BACKEND_COMPARE_RV32IM)
  long rom_cache = syscall6(SYS_MMAP, 0, ROM_TRANSLATION_CACHE_SIZE,
                            PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  long ram_cache = syscall6(SYS_MMAP, 0, RAM_TRANSLATION_CACHE_SIZE,
                            PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (rom_cache < 0 || ram_cache < 0)
  {
    put_raw("result=FAIL command=backend-compare reason=jit_cache_mmap_failed\n");
    sys_exit(2);
  }
  rom_translation_cache = (u8 *)rom_cache;
  ram_translation_cache = (u8 *)ram_cache;
#endif
  init_dynarec_caches();
  init_emitter(false);
}

#if defined(BACKEND_COMPARE_RV32IM)
u32 riscv_fast_read_u8(u32 address, u32 pc);
u32 riscv_fast_read_s8(u32 address, u32 pc);
u32 riscv_fast_read_u16(u32 address, u32 pc);
u32 riscv_fast_read_s16(u32 address, u32 pc);
u32 riscv_fast_read_u32(u32 address, u32 pc);
u32 backend_compare_call_fast_read(u32 address, u32 pc,
                                   u32 (*target)(u32, u32));

__asm__(
  ".text\n"
  ".align 2\n"
  ".globl backend_compare_call_fast_read\n"
  ".type backend_compare_call_fast_read, @function\n"
  "backend_compare_call_fast_read:\n"
  "  addi sp, sp, -16\n"
  "  sw ra, 12(sp)\n"
  "  sw s0, 8(sp)\n"
  "  lla s0, reg\n"
  "  jalr ra, a2, 0\n"
  "  lw s0, 8(sp)\n"
  "  lw ra, 12(sp)\n"
  "  addi sp, sp, 16\n"
  "  ret\n"
  ".size backend_compare_call_fast_read, "
  ".-backend_compare_call_fast_read\n");

static int verify_fast_ram_reads(void)
{
  static const u32 addresses[] =
  {
    0x02000000u, 0x02000001u, 0x02000002u, 0x02007ffdu,
    0x0203fffcu, 0x02fc1235u, 0x03000000u, 0x03000002u,
    0x03000003u, 0x03007ffdu, 0x03ff9235u, 0x01ffffffu,
    0x04000000u, 0x08000001u, 0x0d000002u,
  };
  u32 i;
  u32 byte_count;
  u32 cases = 0;

  for (i = 0; i < 0x40000u; i++)
    ewram[i] = (u8)(i * 37u + (i >> 8) * 11u + 0x53u);
  for (i = 0; i < 0x8000u; i++)
    iwram[0x8000u + i] = (u8)(i * 29u + (i >> 7) * 13u + 0xa7u);

#define CHECK_FAST_READ(width_name, fast_fn, reference_fn)                    \
  do                                                                          \
  {                                                                           \
    u32 address = addresses[i];                                               \
    bool fast_ram = (address >> 25) == 1u;                                    \
    u32 expected;                                                             \
    u32 actual;                                                               \
    reg[REG_PC] = 0x08001234u;                                                \
    expected = reference_fn(address);                                         \
    reg[REG_PC] = 0x0800abcdu;                                                \
    actual = backend_compare_call_fast_read(                                 \
      address, 0x08001234u, fast_fn);                                        \
    cases++;                                                                  \
    if (actual != expected)                                                   \
    {                                                                         \
      put_raw("result=FAIL command=backend-compare-fast-ram width="          \
              width_name " address=");                                      \
      put_u32_hex(address);                                                   \
      put_raw(" expected=");                                                \
      put_u32_hex(expected);                                                   \
      put_raw(" actual=");                                                  \
      put_u32_hex(actual);                                                     \
      put_raw(" reason=fast_ram_value_mismatch\n");                         \
      return 0;                                                               \
    }                                                                         \
    if ((fast_ram && reg[REG_PC] != 0x0800abcdu) ||                           \
        (!fast_ram && reg[REG_PC] != 0x08001234u))                            \
    {                                                                         \
      put_raw("result=FAIL command=backend-compare-fast-ram width="          \
              width_name " address=");                                      \
      put_u32_hex(address);                                                   \
      put_raw(" pc=");                                                      \
      put_u32_hex(reg[REG_PC]);                                               \
      put_raw(" reason=fast_ram_pc_visibility_mismatch\n");                 \
      return 0;                                                               \
    }                                                                         \
  } while (0)

  byte_count = (u32)(sizeof(addresses) / sizeof(addresses[0]));
  for (i = 0; i < byte_count; i++)
  {
    CHECK_FAST_READ("u8", riscv_fast_read_u8, read_memory8);
    CHECK_FAST_READ("s8", riscv_fast_read_s8, read_memory8s);
    CHECK_FAST_READ("u16", riscv_fast_read_u16, read_memory16);
    CHECK_FAST_READ("s16", riscv_fast_read_s16, read_memory16s);
    CHECK_FAST_READ("u32", riscv_fast_read_u32, read_memory32);
  }
#undef CHECK_FAST_READ

  put_raw("result=PASS command=backend-compare-fast-ram backend=rv32im ");
  put_raw("cases=");
  put_u32_dec(cases);
  put_raw(" regions=ewram,iwram,slow-path widths=u8,s8,u16,s16,u32 ");
  put_raw("alignment=0,1,2,3 mirroring=1 ");
  put_raw("boundaries=0x01ffffff,0x04000000 pc_visibility=1 ");
#if defined(BACKEND_COMPARE_REAL_GBA_MEMORY)
  put_raw("reference=linked_gba_memory.c ");
#else
  put_raw("reference=fixture_memory_model ");
#endif
  put_raw("reason=fast_ram_reads_match_memory_contract\n");
  return 1;
}
#endif

void _start(void)
{
  int passed;
#if defined(BACKEND_COMPARE_RV32IM)
  riscv_runtime_stats stats;
  u32 native_ops;
#endif

  install_base_rom();
  init_memory_map();
#if defined(BACKEND_COMPARE_REAL_GBA_MEMORY)
  gamepak_size = sizeof(g_rom);
  g_execute_arm_calls = 0;
  passed = verify_fast_ram_reads();
  put_raw("result=");
  put_raw(passed ? "PASS" : "FAIL");
  put_raw(" command=gba-memory-diff backend=rv32im cases=75 "
          "source=gba_memory.c fast_path=riscv_fast_read "
          "harness_mode=qemu_user_real_memory reason=");
  put_raw(passed ? "linked_real_memory_equal" : "linked_real_memory_mismatch");
  put_raw("\n");
  sys_exit(passed ? 0 : 1);
#else
  init_backend();
  g_execute_arm_calls = 0;
#if defined(BACKEND_COMPARE_RV32IM)
  passed = verify_fast_ram_reads();
#else
  passed = 1;
#endif
  passed &= run_workload("mapped_alu", install_mapped_alu, NULL,
                         MAPPED_ALU_WORDS, MAPPED_ALU_CYCLES, 0u, 0u, 0u);
  passed &= run_workload("memory_read", install_memory_read, NULL,
                         MEMORY_READ_WORDS, MEMORY_READ_CYCLES, 0u, 0u, 0u);
  passed &= run_workload("memory_write", install_memory_write,
                         prepare_memory_write, MEMORY_WRITE_WORDS,
                         MEMORY_WRITE_CYCLES, 3u, 1u, 1u);
#if defined(BACKEND_COMPARE_RV32IM)
  riscv_get_runtime_stats(&stats);
  native_ops = stats.native_data_proc_insns + stats.native_branch_insns +
               stats.native_load_insns + stats.native_store_insns +
               stats.native_psr_insns;
  passed &= stats.blocks_executed != 0u && native_ops != 0u &&
            stats.interpreter_fallbacks == 0u;
#endif
  put_raw("result=");
  put_raw(passed ? "PASS" : "FAIL");
  put_raw(" command=backend-compare backend=" BACKEND_NAME
          " workloads=3 phases=6 execute_arm_calls=");
  put_u32_dec(g_execute_arm_calls);
#if defined(BACKEND_COMPARE_RV32IM)
  put_raw(" native_blocks=");
  put_u32_dec(stats.blocks_executed);
  put_raw(" native_ops=");
  put_u32_dec(native_ops);
  put_raw(" fallbacks=");
  put_u32_dec(stats.interpreter_fallbacks);
#endif
  put_raw(" harness_mode=" HARNESS_MODE " reason=");
  put_raw(passed ? "real_frontend_workloads_complete" :
                   "real_frontend_workloads_failed");
  put_raw("\n");
  sys_exit(passed ? 0 : 1);
#endif
}
