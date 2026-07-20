#include "rv32im_frontend_shim.h"
#include "riscv_emit.h"
#if defined(RV32IM_FRONTEND_CONTROL_ONLY)
#include "rv32im_frontend_control_cases.h"
#endif
#if defined(RV32IM_BIOS_SWI_ONLY)
#include "rv32im_bios_swi_cases.h"
#endif

typedef unsigned int usize;

#if defined(RV32IM_FRONTEND_CONTROL_ONLY)
static int frontend_control_name_equal(const char *left, const char *right)
{
  while (*left && *left == *right)
  {
    left++;
    right++;
  }
  return *left == *right;
}
#endif

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

#ifndef OPEN_GBA_BIOS
#define OPEN_GBA_BIOS "/home/john/work/gpsp/bios/open_gba_bios.bin"
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
#define ARMWRESTLER_TOP_BLOCKS 8u

#if defined(RV32IM_FRONTEND_CACHE_ONLY)
#define FRONTEND_CACHE_PC 0x08010000u
#define FRONTEND_CACHE_BLOCK_WORDS 1024u
#define FRONTEND_CACHE_BLOCK_BYTES (FRONTEND_CACHE_BLOCK_WORDS * 4u)
#define FRONTEND_CACHE_BLOCKS 96u
#define FRONTEND_CACHE_MIN_SPAN_BYTES (1024u * 1024u)
#define FRONTEND_CACHE_SOURCE_PC \
  (FRONTEND_CACHE_PC + FRONTEND_CACHE_BLOCKS * FRONTEND_CACHE_BLOCK_BYTES)
#define FRONTEND_CACHE_CHAIN_CYCLES \
  (6u + FRONTEND_CACHE_BLOCK_WORDS * 6u)
#endif

#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
#if !defined(ARMWRESTLER_PERF_DISABLE_MAPPED_ALU_FASTPATH) || \
    !defined(ARMWRESTLER_PERF_DISABLE_FAST_RAM_READS) || \
    !defined(ARMWRESTLER_PERF_DISABLE_STATE_HELPER_OPT) || \
    !defined(ARMWRESTLER_PERF_DISABLE_VALIDATED_ENTRY_OPT)
#error "Armwrestler perf builds must select each optimization independently"
#endif
#if (defined(ARMWRESTLER_PERF_FAST_STORE_AB) + \
     defined(ARMWRESTLER_PERF_ENTRY_SETUP_AB) + \
     defined(ARMWRESTLER_PERF_STATE_HELPER_AB) + \
     defined(ARMWRESTLER_PERF_VALIDATED_ENTRY_AB) + \
     defined(ARMWRESTLER_PERF_INDIRECT_LOOKUP_AB)) > 1
#error "Armwrestler perf builds must isolate exactly one optimization"
#endif
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_mapped_alu_fastpath =
  ARMWRESTLER_PERF_DISABLE_MAPPED_ALU_FASTPATH;
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_fast_ram_reads =
  ARMWRESTLER_PERF_DISABLE_FAST_RAM_READS;
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_state_helper_opt =
  ARMWRESTLER_PERF_DISABLE_STATE_HELPER_OPT;
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_validated_entry_opt =
  ARMWRESTLER_PERF_DISABLE_VALIDATED_ENTRY_OPT;
#if defined(RISCV_RUNTIME_ENABLE_FAST_RAM_STORES)
#if !defined(ARMWRESTLER_PERF_DISABLE_FAST_RAM_STORES)
#error "Perf builds with fast stores must select the store optimization"
#endif
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_fast_ram_stores =
  ARMWRESTLER_PERF_DISABLE_FAST_RAM_STORES;
#endif
#if defined(ARMWRESTLER_PERF_INDIRECT_LOOKUP_AB)
#if !defined(RISCV_RUNTIME_INDIRECT_LOOKUP_PROFILE_SWITCH)
#error "Indirect-lookup A/B builds must enable the runtime selector"
#endif
#if !defined(ARMWRESTLER_PERF_DISABLE_INDIRECT_LOOKUP_CACHE) || \
    !defined(ARMWRESTLER_PERF_DISABLE_ENTRY_SETUP_OPT)
#error "Indirect-lookup A/B builds must select and pin the profile"
#endif
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_indirect_lookup_cache =
  ARMWRESTLER_PERF_DISABLE_INDIRECT_LOOKUP_CACHE;
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_entry_setup_opt =
  ARMWRESTLER_PERF_DISABLE_ENTRY_SETUP_OPT;
#define ARMWRESTLER_JIT_PROFILE "indirect_lookup_cache_ab"
#elif defined(ARMWRESTLER_PERF_VALIDATED_ENTRY_AB)
#if !defined(RISCV_RUNTIME_VALIDATED_ENTRY_PROFILE_SWITCH)
#error "Validated-entry A/B builds must enable the runtime selector"
#endif
#if !defined(ARMWRESTLER_PERF_DISABLE_ENTRY_SETUP_OPT)
#error "Validated-entry A/B builds must pin the entry-setup optimization"
#endif
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_entry_setup_opt =
  ARMWRESTLER_PERF_DISABLE_ENTRY_SETUP_OPT;
#define ARMWRESTLER_JIT_PROFILE "validated_entry_ab"
#elif defined(ARMWRESTLER_PERF_STATE_HELPER_AB)
#if !defined(ARMWRESTLER_PERF_DISABLE_ENTRY_SETUP_OPT)
#error "State-helper A/B builds must pin the entry-setup optimization"
#endif
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_entry_setup_opt =
  ARMWRESTLER_PERF_DISABLE_ENTRY_SETUP_OPT;
#define ARMWRESTLER_JIT_PROFILE "state_helper_ab"
#elif defined(ARMWRESTLER_PERF_ENTRY_SETUP_AB)
#if !defined(ARMWRESTLER_PERF_DISABLE_ENTRY_SETUP_OPT)
#error "Entry-setup A/B builds must select the entry optimization"
#endif
__attribute__((section(".data")))
volatile u32 riscv_runtime_perf_disable_entry_setup_opt =
  ARMWRESTLER_PERF_DISABLE_ENTRY_SETUP_OPT;
#define ARMWRESTLER_JIT_PROFILE "entry_setup_ab"
#elif defined(ARMWRESTLER_PERF_FAST_STORE_AB)
#if !defined(RISCV_RUNTIME_ENABLE_FAST_RAM_STORES)
#error "Fast-store A/B builds must enable the store optimization"
#endif
#define ARMWRESTLER_JIT_PROFILE "fast_ram_stores_ab"
#else
#define ARMWRESTLER_JIT_PROFILE "fast_ram_reads_ab"
#endif
#elif defined(RISCV_RUNTIME_DISABLE_MAPPED_ALU_FASTPATH)
#define ARMWRESTLER_JIT_PROFILE "mapped_alu_baseline"
#else
#define ARMWRESTLER_JIT_PROFILE "optimized"
#endif

typedef struct
{
  u32 start_pc;
  u32 end_pc;
  u32 thumb;
  u32 code_bytes;
} armwrestler_block_hotspot;

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
u8 ewram[1024 * 256 * 2] __attribute__((aligned(4)));
u8 iwram[1024 * 32 * 2] __attribute__((aligned(4)));
u16 io_registers[512];
u32 cheat_master_hook = 0xffffffffu;
u32 flush_ram_count;
void *stdout;

