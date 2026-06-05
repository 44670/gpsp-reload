#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "common.h"
}

#ifndef ARMWRESTLER_ROM
#define ARMWRESTLER_ROM \
  "/home/john/ref/armwrestler-gba-fixed/armwrestler-gba-fixed.gba"
#endif

#define ROM_BASE 0x08000000u
#define ROM_MAX_BYTES (2u * 1024u * 1024u)
#define ROM_PAGE_BYTES (32u * 1024u)
#define EWRAM_BASE 0x02000000u
#define IWRAM_BASE 0x03000000u
#define ARM_MAIN_PC 0x080002d8u
#define ARM_VSYNC_PC 0x080004c4u
#define ARM_DRAW_RESULT_PC 0x08000634u
#define ARM_TESTNUM_MOV_PC 0x0800032cu
#define ARMWRESTLER_TEST0 0u
#define ARMWRESTLER_TEST0_RESULTS 15u
#define RESULT_BASE 0x03000100u
#define FRAME_COMPLETE 0x80000000u
#define RUN_CYCLES 200000u
#define RUN_CHUNKS 64u

static u8 g_rom[ROM_MAX_BYTES];
static u8 g_rom_open_bus[ROM_PAGE_BYTES];
static u32 g_rom_size;
static u32 g_update_calls;
static u32 g_read32_calls;
static u32 g_write32_calls;

u32 gamepak_sticky_bit[1024 / 32];
u32 gamepak_size;
char gamepak_code[5];
char gamepak_filename[512];
u32 idle_loop_target_pc = 0xffffffffu;
u32 translation_gate_targets;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 cheat_master_hook = 0xffffffffu;
boot_mode selected_boot_mode = boot_game;

u8 ws_cyc_seq[16][2] =
{
  { 1, 1 }, { 1, 1 }, { 3, 6 }, { 1, 1 },
  { 1, 1 }, { 1, 2 }, { 1, 2 }, { 1, 2 },
  { 3, 6 }, { 3, 6 }, { 5, 9 }, { 5, 9 },
  { 9, 17 }, { 9, 17 }, { 1, 1 }, { 1, 1 },
};

u8 ws_cyc_nseq[16][2] =
{
  { 1, 1 }, { 1, 1 }, { 3, 6 }, { 1, 1 },
  { 1, 1 }, { 1, 2 }, { 1, 2 }, { 1, 2 },
  { 3, 6 }, { 3, 6 }, { 5, 9 }, { 5, 9 },
  { 9, 17 }, { 9, 17 }, { 1, 1 }, { 1, 1 },
};

void process_cheats(void)
{
}

bool bson_contains_key(const u8 *srcp, const char *key, u8 keytype)
{
  (void)srcp;
  (void)key;
  (void)keytype;
  return false;
}

const u8 *bson_find_key(const u8 *srcp, const char *key)
{
  (void)key;
  return srcp;
}

bool bson_read_int32(const u8 *srcp, const char *key, u32 *value)
{
  (void)srcp;
  (void)key;
  (void)value;
  return false;
}

