#include "rv32im_frontend_shim.h"
#include "riscv_emit.h"

typedef unsigned int usize;

#define SYS_OPENAT 56
#define SYS_CLOSE 57
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_EXIT 93
#define SYS_MMAP 222
#define SYS_RISCV_FLUSH_ICACHE 259

#define AT_FDCWD (-100)
#define O_RDONLY 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32
#define STDOUT_FD 1

#ifndef ARMWRESTLER_ROM
#define ARMWRESTLER_ROM \
  "/home/john/ref/armwrestler-gba-fixed/armwrestler-gba-fixed.gba"
#endif

#define ROM_BASE 0x08000000u
#define ROM_MAX_BYTES (2u * 1024u * 1024u)
#define ROM_PAGE_BYTES (32u * 1024u)
#define EWRAM_BASE 0x02000000u
#define IWRAM_BASE 0x03000000u
#define IO_BASE 0x04000000u
#define PAL_BASE 0x05000000u
#define VRAM_BASE 0x06000000u
#define OAM_BASE 0x07000000u
#define ARM_MAIN_PC 0x080002d8u
#define ARM_VSYNC_PC 0x080004c4u
#define ARM_DRAW_RESULT_PC 0x08000634u
#define ARM_TESTNUM_MOV_PC 0x0800032cu
#define THUMB_MAIN_PC 0x08003ab0u
#define THUMB_VSYNC_PC 0x08003f7cu
#define THUMB_DRAW_RESULT_PC 0x08003fe4u
#define ARMWRESTLER_ARM_TESTS 5u
#define ARMWRESTLER_ARM_TOTAL_RESULTS 59u
#define ARMWRESTLER_THUMB_TESTS 3u
#define ARMWRESTLER_THUMB_TOTAL_RESULTS 20u
#define ARMWRESTLER_TOTAL_TESTS \
  (ARMWRESTLER_ARM_TESTS + ARMWRESTLER_THUMB_TESTS)
#define ARMWRESTLER_TOTAL_RESULTS \
  (ARMWRESTLER_ARM_TOTAL_RESULTS + ARMWRESTLER_THUMB_TOTAL_RESULTS)
#define RESULT_BASE 0x03000300u
#define FRAME_COMPLETE 0x80000000u
#define RUN_CYCLES 200000u
#define RUN_CHUNKS 64u

static const u32 g_arm_test_ids[ARMWRESTLER_ARM_TESTS] =
  { 0u, 1u, 2u, 3u, 4u };
static const u32 g_arm_test_results[ARMWRESTLER_ARM_TESTS] =
  { 15u, 6u, 16u, 10u, 12u };
static const u32 g_thumb_test_ids[ARMWRESTLER_THUMB_TESTS] =
  { 0u, 1u, 2u };
static const u32 g_thumb_test_results[ARMWRESTLER_THUMB_TESTS] =
  { 11u, 7u, 2u };

u32 reg[REG_MAX];
u32 spsr[6];
u32 reg_mode[7][7];
u32 idle_loop_target_pc = 0xffffffffu;
u32 translation_gate_targets;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 gamepak_sticky_bit[1024 / 32];
const u32 def_seq_cycles[16][2] =
{
  { 1, 1 }, { 1, 1 }, { 3, 6 }, { 1, 1 },
  { 1, 1 }, { 1, 2 }, { 1, 2 }, { 1, 2 },
  { 3, 6 }, { 3, 6 }, { 5, 9 }, { 5, 9 },
  { 9, 17 }, { 9, 17 }, { 1, 1 }, { 1, 1 },
};
u8 *memory_map_read[8 * 1024];
u8 ewram[1024 * 256 * 2];
u8 iwram[1024 * 32 * 2];
u16 io_registers[512];
u32 cheat_master_hook = 0xffffffffu;
u32 flush_ram_count;
void *stdout;