static u8 g_rom[ROM_MAX_BYTES];
static u8 g_rom_open_bus[ROM_PAGE_BYTES];
static u8 g_bios[0x4000u];
#if defined(RV32IM_BIOS_SWI_ONLY)
static u8 g_open_bios[0x4000u];
#endif
static u8 g_vram[1024 * 96];
static u8 g_palette_ram[1024];
static u8 g_oam_ram[1024];
static u32 g_rom_size;
static u32 g_execute_arm_calls;
static u32 g_execute_arm_cycles;
static u32 g_update_calls;
static s32 g_update_last_cycles;
#if defined(RV32IM_FRONTEND_CONTROL_ONLY)
static u32 g_frontend_control_resume_updates;
static u32 g_frontend_control_resume_cycles;
static u32 g_frontend_control_raise_irq;
#endif
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
static u32 g_arm_code_bytes_total;
static u32 g_thumb_code_bytes_total;
static u32 g_last_warm_replays;
static u32 g_last_warm_observed_results;
static u32 g_last_warm_failure_mask;
static u32 g_last_warm_code_bytes_added;
static u32 g_last_cold_native_blocks;
static u32 g_last_warm_native_blocks;
static u32 g_warm_replays_total;
static u32 g_warm_code_bytes_added_total;
static u32 g_cold_native_blocks_total;
static u32 g_warm_native_blocks_total;
static armwrestler_block_hotspot g_block_hotspots[ARMWRESTLER_TOP_BLOCKS];
static u32 g_block_hotspot_count;

/* Out-of-line markers delimit only the real frontend/JIT execution loop.
 * QEMU trace analysis counts the RV32 instructions represented by executed
 * translation blocks between these markers; it does not call the result an
 * architectural retired-instruction count. */
__attribute__((noinline))
void rv32im_armwrestler_measure_begin(void)
{
  __asm__ volatile("addi zero, zero, 21" ::: "memory");
}

__attribute__((noinline))
void rv32im_armwrestler_measure_end(void)
{
  __asm__ volatile("addi zero, zero, 23" ::: "memory");
}

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

#if defined(RV32IM_BIOS_JIT_AUDIT_ONLY) || defined(RV32IM_BIOS_SWI_ONLY)
static int load_open_gba_bios(u8 *destination)
{
  int fd = (int)syscall3(SYS_OPENAT, AT_FDCWD, (long)OPEN_GBA_BIOS,
                         O_RDONLY);
  u32 total = 0;
  u8 extra;
  long got;

  if (fd < 0)
    return 0;

  while (total < sizeof(g_bios))
  {
    got = syscall3(SYS_READ, fd, (long)&destination[total],
                   sizeof(g_bios) - total);
    if (got <= 0)
      break;
    total += (u32)got;
  }
  got = syscall3(SYS_READ, fd, (long)&extra, 1);
  syscall1(SYS_CLOSE, fd);
  return total == sizeof(g_bios) && got == 0;
}
#endif

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

  map_read_region(0x00000000u, 0x00004000u, g_bios, 0x3fffu);
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
    case 0x00:
      return g_bios + (address & 0x3fffu);
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
  if (address & 1u)
    return (u32)((s8)read_memory8(address));
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
  g_update_calls++;
  g_update_last_cycles = (s32)remaining_cycles;
#if defined(RV32IM_FRONTEND_CONTROL_ONLY)
  if (g_frontend_control_raise_irq != 0u && g_update_calls == 1u)
  {
    REG_MODE(MODE_IRQ)[6] = reg[REG_PC] + 4u;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = 0xd2u;
    reg[REG_PC] = 0x00000018u;
    set_cpu_mode(MODE_IRQ);
  }
  if (g_frontend_control_resume_updates != 0u)
  {
    g_frontend_control_resume_updates--;
    return g_frontend_control_resume_cycles;
  }
#endif
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

static int block_hotspot_before(const armwrestler_block_hotspot *left,
                                const armwrestler_block_hotspot *right)
{
  if (left->code_bytes != right->code_bytes)
    return left->code_bytes > right->code_bytes;
  if (left->start_pc != right->start_pc)
    return left->start_pc < right->start_pc;
  if (left->end_pc != right->end_pc)
    return left->end_pc < right->end_pc;
  return left->thumb < right->thumb;
}

static void reset_block_hotspots(void)
{
  g_block_hotspot_count = 0;
}