bool bson_read_int32_array(const u8 *srcp, const char *key, u32 *value,
                           unsigned cnt)
{
  (void)srcp;
  (void)key;
  (void)value;
  (void)cnt;
  return false;
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

static bool load_rom(void)
{
  FILE *file = std::fopen(ARMWRESTLER_ROM, "rb");
  size_t got;

  if (!file)
    return false;

  got = std::fread(g_rom, 1, sizeof(g_rom), file);
  std::fclose(file);
  g_rom_size = (u32)got;
  gamepak_size = (g_rom_size + ROM_PAGE_BYTES - 1u) & ~(ROM_PAGE_BYTES - 1u);
  return got != 0;
}

static void patch_word(u32 pc, u32 value)
{
  store32(g_rom, pc - ROM_BASE, value);
}

static void patch_loaded_rom(void)
{
  static const u32 draw_result_patch[] =
  {
    0xe3a03403u, 0xe2833c01u, 0xe593c000u, 0xe28cc001u,
    0xe583c000u, 0xe593c004u, 0xe18cc001u, 0xe583c004u,
    0xe5830008u, 0xe583100cu, 0xe5832010u, 0xe1a0f00eu,
  };

  patch_word(ARM_TESTNUM_MOV_PC, 0xe3a00000u | ARMWRESTLER_TEST0);
  patch_word(ARM_VSYNC_PC, 0xe1a0f00eu);
  for (u32 i = 0; i < sizeof(draw_result_patch) / sizeof(draw_result_patch[0]); i++)
    patch_word(ARM_DRAW_RESULT_PC + i * 4u, draw_result_patch[i]);
}

static void map_read_region(u32 start, u32 bytes, u8 *base, u32 mask)
{
  for (u32 address = start; address < start + bytes; address += ROM_PAGE_BYTES)
    memory_map_read[address >> 15] = base + (address & mask);
}

static void init_memory_map(void)
{
  std::memset(memory_map_read, 0, sizeof(memory_map_read));
  map_read_region(EWRAM_BASE, 0x01000000u, ewram, 0x3ffffu);
  map_read_region(IWRAM_BASE, 0x01000000u, iwram + 0x8000, 0x7fffu);

  for (u32 address = ROM_BASE; address < 0x0e000000u; address += ROM_PAGE_BYTES)
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

  switch (region)
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
  return load16(addr_ptr(address, 2), 0);
}

u16 function_cc read_memory16_signed(u32 address)
{
  return (u16)((s16)read_memory16(address));
}

u32 function_cc read_memory16s(u32 address)
{
  return (u32)((s16)read_memory16(address));
}

u32 function_cc read_memory32(u32 address)
{
  g_read32_calls++;
  return load32(addr_ptr(address, 4), 0);
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
  g_write32_calls++;
  store32(addr_ptr(address, 4), 0, value);
  return CPU_ALERT_NONE;
}

u32 function_cc update_gba(int remaining_cycles)
{
  (void)remaining_cycles;
  g_update_calls++;
  return FRAME_COMPLETE;
}

static u32 result_word(u32 offset)
{
  return load32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + offset);
}

static void print_summary(const char *result, const char *reason)
{
  std::printf("result=%s command=armwrestler-interp test=arm0 "
              "expected_results=%u observed_results=%u failure_mask=0x%08x "
              "read32_calls=%u write32_calls=%u update_calls=%u "
              "harness_mode=armwrestler_interpreter reason=%s\n",
              result, ARMWRESTLER_TEST0_RESULTS, result_word(0),
              result_word(4), g_read32_calls, g_write32_calls,
              g_update_calls, reason);
}

int main(void)
{
  if (!load_rom())
  {
    std::printf("result=FAIL command=armwrestler-interp reason=rom_load_failed "
                "path=%s\n", ARMWRESTLER_ROM);
    return 2;
  }

  patch_loaded_rom();
  init_memory_map();
  init_cpu();
  reg[REG_SP] = 0x03007f00u;
  reg[REG_PC] = ARM_MAIN_PC;
  reg[REG_CPSR] = 0x0000001fu;
  reg[CPU_MODE] = MODE_SYSTEM;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;

  for (u32 i = 0; i < RUN_CHUNKS; i++)
  {
    execute_arm(RUN_CYCLES);
    if (result_word(0) >= ARMWRESTLER_TEST0_RESULTS)
      break;
  }

  if (result_word(0) < ARMWRESTLER_TEST0_RESULTS)
  {
    print_summary("FAIL", "result_timeout");
    return 1;
  }
  if (result_word(4) != 0)
  {
    print_summary("FAIL", "armwrestler_reported_bad");
    return 1;
  }

  print_summary("PASS", "armwrestler_arm0_interpreter_passed");
  return 0;
}