static u8 g_rom[ROM_MAX_BYTES];
static u8 g_rom_open_bus[ROM_PAGE_BYTES];
static u8 g_vram[1024 * 96];
static u8 g_palette_ram[1024];
static u8 g_oam_ram[1024];
static u32 g_rom_size;
static u32 g_execute_arm_calls;
static u32 g_execute_arm_cycles;
static u32 g_update_calls;
static u32 g_trace_count;
static u32 g_trace_first_pc;
static u32 g_trace_last_pc;
static u32 g_trace_hash = 2166136261u;
static u32 g_fallback_count;
static u32 g_fallback_first_pc;
static u32 g_fallback_last_pc;
static u32 g_fallback_hash = 2166136261u;
static u32 g_total_observed_results;
static u32 g_total_failure_mask;

static long syscall1(long n, long arg0)
{
  register long a7 __asm__("a7") = n;
  register long a0 __asm__("a0") = arg0;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
  return a0;
}

static long syscall3(long n, long arg0, long arg1, long arg2)
{
  register long a7 __asm__("a7") = n;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a7)
                   : "memory");
  return a0;
}

static long syscall6(long n, long arg0, long arg1, long arg2, long arg3,
                     long arg4, long arg5)
{
  register long a7 __asm__("a7") = n;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  register long a4 __asm__("a4") = arg4;
  register long a5 __asm__("a5") = arg5;
  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a7)
                   : "memory");
  return a0;
}

static void sys_exit(int code)
{
  syscall1(SYS_EXIT, code);
  for (;;)
    ;
}

static void put_raw(const char *text)
{
  const char *p = text;
  while (*p)
    p++;
  syscall3(SYS_WRITE, STDOUT_FD, (long)text, p - text);
}

static void put_u32_dec(u32 value)
{
  char buf[11];
  unsigned pos = sizeof(buf);

  if (value == 0)
  {
    put_raw("0");
    return;
  }

  while (value && pos)
  {
    buf[--pos] = (char)('0' + (value % 10u));
    value /= 10u;
  }
  syscall3(SYS_WRITE, STDOUT_FD, (long)&buf[pos], sizeof(buf) - pos);
}

static void put_u32_hex(u32 value)
{
  static const char hex[] = "0123456789abcdef";
  char buf[10];
  unsigned i;

  buf[0] = '0';
  buf[1] = 'x';
  for (i = 0; i < 8; i++)
    buf[2 + i] = hex[(value >> ((7u - i) * 4u)) & 0xfu];
  syscall3(SYS_WRITE, STDOUT_FD, (long)buf, sizeof(buf));
}

void *memset(void *dst, int value, unsigned int size)
{
  u8 *p = (u8 *)dst;
  while (size--)
    *p++ = (u8)value;
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
  syscall3(SYS_RISCV_FLUSH_ICACHE, (long)start, (long)end, 0);
}