void riscv_note_runtime_block_emit(u32 start_pc, u32 end_pc, u32 thumb,
                                   u32 code_bytes)
{
  armwrestler_block_hotspot item;
  u32 pos = 0;
  u32 i;

  if (code_bytes == 0)
    return;

  item.start_pc = start_pc;
  item.end_pc = end_pc;
  item.thumb = thumb;
  item.code_bytes = code_bytes;

  while (pos < g_block_hotspot_count &&
         !block_hotspot_before(&item, &g_block_hotspots[pos]))
  {
    pos++;
  }

  if (pos >= ARMWRESTLER_TOP_BLOCKS)
    return;

  if (g_block_hotspot_count < ARMWRESTLER_TOP_BLOCKS)
    g_block_hotspot_count++;

  for (i = g_block_hotspot_count - 1u; i > pos; i--)
    g_block_hotspots[i] = g_block_hotspots[i - 1u];

  g_block_hotspots[pos] = item;
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

  /* REG_USERDEF..REG_MAX is backend-owned helper state, matching cpu.cc. */
  for (i = 0; i < REG_USERDEF; i++)
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
  reg_mode[MODE_USER & 0xfu][5] = 0x03007f00u;
  reg_mode[MODE_IRQ & 0xfu][5] = 0x03007fa0u;
  reg_mode[MODE_FIQ & 0xfu][5] = 0x03007fa0u;
  reg_mode[MODE_SUPERVISOR & 0xfu][5] = 0x03007fe0u;
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

static void run_frontend_until_results(u32 expected_results)
{
  u32 i;

  for (i = 0; i < RUN_CHUNKS; i++)
  {
    execute_arm_translate_internal(RUN_CYCLES, &reg[0]);
    if (result_word(0) >= expected_results)
      break;
    if (g_execute_arm_calls || g_fallback_count)
      break;
  }
}

static void print_block_hotspots(void)
{
  u32 i;

  put_raw(" largest_block_pc=");
  put_u32_hex(g_block_hotspot_count ? g_block_hotspots[0].start_pc : 0);
  put_raw(" largest_block_end=");
  put_u32_hex(g_block_hotspot_count ? g_block_hotspots[0].end_pc : 0);
  put_raw(" largest_block_thumb=");
  put_u32_dec(g_block_hotspot_count ? g_block_hotspots[0].thumb : 0);
  put_raw(" largest_block_bytes=");
  put_u32_dec(g_block_hotspot_count ? g_block_hotspots[0].code_bytes : 0);
  put_raw(" block_hotspots=");

  if (!g_block_hotspot_count)
  {
    put_raw("none");
    return;
  }

  for (i = 0; i < g_block_hotspot_count; i++)
  {
    if (i)
      put_raw(",");
    put_u32_hex(g_block_hotspots[i].start_pc);
    put_raw(":");
    put_u32_hex(g_block_hotspots[i].end_pc);
    put_raw(":");
    put_u32_dec(g_block_hotspots[i].thumb);
    put_raw(":");
    put_u32_dec(g_block_hotspots[i].code_bytes);
  }
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
  print_block_hotspots();
  put_raw(" arm_code_bytes_total=");
  put_u32_dec(g_arm_code_bytes_total);
  put_raw(" thumb_code_bytes_total=");
  put_u32_dec(g_thumb_code_bytes_total);
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
  put_raw(" thumb_helpers=");
  put_u32_dec(stats.thumb_helper_insns);
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
  put_raw(" warm_replays=");
  put_u32_dec(g_last_warm_replays);
  put_raw(" warm_observed_results=");
  put_u32_dec(g_last_warm_observed_results);
  put_raw(" warm_failure_mask=");
  put_u32_hex(g_last_warm_failure_mask);
  put_raw(" warm_code_bytes_added=");
  put_u32_dec(g_last_warm_code_bytes_added);
  put_raw(" cold_native_blocks=");
  put_u32_dec(g_last_cold_native_blocks);
  put_raw(" warm_native_blocks=");
  put_u32_dec(g_last_warm_native_blocks);
  put_raw(" jit_profile=");
  put_raw(ARMWRESTLER_JIT_PROFILE);
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  put_raw(" mapped_alu_fastpath_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_mapped_alu_fastpath);
  put_raw(" fast_ram_reads_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_fast_ram_reads);
  put_raw(" state_helpers_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_state_helper_opt);
  put_raw(" validated_entry_optimized=");
  put_u32_dec(!riscv_runtime_perf_disable_validated_entry_opt);
#if defined(RISCV_RUNTIME_ENABLE_FAST_RAM_STORES)
  put_raw(" fast_ram_stores_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_fast_ram_stores);
#endif
#if defined(ARMWRESTLER_PERF_ENTRY_SETUP_AB) || \
    defined(ARMWRESTLER_PERF_VALIDATED_ENTRY_AB) || \
    defined(ARMWRESTLER_PERF_STATE_HELPER_AB) || \
    defined(ARMWRESTLER_PERF_INDIRECT_LOOKUP_AB)
  put_raw(" entry_setup_optimized=");
  put_u32_dec(!riscv_runtime_perf_disable_entry_setup_opt);
#endif
#if defined(ARMWRESTLER_PERF_INDIRECT_LOOKUP_AB)
  put_raw(" indirect_lookup_cache_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_indirect_lookup_cache);
#endif
#endif
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
  put_raw(" arm_code_bytes_total=");
  put_u32_dec(g_arm_code_bytes_total);
  put_raw(" thumb_code_bytes_total=");
  put_u32_dec(g_thumb_code_bytes_total);
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
  put_raw(" thumb_helpers=");
  put_u32_dec(stats.thumb_helper_insns);
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
  put_raw(" warm_replays=");
  put_u32_dec(g_warm_replays_total);
  put_raw(" warm_code_bytes_added=");
  put_u32_dec(g_warm_code_bytes_added_total);
  put_raw(" cold_native_blocks=");
  put_u32_dec(g_cold_native_blocks_total);
  put_raw(" warm_native_blocks=");
  put_u32_dec(g_warm_native_blocks_total);
  put_raw(" jit_profile=");
  put_raw(ARMWRESTLER_JIT_PROFILE);
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  put_raw(" mapped_alu_fastpath_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_mapped_alu_fastpath);
  put_raw(" fast_ram_reads_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_fast_ram_reads);
  put_raw(" state_helpers_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_state_helper_opt);
  put_raw(" validated_entry_optimized=");
  put_u32_dec(!riscv_runtime_perf_disable_validated_entry_opt);
#if defined(RISCV_RUNTIME_ENABLE_FAST_RAM_STORES)
  put_raw(" fast_ram_stores_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_fast_ram_stores);
#endif
#if defined(ARMWRESTLER_PERF_ENTRY_SETUP_AB) || \
    defined(ARMWRESTLER_PERF_VALIDATED_ENTRY_AB) || \
    defined(ARMWRESTLER_PERF_STATE_HELPER_AB) || \
    defined(ARMWRESTLER_PERF_INDIRECT_LOOKUP_AB)
  put_raw(" entry_setup_optimized=");
  put_u32_dec(!riscv_runtime_perf_disable_entry_setup_opt);
#endif
#if defined(ARMWRESTLER_PERF_INDIRECT_LOOKUP_AB)
  put_raw(" indirect_lookup_cache_enabled=");
  put_u32_dec(!riscv_runtime_perf_disable_indirect_lookup_cache);
#endif
#endif
  put_raw(" harness_mode=armwrestler_frontend_jit_only reason=");
  put_raw(reason);
  put_raw("\n");
}

static int run_armwrestler_test(u32 test_id, u32 expected_results)
{
  riscv_runtime_stats before;
  riscv_runtime_stats after;
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  riscv_runtime_stats after_cold;
#endif
  u32 before_execute_arm_calls;
  u32 before_fallback_count;
  u32 before_code_bytes;
  u32 after_code_bytes;

  patch_test_id(test_id);
  clear_runtime_memory();
  store32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + 20u,
          expected_results);
  init_memory_map();
  init_cpu_state();
  reset_dynarec_for_armwrestler();
  reset_block_hotspots();
  g_last_warm_replays = 0;
  g_last_warm_observed_results = 0;
  g_last_warm_failure_mask = 0;
  g_last_warm_code_bytes_added = 0;
  g_last_cold_native_blocks = 0;
  g_last_warm_native_blocks = 0;

  riscv_get_runtime_stats(&before);
  before_execute_arm_calls = g_execute_arm_calls;
  before_fallback_count = g_fallback_count;
  before_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);

  rv32im_armwrestler_measure_begin();
  run_frontend_until_results(expected_results);
  rv32im_armwrestler_measure_end();

  riscv_get_runtime_stats(&after);
  g_last_cold_native_blocks =
    after.blocks_executed - before.blocks_executed;
  g_cold_native_blocks_total += g_last_cold_native_blocks;
  after_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);
  g_arm_code_bytes_total += after_code_bytes;
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

#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  after_cold = after;
  clear_runtime_memory();
  store32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + 20u,
          expected_results);
  init_memory_map();
  init_cpu_state();

  rv32im_armwrestler_measure_begin();
  run_frontend_until_results(expected_results);
  rv32im_armwrestler_measure_end();

  riscv_get_runtime_stats(&after);
  g_last_warm_native_blocks =
    after.blocks_executed - after_cold.blocks_executed;
  g_warm_native_blocks_total += g_last_warm_native_blocks;
  g_last_warm_replays = 1;
  g_last_warm_observed_results = result_word(0);
  g_last_warm_failure_mask = result_word(4);
  g_last_warm_code_bytes_added =
      (u32)(rom_translation_ptr - rom_translation_cache) - after_code_bytes;
  g_warm_replays_total++;
  g_warm_code_bytes_added_total += g_last_warm_code_bytes_added;

  if (g_last_warm_observed_results != expected_results ||
      g_last_warm_failure_mask != 0 ||
      g_last_warm_code_bytes_added != 0 ||
      after.blocks_executed == after_cold.blocks_executed ||
      g_execute_arm_calls != before_execute_arm_calls ||
      g_fallback_count != before_fallback_count ||
      after.interpreter_fallbacks != before.interpreter_fallbacks)
  {
    print_summary("FAIL", "arm", test_id, expected_results,
                  "warm_replay_mismatch");
    return 0;
  }
#endif

  print_summary("PASS", "arm", test_id, expected_results,
                "armwrestler_arm_test_jit_only_passed");
  return 1;
}

static int run_thumbwrestler_test(u32 test_id, u32 expected_results)
{
  riscv_runtime_stats before;
  riscv_runtime_stats after;
#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  riscv_runtime_stats after_cold;
#endif
  u32 before_execute_arm_calls;
  u32 before_fallback_count;
  u32 before_code_bytes;
  u32 after_code_bytes;

  clear_runtime_memory();
  store32(iwram + 0x8000u, 0x08u, test_id);
  store32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + 20u,
          expected_results);
  init_memory_map();
  init_thumbwrestler_cpu_state();
  reset_dynarec_for_armwrestler();
  reset_block_hotspots();
  g_last_warm_replays = 0;
  g_last_warm_observed_results = 0;
  g_last_warm_failure_mask = 0;
  g_last_warm_code_bytes_added = 0;
  g_last_cold_native_blocks = 0;
  g_last_warm_native_blocks = 0;

  riscv_get_runtime_stats(&before);
  before_execute_arm_calls = g_execute_arm_calls;
  before_fallback_count = g_fallback_count;
  before_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);

  rv32im_armwrestler_measure_begin();
  run_frontend_until_results(expected_results);
  rv32im_armwrestler_measure_end();

  riscv_get_runtime_stats(&after);
  g_last_cold_native_blocks =
    after.blocks_executed - before.blocks_executed;
  g_cold_native_blocks_total += g_last_cold_native_blocks;
  after_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);
  g_thumb_code_bytes_total += after_code_bytes;
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

#if defined(RISCV_RUNTIME_PERF_PROFILE_SWITCH)
  after_cold = after;
  clear_runtime_memory();
  store32(iwram + 0x8000u, 0x08u, test_id);
  store32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + 20u,
          expected_results);
  init_memory_map();
  init_thumbwrestler_cpu_state();

  rv32im_armwrestler_measure_begin();
  run_frontend_until_results(expected_results);
  rv32im_armwrestler_measure_end();

  riscv_get_runtime_stats(&after);
  g_last_warm_native_blocks =
    after.blocks_executed - after_cold.blocks_executed;
  g_warm_native_blocks_total += g_last_warm_native_blocks;
  g_last_warm_replays = 1;
  g_last_warm_observed_results = result_word(0);
  g_last_warm_failure_mask = result_word(4);
  g_last_warm_code_bytes_added =
      (u32)(rom_translation_ptr - rom_translation_cache) - after_code_bytes;
  g_warm_replays_total++;
  g_warm_code_bytes_added_total += g_last_warm_code_bytes_added;

  if (g_last_warm_observed_results != expected_results ||
      g_last_warm_failure_mask != 0 ||
      g_last_warm_code_bytes_added != 0 ||
      after.blocks_executed == after_cold.blocks_executed ||
      g_execute_arm_calls != before_execute_arm_calls ||
      g_fallback_count != before_fallback_count ||
      after.interpreter_fallbacks != before.interpreter_fallbacks)
  {
    print_summary("FAIL", "thumb", test_id, expected_results,
                  "warm_replay_mismatch");
    return 0;
  }
#endif

  print_summary("PASS", "thumb", test_id, expected_results,
                "thumbwrestler_test_jit_only_passed");
  return 1;
}

#if defined(RV32IM_FRONTEND_CONTROL_ONLY)
extern void rv32im_frontend_control_set_update_slot(u32 index, u32 value);
extern u32 rv32im_frontend_control_get_update_slot(u32 index);

#if defined(RV32IM_FRONTEND_CONTROL_FORCE_DISPATCH)
#define FRONTEND_CONTROL_BACKEND "rv32im-dispatch"
#else
#define FRONTEND_CONTROL_BACKEND "rv32im"
#endif

static void install_frontend_control_case(
  const rv32im_frontend_control_case *item)
{
  u32 i;

  memset(g_rom, 0, sizeof(g_rom));
  memset(g_bios, 0, sizeof(g_bios));
  g_rom_size = sizeof(g_rom);
  store32(g_rom, 8u, 0xeafffffeu);
  for (i = 0; i < item->generated_words; i++)
  {
    store32(g_rom, RV32IM_FRONTEND_CONTROL_PC - ROM_BASE + i * 4u,
            0x12800001u);
  }
  for (i = 0; i < item->word_count; i++)
  {
    store32(g_rom, RV32IM_FRONTEND_CONTROL_PC - ROM_BASE + i * 4u,
            item->words[i]);
  }
  if (item->resume_updates != 0u)
  {
    for (i = 0;
         i < sizeof(rv32im_frontend_control_swi_dispatcher) /
               sizeof(rv32im_frontend_control_swi_dispatcher[0]);
         i++)
    {
      store32(g_bios, rv32im_frontend_control_swi_dispatcher[i].offset,
              rv32im_frontend_control_swi_dispatcher[i].word);
    }
    for (i = 0;
         i < sizeof(rv32im_frontend_control_cpuset_words) /
               sizeof(rv32im_frontend_control_cpuset_words[0]);
         i++)
      store32(g_bios, 0x614u + i * 4u,
              rv32im_frontend_control_cpuset_words[i]);
    for (i = 0;
         i < sizeof(rv32im_frontend_control_cpufastset_words) /
               sizeof(rv32im_frontend_control_cpufastset_words[0]);
         i++)
      store32(g_bios, 0x720u + i * 4u,
              rv32im_frontend_control_cpufastset_words[i]);
    for (i = 0;
         i < sizeof(rv32im_frontend_control_bgaffineset_words) /
               sizeof(rv32im_frontend_control_bgaffineset_words[0]);
         i++)
      store32(g_bios, 0x7e4u + i * 4u,
              rv32im_frontend_control_bgaffineset_words[i]);
    for (i = 0;
         i < sizeof(rv32im_frontend_control_objaffineset_words) /
               sizeof(rv32im_frontend_control_objaffineset_words[0]);
         i++)
      store32(g_bios, 0x8e0u + i * 4u,
              rv32im_frontend_control_objaffineset_words[i]);
    store16(g_bios, 0x2150u, 0x1234u);
    store16(g_bios, 0x21d0u, 0x5678u);
    store32(g_rom, RV32IM_FRONTEND_CONTROL_PC - ROM_BASE + 0x40u,
            0xe12fff1eu);
  }
}

static void reset_frontend_control_observations(void)
{
  g_execute_arm_calls = 0;
  g_execute_arm_cycles = 0;
  g_update_calls = 0;
  g_update_last_cycles = (s32)0x7fffffffu;
  g_trace_count = 0;
  g_trace_first_pc = 0;
  g_trace_last_pc = 0;
  g_trace_hash = 2166136261u;
  g_fallback_count = 0;
  g_fallback_first_pc = 0;
  g_fallback_last_pc = 0;
  g_fallback_hash = 2166136261u;
}

static int run_frontend_control_case(
  const rv32im_frontend_control_case *item)
{
  riscv_runtime_stats stats;
  u32 state_hash;
  u32 before_code_bytes;
  u32 code_bytes;
  u32 memory_hash = 2166136261u;
  u32 stale_slot_cleared = 1u;
  int passed;

  install_frontend_control_case(item);
  clear_runtime_memory();
  if (frontend_control_name_equal(
        item->name,
        "arm_openbios_cpufastset_large_iwram_fill_resume"))
  {
    store32(iwram + 0x8000u, 0x7df4u, 0xa5a5a5a5u);
  }
  else if (frontend_control_name_equal(
        item->name, "arm_openbios_cpufastset_copy_resume"))
  {
    for (u32 source_word = 0; source_word < 8u; source_word++)
      store32(ewram, source_word * 4u, (source_word + 1u) * 0x11u);
  }
  else if (frontend_control_name_equal(
             item->name, "arm_openbios_bgaffineset_resume"))
  {
    store32(ewram, 0x00u, 0x00012345u);
    store32(ewram, 0x04u, 0xfffedcbbu);
    store16(ewram, 0x08u, 0x0011u);
    store16(ewram, 0x0au, 0xffe2u);
    store16(ewram, 0x0cu, 0x0180u);
    store16(ewram, 0x0eu, 0xff40u);
    store16(ewram, 0x10u, 0x0000u);
  }
  else if (frontend_control_name_equal(
             item->name, "arm_openbios_objaffineset_resume"))
  {
    store16(ewram, 0x00u, 0x0180u);
    store16(ewram, 0x02u, 0xff40u);
    store16(ewram, 0x04u, 0x0000u);
  }
  if (item->resume_updates != 0u)
    store32(iwram + 0x8000u, 0x7ffcu,
            RV32IM_FRONTEND_CONTROL_PC + 0x40u);
  init_memory_map();
  init_cpu_state();
  reg[REG_PC] = RV32IM_FRONTEND_CONTROL_PC + item->entry_word_offset * 4u;
  reg[REG_CPSR] = item->initial_cpsr;
  idle_loop_target_pc = item->idle_pc;
  reset_frontend_control_observations();
  g_frontend_control_resume_updates = item->resume_updates;
  g_frontend_control_resume_cycles = item->resume_cycles;
  g_frontend_control_raise_irq = 0u;
  reset_dynarec_for_armwrestler();
  init_emitter(false);
#if defined(RV32IM_FRONTEND_CONTROL_FORCE_DISPATCH)
  riscv_set_runtime_debug_force_dispatch(true);
#else
  riscv_set_runtime_debug_force_dispatch(false);
#endif
  before_code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);

  if (item->pretranslate_disable_arm_mask != 0u)
  {
    riscv_set_runtime_debug_disable_arm_native_mask(
      item->pretranslate_disable_arm_mask);
    if (!block_lookup_address_arm(RV32IM_FRONTEND_CONTROL_PC))
      stale_slot_cleared = 0u;
    riscv_set_runtime_debug_disable_arm_native_mask(0u);
  }

  if (item->stale_update_index_plus_one != 0u)
  {
    const u32 slot = item->stale_update_index_plus_one - 1u;

    rv32im_frontend_control_set_update_slot(slot, 1u);
    if (!translate_block_thumb(RV32IM_FRONTEND_CONTROL_PC, false))
      stale_slot_cleared = 0u;
    else if (rv32im_frontend_control_get_update_slot(slot) != 0u)
      stale_slot_cleared = 0u;
  }

  execute_arm_translate_internal(item->cycles, &reg[0]);

  riscv_get_runtime_stats(&stats);
  code_bytes = (u32)(rom_translation_ptr - rom_translation_cache) -
               before_code_bytes;
  state_hash = rv32im_frontend_control_state_hash(
    &reg[0], &spsr[0], &reg_mode[0][0], g_update_calls,
    g_update_last_cycles);
  for (u32 memory_offset = 0x1000u; memory_offset < 0x2000u;
       memory_offset += 4u)
    memory_hash = rv32im_frontend_control_hash_word(
      memory_hash, load32(ewram, memory_offset));
  state_hash = rv32im_frontend_control_hash_word(state_hash, memory_hash);
  passed = g_update_calls == item->resume_updates + 1u &&
           g_update_last_cycles != (s32)0x7fffffffu &&
           g_execute_arm_calls == 0u && g_fallback_count == 0u &&
           stats.interpreter_fallbacks == 0u &&
           stats.blocks_executed != 0u && code_bytes != 0u &&
           stale_slot_cleared != 0u &&
           (item->generated_words == 0u || code_bytes > 4096u ||
            frontend_control_name_equal(
              item->name, "arm_known_false_block_tail_cycles")) &&
           g_trace_count != 0u &&
           ((item->resume_updates != 0u && g_trace_first_pc < 0x4000u) ||
            (g_trace_first_pc >= ROM_BASE &&
             g_trace_first_pc < 0x0e000000u));

  put_raw("result=");
  put_raw(passed ? "PASS" : "FAIL");
  put_raw(" command=frontend-control case=");
  put_raw(item->name);
  put_raw(" backend=" FRONTEND_CONTROL_BACKEND " state_hash=");
  put_u32_hex(state_hash);
  put_raw(" r0=");
  put_u32_hex(reg[0]);
  put_raw(" r1=");
  put_u32_hex(reg[1]);
  put_raw(" r2=");
  put_u32_hex(reg[2]);
  put_raw(" r3=");
  put_u32_hex(reg[3]);
  put_raw(" r4=");
  put_u32_hex(reg[4]);
  put_raw(" r5=");
  put_u32_hex(reg[5]);
  put_raw(" r8=");
  put_u32_hex(reg[8]);
  put_raw(" r10=");
  put_u32_hex(reg[10]);
  put_raw(" lr=");
  put_u32_hex(reg[REG_LR]);
  put_raw(" pc=");
  put_u32_hex(reg[REG_PC]);
  put_raw(" cpsr=");
  put_u32_hex(reg[REG_CPSR]);
  put_raw(" svc_spsr=");
  put_u32_hex(REG_SPSR(MODE_SUPERVISOR));
  put_raw(" svc_lr=");
  put_u32_hex(REG_MODE(MODE_SUPERVISOR)[6]);
  put_raw(" irq_spsr=");
  put_u32_hex(REG_SPSR(MODE_IRQ));
  put_raw(" sys_sp=");
  put_u32_hex(REG_MODE(MODE_SYSTEM)[5]);
  put_raw(" sys_lr=");
  put_u32_hex(REG_MODE(MODE_SYSTEM)[6]);
  put_raw(" svc_sp=");
  put_u32_hex(REG_MODE(MODE_SUPERVISOR)[5]);
  put_raw(" irq_sp=");
  put_u32_hex(REG_MODE(MODE_IRQ)[5]);
  put_raw(" irq_lr=");
  put_u32_hex(REG_MODE(MODE_IRQ)[6]);
  put_raw(" memory_hash=");
  put_u32_hex(memory_hash);
  put_raw(" update_calls=");
  put_u32_dec(g_update_calls);
  put_raw(" update_cycles=");
  put_u32_hex((u32)g_update_last_cycles);
  put_raw(" update_exhausted=");
  put_u32_dec(g_update_last_cycles <= 0 ? 1u : 0u);
  put_raw(" generated_words=");
  put_u32_dec(item->generated_words);
  put_raw(" native_blocks=");
  put_u32_dec(stats.blocks_executed);
  put_raw(" code_bytes=");
  put_u32_dec(code_bytes);
  put_raw(" trace_first_pc=");
  put_u32_hex(g_trace_first_pc);
  put_raw(" trace_count=");
  put_u32_dec(g_trace_count);
  put_raw(" fallbacks=");
  put_u32_dec(g_fallback_count);
  put_raw(" execute_arm_calls=");
  put_u32_dec(g_execute_arm_calls);
  put_raw(" stale_slot_cleared=");
  put_u32_dec(stale_slot_cleared);
  put_raw(" harness_mode=cpu_threaded_frontend reason=");
  put_raw(passed ? "native_exit_contract_executed" :
                   "native_exit_contract_failed");
  put_raw("\n");
  return passed;
}

static int run_frontend_control_cases(void)
{
  u32 i;

  init_dynarec_for_armwrestler();
  for (i = 0; i < RV32IM_FRONTEND_CONTROL_CASE_COUNT; i++)
  {
    if (!run_frontend_control_case(&rv32im_frontend_control_cases[i]))
      return 0;
  }

  put_raw("result=PASS command=frontend-control case=all backend="
          FRONTEND_CONTROL_BACKEND " "
          "cases=");
  put_u32_dec((u32)RV32IM_FRONTEND_CONTROL_CASE_COUNT);
  put_raw(" harness_mode=cpu_threaded_frontend "
          "reason=native_exit_contract_cases_complete\n");
  return 1;
}
#endif

#if defined(RV32IM_FRONTEND_CACHE_ONLY)
static void install_frontend_cache_fixture(void)
{
  u32 block;
  u32 word;

  memset(g_rom, 0, sizeof(g_rom));
  g_rom_size = sizeof(g_rom);
  store32(g_rom, 8u, 0xeafffffeu);
  for (block = 0; block < FRONTEND_CACHE_BLOCKS; block++)
  {
    u32 block_offset = FRONTEND_CACHE_PC - ROM_BASE +
                       block * FRONTEND_CACHE_BLOCK_BYTES;
    for (word = 0; word < FRONTEND_CACHE_BLOCK_WORDS; word++)
      store32(g_rom, block_offset + word * 4u, 0x12800001u);
  }
}

static u32 frontend_cache_arm_branch(u32 source_pc, u32 target_pc)
{
  s32 offset = (s32)target_pc - (s32)source_pc - 8;

  return 0xea000000u | (((u32)(offset >> 2)) & 0x00ffffffu);
}

static int run_frontend_cache_span(void)
{
  riscv_runtime_stats stats;
  u8 *first_entry = 0;
  u8 *entry;
  u32 start_offset;
  u32 previous_offset;
  u32 end_offset;
  u32 generated_bytes;
  u32 chain_distance;
  u32 cache_resets = 0;
  u32 missing_entries = 0;
  u32 block;
  int first_entry_stable;
  int far_chain_executed;
  int passed;

  install_frontend_cache_fixture();
  clear_runtime_memory();
  init_memory_map();
  init_cpu_state();
  idle_loop_target_pc = 0xffffffffu;
  g_execute_arm_calls = 0;
  g_fallback_count = 0;
  reset_dynarec_for_armwrestler();
  init_emitter(false);
  start_offset = (u32)(rom_translation_ptr - rom_translation_cache);
  previous_offset = start_offset;

  for (block = 0; block < FRONTEND_CACHE_BLOCKS; block++)
  {
    entry = block_lookup_address_arm(
      FRONTEND_CACHE_PC + block * FRONTEND_CACHE_BLOCK_BYTES);
    if (!entry)
    {
      missing_entries++;
      break;
    }
    if (!first_entry)
      first_entry = entry;
    end_offset = (u32)(rom_translation_ptr - rom_translation_cache);
    if (end_offset <= previous_offset)
      cache_resets++;
    previous_offset = end_offset;
  }

  end_offset = (u32)(rom_translation_ptr - rom_translation_cache);
  generated_bytes = end_offset - start_offset;
  entry = block_lookup_address_arm(FRONTEND_CACHE_PC);
  first_entry_stable = entry && entry == first_entry;

  store32(g_rom, FRONTEND_CACHE_SOURCE_PC - ROM_BASE,
          frontend_cache_arm_branch(FRONTEND_CACHE_SOURCE_PC,
                                    FRONTEND_CACHE_PC));
  entry = block_lookup_address_arm(FRONTEND_CACHE_SOURCE_PC);
  chain_distance = entry && first_entry ?
    (u32)(entry - first_entry) : 0u;

  init_cpu_state();
  reg[REG_PC] = FRONTEND_CACHE_SOURCE_PC;
  g_update_calls = 0;
  g_update_last_cycles = (s32)0x7fffffffu;
  g_trace_count = 0;
  g_trace_first_pc = 0;
  g_trace_last_pc = 0;
  g_trace_hash = 2166136261u;
  execute_arm_translate_internal(FRONTEND_CACHE_CHAIN_CYCLES, &reg[0]);
  riscv_get_runtime_stats(&stats);
  /* Direct chaining reaches the old target without entering the scheduler at
     the source, so runtime block evidence names only the terminal target. */
  far_chain_executed = entry &&
    chain_distance > FRONTEND_CACHE_MIN_SPAN_BYTES &&
    g_update_calls == 1u && g_update_last_cycles <= 0 &&
    g_trace_count == 1u && g_trace_first_pc == FRONTEND_CACHE_PC &&
    g_trace_last_pc == FRONTEND_CACHE_PC &&
    reg[0] == FRONTEND_CACHE_BLOCK_WORDS &&
    reg[REG_PC] == FRONTEND_CACHE_PC + FRONTEND_CACHE_BLOCK_BYTES &&
    stats.blocks_executed == 1u;

  passed = missing_entries == 0u && cache_resets == 0u &&
           first_entry_stable &&
           generated_bytes > FRONTEND_CACHE_MIN_SPAN_BYTES &&
           end_offset < ROM_TRANSLATION_CACHE_SIZE -
                        TRANSLATION_CACHE_LIMIT_THRESHOLD &&
           stats.blocks_emitted >= FRONTEND_CACHE_BLOCKS + 1u &&
           stats.interpreter_fallbacks == 0u &&
           g_execute_arm_calls == 0u && g_fallback_count == 0u &&
           far_chain_executed;

  put_raw("result=");
  put_raw(passed ? "PASS" : "FAIL");
  put_raw(" command=frontend-cache-span backend=rv32im blocks=");
  put_u32_dec(FRONTEND_CACHE_BLOCKS);
  put_raw(" default_cache_bytes=");
  put_u32_dec(ROM_TRANSLATION_CACHE_SIZE);
  put_raw(" start_offset=");
  put_u32_dec(start_offset);
  put_raw(" end_offset=");
  put_u32_dec(end_offset);
  put_raw(" generated_bytes=");
  put_u32_dec(generated_bytes);
  put_raw(" cache_resets=");
  put_u32_dec(cache_resets);
  put_raw(" first_entry_stable=");
  put_u32_dec(first_entry_stable ? 1u : 0u);
  put_raw(" missing_entries=");
  put_u32_dec(missing_entries);
  put_raw(" emitted_blocks=");
  put_u32_dec(stats.blocks_emitted);
  put_raw(" chain_distance=");
  put_u32_dec(chain_distance);
  put_raw(" far_chain_executed=");
  put_u32_dec(far_chain_executed ? 1u : 0u);
  put_raw(" native_blocks=");
  put_u32_dec(stats.blocks_executed);
  put_raw(" chain_r0=");
  put_u32_dec(reg[0]);
  put_raw(" chain_pc=");
  put_u32_hex(reg[REG_PC]);
  put_raw(" update_calls=");
  put_u32_dec(g_update_calls);
  put_raw(" update_cycles=");
  put_u32_hex((u32)g_update_last_cycles);
  put_raw(" trace_count=");
  put_u32_dec(g_trace_count);
  put_raw(" trace_first_pc=");
  put_u32_hex(g_trace_first_pc);
  put_raw(" trace_last_pc=");
  put_u32_hex(g_trace_last_pc);
  put_raw(" fallbacks=");
  put_u32_dec(stats.interpreter_fallbacks);
  put_raw(" harness_mode=cpu_threaded_frontend reason=");
  put_raw(passed ? "default_rom_cache_span_preserved" :
                   "default_rom_cache_span_churned");
  put_raw("\n");
  return passed;
}
#endif

#if defined(RV32IM_BIOS_SWI_ONLY)
static u32 bios_swi_hash_bytes(u32 hash, const u8 *bytes, u32 count)
{
  u32 i;

  for (i = 0; i < count; i++)
  {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

static u32 bios_swi_state_hash(void)
{
  u32 hash = 2166136261u;
  u32 i;

  for (i = 0; i < 16u; i++)
    hash = fnv1a_update_u32(hash, reg[i]);
  hash = fnv1a_update_u32(hash, reg[REG_CPSR]);
  hash = fnv1a_update_u32(hash, reg[CPU_MODE]);
  hash = fnv1a_update_u32(hash, reg[CPU_HALT_STATE]);
  hash = fnv1a_update_u32(hash, reg[REG_BUS_VALUE]);
  for (i = 0; i < 6u; i++)
    hash = fnv1a_update_u32(hash, spsr[i]);
  for (i = 0; i < 7u * 7u; i++)
    hash = fnv1a_update_u32(hash, ((u32 *)reg_mode)[i]);
  return hash;
}

static u32 bios_swi_memory_hash(void)
{
  u32 hash = 2166136261u;

  hash = bios_swi_hash_bytes(hash, ewram, 0x2200u);
  hash = bios_swi_hash_bytes(hash, iwram + 0x8000u + 0x7f00u, 0x100u);
  hash = bios_swi_hash_bytes(hash, (const u8 *)io_registers,
                             sizeof(io_registers));
  hash = bios_swi_hash_bytes(hash, g_vram, 0x200u);
  hash = bios_swi_hash_bytes(hash, g_palette_ram, sizeof(g_palette_ram));
  hash = bios_swi_hash_bytes(hash, g_oam_ram, sizeof(g_oam_ram));
  return hash;
}

static void install_bios_swi_fixture(rv32im_bios_fixture fixture)
{
  static const u8 lz_literals[12] =
    { 0x10u, 0x21u, 0x32u, 0x43u, 0x54u, 0x65u,
      0x76u, 0x87u, 0x98u, 0xa9u, 0xbau, 0xcbu };
  static const u8 diff8[8] =
    { 0x10u, 0x01u, 0x02u, 0xffu, 0x04u, 0xfcu, 0x08u, 0x80u };
  u32 i;

  switch (fixture)
  {
    case RV32IM_BIOS_FIXTURE_INTR_READY:
      store16(iwram + 0x8000u, 0x7ff8u, 1u);
      break;

    case RV32IM_BIOS_FIXTURE_BIT_UNPACK:
      ewram[0] = 0xe4u;
      ewram[1] = 0x1bu;
      ewram[2] = 0x55u;
      ewram[3] = 0xaau;
      store16(ewram, RV32IM_BIOS_SWI_HEADER - EWRAM_BASE, 4u);
      ewram[RV32IM_BIOS_SWI_HEADER - EWRAM_BASE + 2u] = 2u;
      ewram[RV32IM_BIOS_SWI_HEADER - EWRAM_BASE + 3u] = 8u;
      store32(ewram, RV32IM_BIOS_SWI_HEADER - EWRAM_BASE + 4u,
              0x00000010u);
      break;

    case RV32IM_BIOS_FIXTURE_LZ77:
      store32(ewram, 0u, (12u << 8) | 0x10u);
      ewram[4] = 0u;
      for (i = 0; i < 8u; i++)
        ewram[5u + i] = lz_literals[i];
      ewram[13] = 0u;
      for (i = 0; i < 4u; i++)
        ewram[14u + i] = lz_literals[8u + i];
      break;

    case RV32IM_BIOS_FIXTURE_HUFF:
      store32(ewram, 0u, (4u << 8) | 8u);
      ewram[4] = 1u;
      ewram[5] = 0xc0u;
      ewram[6] = 0x41u;
      ewram[7] = 0x42u;
      store32(ewram, 8u, 0x50000000u);
      break;

    case RV32IM_BIOS_FIXTURE_RL:
      store32(ewram, 0u, (10u << 8) | 0x30u);
      ewram[4] = 0x87u;
      ewram[5] = 0x5au;
      break;

    case RV32IM_BIOS_FIXTURE_DIFF8:
      store32(ewram, 0u, (8u << 8) | 0x80u);
      for (i = 0; i < sizeof(diff8); i++)
        ewram[4u + i] = diff8[i];
      break;

    case RV32IM_BIOS_FIXTURE_DIFF16:
      store32(ewram, 0u, (8u << 8) | 0x81u);
      store16(ewram, 4u, 0x1000u);
      store16(ewram, 6u, 0x0011u);
      store16(ewram, 8u, 0xfff0u);
      store16(ewram, 10u, 0x0100u);
      break;

    case RV32IM_BIOS_FIXTURE_MIDI:
      store32(ewram, 4u, 0x00123456u);
      break;

    default:
      break;
  }
}

static int run_bios_swi_case(const rv32im_bios_swi_case *item)
{
  riscv_runtime_stats stats;
  u32 i;
  u32 code_bytes;
  u32 state_hash;
  u32 memory_hash;
  int passed;

  memset(g_rom, 0, sizeof(g_rom));
  for (i = 0; i < sizeof(g_bios); i++)
    g_bios[i] = g_open_bios[i];
  g_rom_size = sizeof(g_rom);
  store32(g_rom, RV32IM_BIOS_SWI_PC - ROM_BASE,
          0xef000000u | (item->swi << 16));
  store32(g_rom, RV32IM_BIOS_SWI_PC - ROM_BASE + 4u, 0xeafffffeu);

  clear_runtime_memory();
  install_bios_swi_fixture(item->fixture);
  init_memory_map();
  init_cpu_state();
  reg[0] = item->r0;
  reg[1] = item->r1;
  reg[2] = item->r2;
  reg[3] = item->r3;
  reg[REG_PC] = RV32IM_BIOS_SWI_PC;
  reg[REG_CPSR] = 0x0000001fu;
  idle_loop_target_pc = RV32IM_BIOS_SWI_PC + 4u;
  g_execute_arm_calls = 0u;
  g_execute_arm_cycles = 0u;
  g_update_calls = 0u;
  g_update_last_cycles = (s32)0x7fffffffu;
  g_trace_count = 0u;
  g_trace_first_pc = 0u;
  g_trace_last_pc = 0u;
  g_trace_hash = 2166136261u;
  g_fallback_count = 0u;
  g_fallback_first_pc = 0u;
  g_fallback_last_pc = 0u;
  g_fallback_hash = 2166136261u;
  reset_dynarec_for_armwrestler();
  init_emitter(false);

  execute_arm_translate_internal(500000u, &reg[0]);
  riscv_get_runtime_stats(&stats);
  code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);
  state_hash = bios_swi_state_hash();
  memory_hash = bios_swi_memory_hash();
  passed = g_update_calls == 1u && g_update_last_cycles <= 0 &&
           reg[REG_PC] == RV32IM_BIOS_SWI_PC + 4u &&
           stats.blocks_executed != 0u &&
           stats.bios_native_blocks_executed != 0u &&
           stats.interpreter_fallbacks == 0u &&
           stats.bios_interpreter_fallbacks == 0u &&
           g_fallback_count == 0u && g_execute_arm_calls == 0u &&
           code_bytes != 0u;

  put_raw("result=");
  put_raw(passed ? "PASS" : "FAIL");
  put_raw(" command=bios-swi case=");
  put_raw(item->name);
  put_raw(" backend=rv32im swi=");
  put_u32_hex(item->swi);
  put_raw(" state_hash=");
  put_u32_hex(state_hash);
  put_raw(" memory_hash=");
  put_u32_hex(memory_hash);
  put_raw(" r0=");
  put_u32_hex(reg[0]);
  put_raw(" r1=");
  put_u32_hex(reg[1]);
  put_raw(" r2=");
  put_u32_hex(reg[2]);
  put_raw(" r3=");
  put_u32_hex(reg[3]);
  put_raw(" pc=");
  put_u32_hex(reg[REG_PC]);
  put_raw(" cpsr=");
  put_u32_hex(reg[REG_CPSR]);
  put_raw(" update_calls=");
  put_u32_dec(g_update_calls);
  put_raw(" update_cycles=");
  put_u32_hex((u32)g_update_last_cycles);
  put_raw(" native_blocks=");
  put_u32_dec(stats.blocks_executed);
  put_raw(" bios_native_blocks=");
  put_u32_dec(stats.bios_native_blocks_executed);
  put_raw(" bios_blocks_emitted=");
  put_u32_dec(stats.bios_native_blocks_emitted);
  put_raw(" code_bytes=");
  put_u32_dec(code_bytes);
  put_raw(" fallbacks=");
  put_u32_dec(stats.interpreter_fallbacks);
  put_raw(" bios_fallbacks=");
  put_u32_dec(stats.bios_interpreter_fallbacks);
  put_raw(" execute_arm_calls=");
  put_u32_dec(g_execute_arm_calls);
  put_raw(" harness_mode=cpu_threaded_frontend reason=");
  put_raw(passed ? "real_bios_swi_executed_native" :
                   "real_bios_swi_native_contract_failed");
  put_raw("\n");
  return passed;
}

static int run_bios_swi_cases(void)
{
  u32 i;

  for (i = 0; i < RV32IM_BIOS_SWI_CASE_COUNT; i++)
  {
    if (!run_bios_swi_case(&rv32im_bios_swi_cases[i]))
      return 0;
  }
  put_raw("result=PASS command=bios-swi case=all backend=rv32im cases=");
  put_u32_dec((u32)RV32IM_BIOS_SWI_CASE_COUNT);
  put_raw(" harness_mode=cpu_threaded_frontend "
          "reason=real_bios_swi_cases_complete\n");
  return 1;
}
#endif

#if defined(RV32IM_BIOS_JIT_AUDIT_ONLY)
#define OPEN_GBA_BIOS_SWI_TABLE 0x000000b0u
#define OPEN_GBA_BIOS_SWI_COUNT 43u

static int audit_bios_root(u32 pc, u32 *missing, u32 *unsupported)
{
  u8 *entry = block_lookup_address_arm(pc);
  riscv_jit_block_meta *meta;

  if (!entry || entry == (u8 *)(~(usize)0))
  {
    (*missing)++;
    return 0;
  }

  meta = (riscv_jit_block_meta *)(void *)(entry - block_prologue_size);
  if (meta->start_pc != pc || !(meta->flags & 1u))
  {
    (*unsupported)++;
    return 0;
  }
  return 1;
}

static int run_bios_jit_audit(void)
{
  static const u32 vector_roots[] = { 0x00000000u, 0x00000008u,
                                      0x00000018u };
  riscv_runtime_stats stats;
  u32 invalid_table_entries = 0;
  u32 missing_roots = 0;
  u32 unsupported_roots = 0;
  u32 unique_roots = 0;
  u32 root_count = 0;
  u32 code_bytes;
  u32 i;
  int passed;

  memset(g_rom, 0, sizeof(g_rom));
  g_rom_size = sizeof(g_rom);
  clear_runtime_memory();
  init_memory_map();
  init_cpu_state();
  idle_loop_target_pc = 0xffffffffu;
  reset_dynarec_for_armwrestler();
  init_emitter(false);

  for (i = 0; i < sizeof(vector_roots) / sizeof(vector_roots[0]); i++)
  {
    u32 before = (u32)(rom_translation_ptr - rom_translation_cache);

    root_count++;
    audit_bios_root(vector_roots[i], &missing_roots, &unsupported_roots);
    if ((u32)(rom_translation_ptr - rom_translation_cache) != before)
      unique_roots++;
  }

  for (i = 0; i < OPEN_GBA_BIOS_SWI_COUNT; i++)
  {
    u32 pc = load32(g_bios, OPEN_GBA_BIOS_SWI_TABLE + i * 4u);
    u32 before;

    root_count++;
    if (pc >= sizeof(g_bios) || (pc & 3u))
    {
      invalid_table_entries++;
      continue;
    }
    before = (u32)(rom_translation_ptr - rom_translation_cache);
    audit_bios_root(pc, &missing_roots, &unsupported_roots);
    if ((u32)(rom_translation_ptr - rom_translation_cache) != before)
      unique_roots++;
  }

  riscv_get_runtime_stats(&stats);
  code_bytes = (u32)(rom_translation_ptr - rom_translation_cache);
  passed = invalid_table_entries == 0u && missing_roots == 0u &&
           unsupported_roots == 0u && stats.blocks_emitted != 0u &&
           stats.blocks_emitted == stats.bios_native_blocks_emitted &&
           stats.interpreter_fallbacks == 0u && code_bytes != 0u;

  put_raw("result=");
  put_raw(passed ? "PASS" : "FAIL");
  put_raw(" command=bios-jit-audit bios_bytes=");
  put_u32_dec((u32)sizeof(g_bios));
  put_raw(" vectors=");
  put_u32_dec((u32)(sizeof(vector_roots) / sizeof(vector_roots[0])));
  put_raw(" swi_entries=");
  put_u32_dec(OPEN_GBA_BIOS_SWI_COUNT);
  put_raw(" roots=");
  put_u32_dec(root_count);
  put_raw(" unique_roots=");
  put_u32_dec(unique_roots);
  put_raw(" invalid_table_entries=");
  put_u32_dec(invalid_table_entries);
  put_raw(" missing_roots=");
  put_u32_dec(missing_roots);
  put_raw(" unsupported_roots=");
  put_u32_dec(unsupported_roots);
  put_raw(" blocks_emitted=");
  put_u32_dec(stats.blocks_emitted);
  put_raw(" bios_native_blocks_emitted=");
  put_u32_dec(stats.bios_native_blocks_emitted);
  put_raw(" code_bytes=");
  put_u32_dec(code_bytes);
  put_raw(" fallbacks=");
  put_u32_dec(stats.interpreter_fallbacks);
  put_raw(" bios_fallbacks=");
  put_u32_dec(stats.bios_interpreter_fallbacks);
  put_raw(" harness_mode=cpu_threaded_frontend reason=");
  put_raw(passed ? "all_bios_vector_and_swi_roots_translate_native" :
                   "bios_root_translation_not_fully_native");
  put_raw("\n");
  return passed;
}
#endif

void _start(void)
{
  u32 i;

#if defined(RV32IM_BIOS_SWI_ONLY)
  (void)i;
  if (!load_open_gba_bios(g_open_bios))
  {
    put_raw("result=FAIL command=bios-swi reason=bios_load_failed path="
            OPEN_GBA_BIOS "\n");
    sys_exit(2);
  }
  init_dynarec_for_armwrestler();
  sys_exit(run_bios_swi_cases() ? 0 : 1);
#elif defined(RV32IM_BIOS_JIT_AUDIT_ONLY)
  (void)i;
  if (!load_open_gba_bios(g_bios))
  {
    put_raw("result=FAIL command=bios-jit-audit reason=bios_load_failed path="
            OPEN_GBA_BIOS "\n");
    sys_exit(2);
  }
  init_dynarec_for_armwrestler();
  sys_exit(run_bios_jit_audit() ? 0 : 1);
#elif defined(RV32IM_FRONTEND_CACHE_ONLY)
  (void)i;
  init_dynarec_for_armwrestler();
  sys_exit(run_frontend_cache_span() ? 0 : 1);
#elif defined(RV32IM_FRONTEND_CONTROL_ONLY)
  (void)i;
  sys_exit(run_frontend_control_cases() ? 0 : 1);
#else
  if (!load_rom())
  {
    put_raw("result=FAIL command=armwrestler reason=rom_load_failed path="
            ARMWRESTLER_ROM "\n");
    sys_exit(2);
  }

  patch_loaded_rom();
  init_dynarec_for_armwrestler();
  init_emitter(false);

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
#endif
}