static u32 fnv1a_update_u32(u32 hash, u32 value)
{
  unsigned i;

  for (i = 0; i < 4; i++)
  {
    hash ^= (value >> (i * 8u)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

static u16 load16(const u8 *base, u32 offset)
{
  return (u16)(base[offset] | (base[offset + 1u] << 8));
}

static u32 load32(const u8 *base, u32 offset)
{
  return (u32)base[offset] |
         ((u32)base[offset + 1u] << 8) |
         ((u32)base[offset + 2u] << 16) |
         ((u32)base[offset + 3u] << 24);
}

static u32 ror32(u32 value, u32 shift)
{
  return shift ? (value >> shift) | (value << (32u - shift)) : value;
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

static int load_rom(void)
{
  int fd = (int)syscall3(SYS_OPENAT, AT_FDCWD, (long)ARMWRESTLER_ROM,
                         O_RDONLY);
  u32 total = 0;

  if (fd < 0)
    return 0;

  for (;;)
  {
    long got = syscall3(SYS_READ, fd, (long)&g_rom[total],
                        ROM_MAX_BYTES - total);
    if (got < 0)
    {
      syscall1(SYS_CLOSE, fd);
      return 0;
    }
    if (got == 0)
      break;
    total += (u32)got;
    if (total == ROM_MAX_BYTES)
      break;
  }

  syscall1(SYS_CLOSE, fd);
  g_rom_size = total;
  return total != 0;
}

static void patch_word(u32 pc, u32 value)
{
  store32(g_rom, pc - ROM_BASE, value);
}

static void patch_halfword(u32 pc, u16 value)
{
  store16(g_rom, pc - ROM_BASE, value);
}

static void patch_bytes(u32 pc, const u8 *bytes, u32 count)
{
  u32 offset = pc - ROM_BASE;
  u32 i;

  for (i = 0; i < count; i++)
    g_rom[offset + i] = bytes[i];
}

static void patch_test_id(u32 test_id)
{
  patch_word(ARM_TESTNUM_MOV_PC, 0xe3a00000u | test_id);
}

static void patch_loaded_rom(void)
{
  static const u8 thumb_draw_result_patch[] =
  {
    0x0f, 0xb5, 0x08, 0x4b, 0x1a, 0x68, 0x01, 0x32,
    0x1a, 0x60, 0x98, 0x60, 0xde, 0x60, 0x00, 0x20,
    0x18, 0x61, 0x59, 0x68, 0x31, 0x43, 0x59, 0x60,
    0x58, 0x69, 0x82, 0x42, 0x00, 0xd3, 0xfe, 0xe7,
    0x0f, 0xbd, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03,
  };
  static const u32 draw_result_patch[] =
  {
    0xe3a03403u, 0xe2833c03u, 0xe593c000u, 0xe28cc001u,
    0xe583c000u, 0xe5830008u, 0xe583100cu, 0xe5832010u,
    0xe20100ffu, 0xe5932004u, 0xe1822000u, 0xe5832004u,
    0xe5932014u,
    0xe15c0002u, 0xba000001u, 0xe3a0200au, 0xe50322f8u,
    0xe1a0f00eu,
  };
  u32 i;

  patch_word(ARM_VSYNC_PC, 0xe1a0f00eu);
  for (i = 0; i < sizeof(draw_result_patch) / sizeof(draw_result_patch[0]); i++)
    patch_word(ARM_DRAW_RESULT_PC + i * 4u, draw_result_patch[i]);
  patch_halfword(THUMB_VSYNC_PC, 0x4770u);
  patch_bytes(THUMB_DRAW_RESULT_PC, thumb_draw_result_patch,
              sizeof(thumb_draw_result_patch));
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

  for (address = 0; address < 8u * 1024u; address++)
    memory_map_read[address] = 0;

  map_read_region(EWRAM_BASE, 0x01000000u, ewram, 0x3ffffu);
  map_read_region(IWRAM_BASE, 0x01000000u, iwram + 0x8000, 0x7fffu);

  for (address = ROM_BASE; address < 0x0e000000u; address += ROM_PAGE_BYTES)
  {
    u32 offset = (address - ROM_BASE) & 0x01ffffffu;
    memory_map_read[address >> 15] =
      offset < ROM_MAX_BYTES ? g_rom + offset : g_rom_open_bus;
  }
}

static u8 *addr_ptr(u32 address, u32 size)
{
  u32 region = address >> 24;
  u32 offset;

  (void)size;
  switch (region)
  {
    case 0x02:
      return ewram + (address & 0x3ffffu);
    case 0x03:
      return iwram + 0x8000u + (address & 0x7fffu);
    case 0x04:
      return (u8 *)io_registers + (address & 0x3ffu);
    case 0x05:
      return g_palette_ram + (address & 0x3ffu);
    case 0x06:
      return g_vram + (address & 0x1ffffu) % sizeof(g_vram);
    case 0x07:
      return g_oam_ram + (address & 0x3ffu);
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:
      offset = (address - ROM_BASE) & 0x01ffffffu;
      if (offset + size <= g_rom_size)
        return g_rom + offset;
      return g_rom_open_bus;
    default:
      return g_rom_open_bus;
  }
}

u8 *load_gamepak_page(u32 physical_index)
{
  u32 offset = physical_index * ROM_PAGE_BYTES;
  if (offset < ROM_MAX_BYTES)
    return g_rom + offset;
  return g_rom_open_bus;
}

u32 function_cc read_memory8(u32 address)
{
  return *addr_ptr(address, 1);
}

u32 function_cc read_memory8s(u32 address)
{
  return (u32)((s8)read_memory8(address));
}

u32 function_cc read_memory16(u32 address)
{
  if ((address & 0x0fffffffu) == 0x0000130u)
    return 0x03ffu;
  {
    u32 value = load16(addr_ptr(address & ~1u, 2), 0);
    return (address & 1u) ? ror32(value, 8) : value;
  }
}

u32 function_cc read_memory16s(u32 address)
{
  return (u32)((s16)read_memory16(address));
}

u32 function_cc read_memory32(u32 address)
{
  u32 rotate = (address & 3u) * 8u;
  return ror32(load32(addr_ptr(address & ~3u, 4), 0), rotate);
}

cpu_alert_type function_cc write_memory8(u32 address, u8 value)
{
  *addr_ptr(address, 1) = value;
  return CPU_ALERT_NONE;
}

cpu_alert_type function_cc write_memory16(u32 address, u16 value)
{
  store16(addr_ptr(address, 2), 0, value);
  return CPU_ALERT_NONE;
}

cpu_alert_type function_cc write_memory32(u32 address, u32 value)
{
  store32(addr_ptr(address, 4), 0, value);
  return CPU_ALERT_NONE;
}

u32 check_and_raise_interrupts(void)
{
  return 0;
}

void set_cpu_mode(u32 new_mode)
{
  u32 cpu_mode = reg[CPU_MODE];
  u32 i;

  if (cpu_mode == new_mode)
    return;

  if (new_mode == MODE_FIQ)
  {
    for (i = 8; i < 15; i++)
      reg_mode[cpu_mode & 0xfu][i - 8u] = reg[i];
  }
  else
  {
    reg_mode[cpu_mode & 0xfu][5] = reg[REG_SP];
    reg_mode[cpu_mode & 0xfu][6] = reg[REG_LR];
  }

  if (cpu_mode == MODE_FIQ)
  {
    for (i = 8; i < 15; i++)
      reg[i] = reg_mode[new_mode & 0xfu][i - 8u];
  }
  else
  {
    reg[REG_SP] = reg_mode[new_mode & 0xfu][5];
    reg[REG_LR] = reg_mode[new_mode & 0xfu][6];
  }

  reg[CPU_MODE] = new_mode;
}

u32 function_cc update_gba(int remaining_cycles)
{
  (void)remaining_cycles;
  g_update_calls++;
  return FRAME_COMPLETE;
}

void execute_arm(u32 cycles)
{
  g_execute_arm_calls++;
  g_execute_arm_cycles += cycles;
}

void riscv_note_runtime_block_execute(u32 start_pc, u32 end_pc, u32 thumb)
{
  (void)end_pc;
  (void)thumb;
  if (g_trace_count == 0)
    g_trace_first_pc = start_pc;
  g_trace_last_pc = start_pc;
  g_trace_count++;
  g_trace_hash = fnv1a_update_u32(g_trace_hash, start_pc);
}

void riscv_note_runtime_fallback(u32 kind, u32 pc, u32 thumb,
                                 u32 lookup_result, u32 cycles_remaining)
{
  (void)thumb;
  (void)lookup_result;
  (void)cycles_remaining;
  if (g_fallback_count == 0)
    g_fallback_first_pc = pc;
  g_fallback_last_pc = pc;
  g_fallback_count++;
  g_fallback_hash = fnv1a_update_u32(g_fallback_hash, kind);
  g_fallback_hash = fnv1a_update_u32(g_fallback_hash, pc);
}

static void init_cpu_state(void)
{
  unsigned i;

  for (i = 0; i < REG_MAX; i++)
    reg[i] = 0;
  for (i = 0; i < 6; i++)
    spsr[i] = MODE_SYSTEM;
  for (i = 0; i < 7 * 7; i++)
    ((u32 *)reg_mode)[i] = 0;

  reg[REG_SP] = 0x03007f00u;
  reg[REG_PC] = ARM_MAIN_PC;
  reg[REG_CPSR] = 0x0000001fu;
  reg[CPU_MODE] = MODE_SYSTEM;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
}

static void init_thumbwrestler_cpu_state(void)
{
  init_cpu_state();
  reg[REG_PC] = THUMB_MAIN_PC;
  reg[REG_CPSR] = 0x0000003fu;
  reg[8] = 0;
  reg[9] = 3;
  reg[10] = 0;
  reg[11] = 0xffffffffu;
}

static void clear_runtime_memory(void)
{
  memset(ewram, 0, sizeof(ewram));
  memset(iwram, 0, sizeof(iwram));
  memset(io_registers, 0, sizeof(io_registers));
  memset(g_vram, 0, sizeof(g_vram));
  memset(g_palette_ram, 0, sizeof(g_palette_ram));
  memset(g_oam_ram, 0, sizeof(g_oam_ram));
}

static void *alloc_exec(unsigned bytes)
{
  long ret = syscall6(SYS_MMAP, 0, bytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ret < 0)
    return 0;
  return (void *)ret;
}

static void init_dynarec_for_armwrestler(void)
{
  rom_translation_cache = (u8 *)alloc_exec(ROM_TRANSLATION_CACHE_SIZE);
  ram_translation_cache = (u8 *)alloc_exec(RAM_TRANSLATION_CACHE_SIZE);
  if (!rom_translation_cache || !ram_translation_cache)
  {
    put_raw("result=FAIL command=armwrestler reason=jit_cache_mmap_failed\n");
    sys_exit(2);
  }
}

static void reset_dynarec_for_armwrestler(void)
{
  init_dynarec_caches();
  rom_cache_watermark = 16u;
  rom_translation_ptr = rom_translation_cache + rom_cache_watermark;
  last_rom_translation_ptr = rom_translation_ptr;
}

static u32 result_word(u32 offset)
{
  return load32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + offset);
}

static u32 runtime_native_counter_sum(const riscv_runtime_stats *stats)
{
  return stats->native_data_proc_insns + stats->native_branch_insns +
         stats->native_load_insns + stats->native_store_insns +
         stats->native_psr_insns;
}

static void print_summary(const char *result, const char *suite, u32 test_id,
                          u32 expected_results, const char *reason)
{
  riscv_runtime_stats stats;
  u32 result_count = result_word(0);
  u32 result_mask = result_word(4);
  u32 code_bytes;

  riscv_get_runtime_stats(&stats);
  code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);

  put_raw("result=");
  put_raw(result);
  put_raw(" command=armwrestler-jit-test test=");
  put_raw(suite);
  put_u32_dec(test_id);
  put_raw(" expected_results=");
  put_u32_dec(expected_results);
  put_raw(" observed_results=");
  put_u32_dec(result_count);
  put_raw(" failure_mask=");
  put_u32_hex(result_mask);
  put_raw(" last_label=");
  put_u32_hex(result_word(8));
  put_raw(" last_mask=");
  put_u32_hex(result_word(12));
  put_raw(" last_type=");
  put_u32_dec(result_word(16));
  put_raw(" blocks_emitted=");
  put_u32_dec(stats.blocks_emitted);
  put_raw(" native_blocks=");
  put_u32_dec(stats.blocks_executed);
  put_raw(" code_bytes=");
  put_u32_dec(code_bytes);
  put_raw(" fallbacks=");
  put_u32_dec(stats.interpreter_fallbacks);
  put_raw(" fallback_events=");
  put_u32_dec(g_fallback_count);
  put_raw(" execute_arm_calls=");
  put_u32_dec(g_execute_arm_calls);
  put_raw(" native_data_proc=");
  put_u32_dec(stats.native_data_proc_insns);
  put_raw(" native_branch=");
  put_u32_dec(stats.native_branch_insns);
  put_raw(" native_load=");
  put_u32_dec(stats.native_load_insns);
  put_raw(" native_store=");
  put_u32_dec(stats.native_store_insns);
  put_raw(" native_psr=");
  put_u32_dec(stats.native_psr_insns);
  put_raw(" trace_count=");
  put_u32_dec(g_trace_count);
  put_raw(" trace_first=");
  put_u32_hex(g_trace_first_pc);
  put_raw(" trace_last=");
  put_u32_hex(g_trace_last_pc);
  put_raw(" trace_hash=");
  put_u32_hex(g_trace_hash);
  put_raw(" fallback_first=");
  put_u32_hex(g_fallback_first_pc);
  put_raw(" fallback_last=");
  put_u32_hex(g_fallback_last_pc);
  put_raw(" fallback_hash=");
  put_u32_hex(g_fallback_hash);
  put_raw(" update_calls=");
  put_u32_dec(g_update_calls);
  put_raw(" harness_mode=armwrestler_frontend_jit_only reason=");
  put_raw(reason);
  put_raw("\n");
}

static void print_aggregate_summary(const char *result, const char *reason)
{
  riscv_runtime_stats stats;
  u32 code_bytes;

  riscv_get_runtime_stats(&stats);
  code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);

  put_raw("result=");
  put_raw(result);
  put_raw(" command=armwrestler test=all expected_results=");
  put_u32_dec(ARMWRESTLER_TOTAL_RESULTS);
  put_raw(" observed_results=");
  put_u32_dec(g_total_observed_results);
  put_raw(" failure_mask=");
  put_u32_hex(g_total_failure_mask);
  put_raw(" tests=");
  put_u32_dec(ARMWRESTLER_TOTAL_TESTS);
  put_raw(" blocks_emitted=");
  put_u32_dec(stats.blocks_emitted);
  put_raw(" native_blocks=");
  put_u32_dec(stats.blocks_executed);
  put_raw(" code_bytes=");
  put_u32_dec(code_bytes);
  put_raw(" fallbacks=");
  put_u32_dec(stats.interpreter_fallbacks);
  put_raw(" fallback_events=");
  put_u32_dec(g_fallback_count);
  put_raw(" execute_arm_calls=");
  put_u32_dec(g_execute_arm_calls);
  put_raw(" native_data_proc=");
  put_u32_dec(stats.native_data_proc_insns);
  put_raw(" native_branch=");
  put_u32_dec(stats.native_branch_insns);
  put_raw(" native_load=");
  put_u32_dec(stats.native_load_insns);
  put_raw(" native_store=");
  put_u32_dec(stats.native_store_insns);
  put_raw(" native_psr=");
  put_u32_dec(stats.native_psr_insns);
  put_raw(" trace_count=");
  put_u32_dec(g_trace_count);
  put_raw(" trace_first=");
  put_u32_hex(g_trace_first_pc);
  put_raw(" trace_last=");
  put_u32_hex(g_trace_last_pc);
  put_raw(" trace_hash=");
  put_u32_hex(g_trace_hash);
  put_raw(" fallback_first=");
  put_u32_hex(g_fallback_first_pc);
  put_raw(" fallback_last=");
  put_u32_hex(g_fallback_last_pc);
  put_raw(" fallback_hash=");
  put_u32_hex(g_fallback_hash);
  put_raw(" update_calls=");
  put_u32_dec(g_update_calls);
  put_raw(" harness_mode=armwrestler_frontend_jit_only reason=");
  put_raw(reason);
  put_raw("\n");
}

static int run_armwrestler_test(u32 test_id, u32 expected_results)
{
  riscv_runtime_stats before;
  riscv_runtime_stats after;
  u32 before_execute_arm_calls;
  u32 before_fallback_count;
  u32 before_code_bytes;
  u32 after_code_bytes;
  u32 i;

  patch_test_id(test_id);
  clear_runtime_memory();
  store32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + 20u,
          expected_results);
  init_memory_map();
  init_cpu_state();
  reset_dynarec_for_armwrestler();

  riscv_get_runtime_stats(&before);
  before_execute_arm_calls = g_execute_arm_calls;
  before_fallback_count = g_fallback_count;
  before_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);

  for (i = 0; i < RUN_CHUNKS; i++)
  {
    execute_arm_translate_internal(RUN_CYCLES, &reg[0]);
    if (result_word(0) >= expected_results)
      break;
    if (g_execute_arm_calls || g_fallback_count)
      break;
  }

  riscv_get_runtime_stats(&after);
  after_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);
  g_total_observed_results += result_word(0);
  g_total_failure_mask |= result_word(4);

  if (result_word(0) < expected_results)
  {
    print_summary("FAIL", "arm", test_id, expected_results,
                  "result_timeout");
    return 0;
  }
  if (result_word(4) != 0)
  {
    print_summary("FAIL", "arm", test_id, expected_results,
                  "armwrestler_reported_bad");
    return 0;
  }
  if (g_execute_arm_calls != before_execute_arm_calls ||
      g_fallback_count != before_fallback_count ||
      after.interpreter_fallbacks != before.interpreter_fallbacks)
  {
    print_summary("FAIL", "arm", test_id, expected_results,
                  "interpreter_fallback_used");
    return 0;
  }
  if (after.blocks_executed == before.blocks_executed ||
      runtime_native_counter_sum(&after) == runtime_native_counter_sum(&before) ||
      after_code_bytes <= before_code_bytes ||
      g_trace_first_pc < ROM_BASE || g_trace_first_pc >= 0x0e000000u)
  {
    print_summary("FAIL", "arm", test_id, expected_results,
                  "missing_native_execution_evidence");
    return 0;
  }

  print_summary("PASS", "arm", test_id, expected_results,
                "armwrestler_arm_test_jit_only_passed");
  return 1;
}

static int run_thumbwrestler_test(u32 test_id, u32 expected_results)
{
  riscv_runtime_stats before;
  riscv_runtime_stats after;
  u32 before_execute_arm_calls;
  u32 before_fallback_count;
  u32 before_code_bytes;
  u32 after_code_bytes;
  u32 i;

  clear_runtime_memory();
  store32(iwram + 0x8000u, 0x08u, test_id);
  store32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + 20u,
          expected_results);
  init_memory_map();
  init_thumbwrestler_cpu_state();
  reset_dynarec_for_armwrestler();

  riscv_get_runtime_stats(&before);
  before_execute_arm_calls = g_execute_arm_calls;
  before_fallback_count = g_fallback_count;
  before_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);

  for (i = 0; i < RUN_CHUNKS; i++)
  {
    execute_arm_translate_internal(RUN_CYCLES, &reg[0]);
    if (result_word(0) >= expected_results)
      break;
    if (g_execute_arm_calls || g_fallback_count)
      break;
  }

  riscv_get_runtime_stats(&after);
  after_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);
  g_total_observed_results += result_word(0);
  g_total_failure_mask |= result_word(4);

  if (result_word(0) < expected_results)
  {
    print_summary("FAIL", "thumb", test_id, expected_results,
                  "result_timeout");
    return 0;
  }
  if (result_word(4) != 0)
  {
    print_summary("FAIL", "thumb", test_id, expected_results,
                  "thumbwrestler_reported_bad");
    return 0;
  }
  if (g_execute_arm_calls != before_execute_arm_calls ||
      g_fallback_count != before_fallback_count ||
      after.interpreter_fallbacks != before.interpreter_fallbacks)
  {
    print_summary("FAIL", "thumb", test_id, expected_results,
                  "interpreter_fallback_used");
    return 0;
  }
  if (after.blocks_executed == before.blocks_executed ||
      runtime_native_counter_sum(&after) == runtime_native_counter_sum(&before) ||
      after_code_bytes <= before_code_bytes ||
      g_trace_first_pc < ROM_BASE || g_trace_first_pc >= 0x0e000000u)
  {
    print_summary("FAIL", "thumb", test_id, expected_results,
                  "missing_native_execution_evidence");
    return 0;
  }

  print_summary("PASS", "thumb", test_id, expected_results,
                "thumbwrestler_test_jit_only_passed");
  return 1;
}

void _start(void)
{
  u32 i;

  if (!load_rom())
  {
    put_raw("result=FAIL command=armwrestler reason=rom_load_failed path="
            ARMWRESTLER_ROM "\n");
    sys_exit(2);
  }

  patch_loaded_rom();
  init_dynarec_for_armwrestler();

  for (i = 0; i < ARMWRESTLER_ARM_TESTS; i++)
  {
    if (!run_armwrestler_test(g_arm_test_ids[i], g_arm_test_results[i]))
    {
      print_aggregate_summary("FAIL", "armwrestler_arm_all_jit_only_failed");
      sys_exit(1);
    }
  }
  for (i = 0; i < ARMWRESTLER_THUMB_TESTS; i++)
  {
    if (!run_thumbwrestler_test(g_thumb_test_ids[i], g_thumb_test_results[i]))
    {
      print_aggregate_summary("FAIL", "armwrestler_all_jit_only_failed");
      sys_exit(1);
    }
  }

  if (g_total_observed_results != ARMWRESTLER_TOTAL_RESULTS ||
      g_total_failure_mask != 0)
  {
    print_aggregate_summary("FAIL", "armwrestler_all_jit_only_mismatch");
    sys_exit(1);
  }

  print_aggregate_summary("PASS", "armwrestler_all_jit_only_passed");
  sys_exit(0);
}
