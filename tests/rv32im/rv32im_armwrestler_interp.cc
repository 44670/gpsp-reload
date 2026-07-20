#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "common.h"
}
#if defined(RV32IM_MAPPED_ALU_ONLY)
#include "rv32im_mapped_alu_cases.h"
#endif
#if defined(RV32IM_FRONTEND_CONTROL_ONLY)
#include "rv32im_frontend_control_cases.h"
#endif
#if defined(RV32IM_BIOS_SWI_ONLY)
#include "rv32im_bios_swi_cases.h"
#endif

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

#if !defined(RV32IM_MAPPED_ALU_ONLY)
static const u32 g_arm_test_ids[ARMWRESTLER_ARM_TESTS] =
  { 0u, 1u, 2u, 3u, 4u };
static const u32 g_arm_test_results[ARMWRESTLER_ARM_TESTS] =
  { 15u, 6u, 16u, 10u, 12u };
static const u32 g_thumb_test_ids[ARMWRESTLER_THUMB_TESTS] =
  { 0u, 1u, 2u };
static const u32 g_thumb_test_results[ARMWRESTLER_THUMB_TESTS] =
  { 11u, 7u, 2u };
#endif

static u8 g_rom[ROM_MAX_BYTES];
static u8 g_rom_open_bus[ROM_PAGE_BYTES];
static u8 g_bios[0x4000u];
#if defined(RV32IM_BIOS_SWI_ONLY)
static u8 g_open_bios[0x4000u];
#endif
static u32 g_rom_size;
static u32 g_update_calls;
static s32 g_update_last_cycles;
#if defined(RV32IM_FRONTEND_CONTROL_ONLY)
static u32 g_frontend_control_resume_updates;
static u32 g_frontend_control_resume_cycles;
static u32 g_frontend_control_raise_irq;
#endif
static u32 g_read32_calls;
static u32 g_write32_calls;
#if !defined(RV32IM_MAPPED_ALU_ONLY)
static u32 g_total_observed_results;
static u32 g_total_failure_mask;
#endif

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

#if !defined(RV32IM_MAPPED_ALU_ONLY)
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

#if defined(RV32IM_BIOS_SWI_ONLY)
static bool load_open_gba_bios(void)
{
  FILE *file = std::fopen(OPEN_GBA_BIOS, "rb");
  size_t got;
  int extra;

  if (!file)
    return false;
  got = std::fread(g_open_bios, 1, sizeof(g_open_bios), file);
  extra = std::fgetc(file);
  std::fclose(file);
  return got == sizeof(g_open_bios) && extra == EOF;
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

  for (u32 i = 0; i < count; i++)
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

  patch_word(ARM_VSYNC_PC, 0xe1a0f00eu);
  for (u32 i = 0; i < sizeof(draw_result_patch) / sizeof(draw_result_patch[0]); i++)
    patch_word(ARM_DRAW_RESULT_PC + i * 4u, draw_result_patch[i]);
  patch_halfword(THUMB_VSYNC_PC, 0x4770u);
  patch_bytes(THUMB_DRAW_RESULT_PC, thumb_draw_result_patch,
              sizeof(thumb_draw_result_patch));
}
#endif

static void map_read_region(u32 start, u32 bytes, u8 *base, u32 mask)
{
  for (u32 address = start; address < start + bytes; address += ROM_PAGE_BYTES)
    memory_map_read[address >> 15] = base + (address & mask);
}

static void init_memory_map(void)
{
  std::memset(memory_map_read, 0, sizeof(memory_map_read));
  map_read_region(0x00000000u, 0x00004000u, g_bios, 0x3fffu);
  map_read_region(EWRAM_BASE, 0x01000000u, ewram, 0x3ffffu);
  map_read_region(IWRAM_BASE, 0x01000000u, iwram + 0x8000, 0x7fffu);

  for (u32 address = ROM_BASE; address < 0x0e000000u; address += ROM_PAGE_BYTES)
  {
    u32 offset = (address - ROM_BASE) & 0x01ffffffu;
    memory_map_read[address >> 15] =
      offset < ROM_MAX_BYTES ? g_rom + offset : g_rom_open_bus;
  }
}

#if !defined(RV32IM_MAPPED_ALU_ONLY)
static void clear_runtime_memory(void)
{
  std::memset(ewram, 0, sizeof(ewram));
  std::memset(iwram, 0, sizeof(iwram));
  std::memset(io_registers, 0, sizeof(io_registers));
  std::memset(vram, 0, sizeof(vram));
  std::memset(palette_ram, 0, sizeof(palette_ram));
  std::memset(oam_ram, 0, sizeof(oam_ram));
}
#endif

static u8 *addr_ptr(u32 address, u32 size)
{
  u32 region = address >> 24;
  u32 offset;

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
  u32 value = load16(addr_ptr(address & ~1u, 2), 0);
  return (address & 1u) ? ror32(value, 8) : value;
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
  g_write32_calls++;
  store32(addr_ptr(address, 4), 0, value);
  return CPU_ALERT_NONE;
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

#if !defined(RV32IM_MAPPED_ALU_ONLY)
static u32 result_word(u32 offset)
{
  return load32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + offset);
}

static void init_armwrestler_cpu_state(void)
{
  init_cpu();
  reg[REG_SP] = 0x03007f00u;
  reg[REG_PC] = ARM_MAIN_PC;
  reg[REG_CPSR] = 0x0000001fu;
  reg[CPU_MODE] = MODE_SYSTEM;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
}

static void init_thumbwrestler_cpu_state(void)
{
  init_cpu();
  reg[REG_SP] = 0x03007f00u;
  reg[REG_PC] = THUMB_MAIN_PC;
  reg[REG_CPSR] = 0x0000003fu;
  reg[CPU_MODE] = MODE_SYSTEM;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
  reg[8] = 0;
  reg[9] = 3;
  reg[10] = 0;
  reg[11] = 0xffffffffu;
}
#endif

#if defined(RV32IM_MAPPED_ALU_ONLY)
static bool run_mapped_alu_interpreter_cases(void)
{
  bool all_passed = true;

  std::memset(g_rom, 0, sizeof(g_rom));
  g_rom_size = sizeof(g_rom);
  init_memory_map();

  for (u32 i = 0; i < RV32IM_MAPPED_ALU_CASE_COUNT; i++)
  {
    const rv32im_mapped_alu_case *item = &rv32im_mapped_alu_cases[i];
    u32 pc = 0x08015000u + i * 0x100u;
    bool passed;

    store32(g_rom, pc - ROM_BASE, rv32im_mapped_alu_opcode(item));
    init_cpu();
    rv32im_mapped_alu_initial_regs(&reg[0]);
    reg[REG_PC] = pc;
    reg[REG_CPSR] = 0x2000001fu;
    reg[CPU_MODE] = MODE_SYSTEM;
    reg[CPU_HALT_STATE] = CPU_ACTIVE;
    execute_arm(1u);

    passed = reg[REG_PC] == pc + 4u && reg[REG_CPSR] == 0x2000001fu;
    std::printf("result=%s command=rv32im-mapped-alu-semantics case=%s "
                "backend=interp opcode=0x%08x rd=%u rd_value=0x%08x "
                "pc=0x%08x cpsr=0x%08x guest_state_hash=0x%08x "
                "harness_mode=cpu_cc_interpreter reason=%s\n",
                passed ? "PASS" : "FAIL", item->name,
                rv32im_mapped_alu_opcode(item), item->rd, reg[item->rd],
                reg[REG_PC], reg[REG_CPSR],
                rv32im_mapped_alu_state_hash(&reg[0]),
                passed ? "interpreter_case_executed" :
                         "interpreter_case_contract_mismatch");
    if (!passed)
      all_passed = false;
  }

  std::printf("result=%s command=rv32im-mapped-alu-semantics case=all "
              "backend=interp cases=%u reason=%s\n",
              all_passed ? "PASS" : "FAIL",
              (u32)RV32IM_MAPPED_ALU_CASE_COUNT,
              all_passed ? "interpreter_semantic_cases_complete" :
                           "interpreter_semantic_case_failed");
  return all_passed;
}
#endif

#if !defined(RV32IM_MAPPED_ALU_ONLY)
static void print_summary(const char *result, const char *suite, u32 test_id,
                          u32 expected_results, const char *reason)
{
  std::printf("result=%s command=armwrestler-interp-test test=%s%u "
              "expected_results=%u observed_results=%u failure_mask=0x%08x "
              "last_label=0x%08x last_mask=0x%08x last_type=%u "
              "read32_calls=%u write32_calls=%u update_calls=%u "
              "harness_mode=armwrestler_interpreter reason=%s\n",
              result, suite, test_id, expected_results, result_word(0),
              result_word(4), result_word(8), result_word(12),
              result_word(16), g_read32_calls, g_write32_calls,
              g_update_calls, reason);
}

static void print_aggregate_summary(const char *result, const char *reason)
{
  std::printf("result=%s command=armwrestler-interp test=all "
              "expected_results=%u observed_results=%u failure_mask=0x%08x "
              "tests=%u read32_calls=%u write32_calls=%u update_calls=%u "
              "harness_mode=armwrestler_interpreter reason=%s\n",
              result, ARMWRESTLER_TOTAL_RESULTS, g_total_observed_results,
              g_total_failure_mask, ARMWRESTLER_TOTAL_TESTS, g_read32_calls,
              g_write32_calls, g_update_calls, reason);
}

static bool run_armwrestler_test(u32 test_id, u32 expected_results)
{
  patch_test_id(test_id);
  clear_runtime_memory();
  store32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + 20u,
          expected_results);
  init_memory_map();
  init_armwrestler_cpu_state();

  for (u32 i = 0; i < RUN_CHUNKS; i++)
  {
    execute_arm(RUN_CYCLES);
    if (result_word(0) >= expected_results)
      break;
  }

  g_total_observed_results += result_word(0);
  g_total_failure_mask |= result_word(4);

  if (result_word(0) < expected_results)
  {
    print_summary("FAIL", "arm", test_id, expected_results, "result_timeout");
    return false;
  }
  if (result_word(4) != 0)
  {
    print_summary("FAIL", "arm", test_id, expected_results,
                  "armwrestler_reported_bad");
    return false;
  }

  print_summary("PASS", "arm", test_id, expected_results,
                "armwrestler_arm_test_interpreter_passed");
  return true;
}

static bool run_thumbwrestler_test(u32 test_id, u32 expected_results)
{
  clear_runtime_memory();
  store32(iwram + 0x8000u, 0x08u, test_id);
  store32(iwram + 0x8000u, (RESULT_BASE - IWRAM_BASE) + 20u,
          expected_results);
  init_memory_map();
  init_thumbwrestler_cpu_state();

  for (u32 i = 0; i < RUN_CHUNKS; i++)
  {
    execute_arm(RUN_CYCLES);
    if (result_word(0) >= expected_results)
      break;
  }

  g_total_observed_results += result_word(0);
  g_total_failure_mask |= result_word(4);

  if (result_word(0) < expected_results)
  {
    print_summary("FAIL", "thumb", test_id, expected_results,
                  "result_timeout");
    return false;
  }
  if (result_word(4) != 0)
  {
    print_summary("FAIL", "thumb", test_id, expected_results,
                  "thumbwrestler_reported_bad");
    return false;
  }

  print_summary("PASS", "thumb", test_id, expected_results,
                "thumbwrestler_test_interpreter_passed");
  return true;
}
#endif

#if defined(RV32IM_FRONTEND_CONTROL_ONLY)
static void install_frontend_control_case(
  const rv32im_frontend_control_case *item)
{
  std::memset(g_rom, 0, sizeof(g_rom));
  std::memset(g_bios, 0, sizeof(g_bios));
  g_rom_size = sizeof(g_rom);
  store32(g_rom, 8u, 0xeafffffeu);
  for (u32 i = 0; i < item->generated_words; i++)
  {
    store32(g_rom, RV32IM_FRONTEND_CONTROL_PC - ROM_BASE + i * 4u,
            0x12800001u);
  }
  for (u32 i = 0; i < item->word_count; i++)
  {
    store32(g_rom, RV32IM_FRONTEND_CONTROL_PC - ROM_BASE + i * 4u,
            item->words[i]);
  }
  if (item->resume_updates != 0u)
  {
    for (u32 i = 0;
         i < sizeof(rv32im_frontend_control_swi_dispatcher) /
               sizeof(rv32im_frontend_control_swi_dispatcher[0]);
         i++)
    {
      store32(g_bios, rv32im_frontend_control_swi_dispatcher[i].offset,
              rv32im_frontend_control_swi_dispatcher[i].word);
    }
    for (u32 i = 0;
         i < sizeof(rv32im_frontend_control_cpuset_words) /
               sizeof(rv32im_frontend_control_cpuset_words[0]);
         i++)
      store32(g_bios, 0x614u + i * 4u,
              rv32im_frontend_control_cpuset_words[i]);
    for (u32 i = 0;
         i < sizeof(rv32im_frontend_control_cpufastset_words) /
               sizeof(rv32im_frontend_control_cpufastset_words[0]);
         i++)
      store32(g_bios, 0x720u + i * 4u,
              rv32im_frontend_control_cpufastset_words[i]);
    for (u32 i = 0;
         i < sizeof(rv32im_frontend_control_bgaffineset_words) /
               sizeof(rv32im_frontend_control_bgaffineset_words[0]);
         i++)
      store32(g_bios, 0x7e4u + i * 4u,
              rv32im_frontend_control_bgaffineset_words[i]);
    for (u32 i = 0;
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

static bool run_frontend_control_case(
  const rv32im_frontend_control_case *item)
{
  u32 state_hash;
  u32 memory_hash = 2166136261u;
  bool passed;

  install_frontend_control_case(item);
  clear_runtime_memory();
  if (std::strcmp(
        item->name,
        "arm_openbios_cpufastset_large_iwram_fill_resume") == 0)
  {
    store32(iwram + 0x8000u, 0x7df4u, 0xa5a5a5a5u);
  }
  else if (std::strcmp(item->name,
                  "arm_openbios_cpufastset_copy_resume") == 0)
  {
    for (u32 source_word = 0; source_word < 8u; source_word++)
      store32(ewram, source_word * 4u, (source_word + 1u) * 0x11u);
  }
  else if (std::strcmp(item->name, "arm_openbios_bgaffineset_resume") == 0)
  {
    store32(ewram, 0x00u, 0x00012345u);
    store32(ewram, 0x04u, 0xfffedcbbu);
    store16(ewram, 0x08u, 0x0011u);
    store16(ewram, 0x0au, 0xffe2u);
    store16(ewram, 0x0cu, 0x0180u);
    store16(ewram, 0x0eu, 0xff40u);
    store16(ewram, 0x10u, 0x0000u);
  }
  else if (std::strcmp(item->name,
                       "arm_openbios_objaffineset_resume") == 0)
  {
    store16(ewram, 0x00u, 0x0180u);
    store16(ewram, 0x02u, 0xff40u);
    store16(ewram, 0x04u, 0x0000u);
  }
  if (item->resume_updates != 0u)
    store32(iwram + 0x8000u, 0x7ffcu,
            RV32IM_FRONTEND_CONTROL_PC + 0x40u);
  init_memory_map();
  init_cpu();
  reg[REG_PC] = RV32IM_FRONTEND_CONTROL_PC + item->entry_word_offset * 4u;
  reg[REG_CPSR] = item->initial_cpsr;
  idle_loop_target_pc = item->idle_pc;
  g_update_calls = 0;
  g_update_last_cycles = (s32)0x7fffffffu;
  g_frontend_control_resume_updates = item->resume_updates;
  g_frontend_control_resume_cycles = item->resume_cycles;
  g_frontend_control_raise_irq = 0u;
  g_read32_calls = 0;
  g_write32_calls = 0;

  execute_arm(item->cycles);

  state_hash = rv32im_frontend_control_state_hash(
    &reg[0], &spsr[0], &reg_mode[0][0], g_update_calls,
    g_update_last_cycles);
  for (u32 memory_offset = 0x1000u; memory_offset < 0x2000u;
       memory_offset += 4u)
    memory_hash = rv32im_frontend_control_hash_word(
      memory_hash, load32(ewram, memory_offset));
  state_hash = rv32im_frontend_control_hash_word(state_hash, memory_hash);
  passed = g_update_calls == item->resume_updates + 1u &&
           g_update_last_cycles != (s32)0x7fffffffu;
  std::printf("result=%s command=frontend-control case=%s backend=interp "
              "state_hash=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x "
              "r3=0x%08x r4=0x%08x r5=0x%08x r8=0x%08x r10=0x%08x "
              "lr=0x%08x pc=0x%08x "
              "cpsr=0x%08x svc_spsr=0x%08x svc_lr=0x%08x "
              "irq_spsr=0x%08x sys_sp=0x%08x sys_lr=0x%08x "
              "svc_sp=0x%08x irq_sp=0x%08x irq_lr=0x%08x "
              "memory_hash=0x%08x "
              "update_calls=%u update_cycles=0x%08x update_exhausted=%u "
              "generated_words=%u "
              "harness_mode=cpu_cc_interpreter reason=%s\n",
              passed ? "PASS" : "FAIL", item->name, state_hash,
              reg[0], reg[1], reg[2], reg[3], reg[4], reg[5], reg[8],
              reg[10], reg[REG_LR], reg[REG_PC], reg[REG_CPSR],
              REG_SPSR(MODE_SUPERVISOR),
              REG_MODE(MODE_SUPERVISOR)[6], REG_SPSR(MODE_IRQ),
              REG_MODE(MODE_SYSTEM)[5], REG_MODE(MODE_SYSTEM)[6],
              REG_MODE(MODE_SUPERVISOR)[5], REG_MODE(MODE_IRQ)[5],
              REG_MODE(MODE_IRQ)[6], memory_hash, g_update_calls,
              (u32)g_update_last_cycles,
              g_update_last_cycles <= 0 ? 1u : 0u,
              item->generated_words,
              passed ? "interpreter_exit_contract_executed" :
                       "interpreter_exit_contract_failed");
  return passed;
}

static bool run_frontend_control_cases(void)
{
  for (u32 i = 0; i < RV32IM_FRONTEND_CONTROL_CASE_COUNT; i++)
  {
    if (!run_frontend_control_case(&rv32im_frontend_control_cases[i]))
      return false;
  }

  std::printf("result=PASS command=frontend-control case=all backend=interp "
              "cases=%u harness_mode=cpu_cc_interpreter "
              "reason=interpreter_exit_contract_cases_complete\n",
              (u32)RV32IM_FRONTEND_CONTROL_CASE_COUNT);
  return true;
}
#endif

#if defined(RV32IM_BIOS_SWI_ONLY)
static u32 bios_swi_hash_word(u32 hash, u32 value)
{
  for (u32 i = 0; i < 4u; i++)
  {
    hash ^= (value >> (i * 8u)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

static u32 bios_swi_hash_bytes(u32 hash, const u8 *bytes, u32 count)
{
  for (u32 i = 0; i < count; i++)
  {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

static u32 bios_swi_state_hash(void)
{
  u32 hash = 2166136261u;

  for (u32 i = 0; i < 16u; i++)
    hash = bios_swi_hash_word(hash, reg[i]);
  hash = bios_swi_hash_word(hash, reg[REG_CPSR]);
  hash = bios_swi_hash_word(hash, reg[CPU_MODE]);
  hash = bios_swi_hash_word(hash, reg[CPU_HALT_STATE]);
  hash = bios_swi_hash_word(hash, reg[REG_BUS_VALUE]);
  for (u32 i = 0; i < 6u; i++)
    hash = bios_swi_hash_word(hash, spsr[i]);
  for (u32 i = 0; i < 7u * 7u; i++)
    hash = bios_swi_hash_word(hash, ((u32 *)reg_mode)[i]);
  return hash;
}

static u32 bios_swi_memory_hash(void)
{
  u32 hash = 2166136261u;

  hash = bios_swi_hash_bytes(hash, ewram, 0x2200u);
  hash = bios_swi_hash_bytes(hash, iwram + 0x8000u + 0x7f00u, 0x100u);
  hash = bios_swi_hash_bytes(hash, (const u8 *)io_registers,
                             sizeof(io_registers));
  hash = bios_swi_hash_bytes(hash, vram, 0x200u);
  hash = bios_swi_hash_bytes(hash, (const u8 *)palette_ram,
                             sizeof(palette_ram));
  hash = bios_swi_hash_bytes(hash, (const u8 *)oam_ram, sizeof(oam_ram));
  return hash;
}

static void install_bios_swi_fixture(rv32im_bios_fixture fixture)
{
  static const u8 lz_literals[12] =
    { 0x10u, 0x21u, 0x32u, 0x43u, 0x54u, 0x65u,
      0x76u, 0x87u, 0x98u, 0xa9u, 0xbau, 0xcbu };
  static const u8 diff8[8] =
    { 0x10u, 0x01u, 0x02u, 0xffu, 0x04u, 0xfcu, 0x08u, 0x80u };

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
      for (u32 i = 0; i < 8u; i++)
        ewram[5u + i] = lz_literals[i];
      ewram[13] = 0u;
      for (u32 i = 0; i < 4u; i++)
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
      for (u32 i = 0; i < sizeof(diff8); i++)
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

static bool run_bios_swi_case(const rv32im_bios_swi_case *item)
{
  u32 state_hash;
  u32 memory_hash;
  bool passed;

  std::memset(g_rom, 0, sizeof(g_rom));
  std::memcpy(g_bios, g_open_bios, sizeof(g_bios));
  g_rom_size = sizeof(g_rom);
  store32(g_rom, RV32IM_BIOS_SWI_PC - ROM_BASE,
          0xef000000u | (item->swi << 16));
  store32(g_rom, RV32IM_BIOS_SWI_PC - ROM_BASE + 4u, 0xeafffffeu);

  clear_runtime_memory();
  install_bios_swi_fixture(item->fixture);
  init_memory_map();
  init_cpu();
  reg[0] = item->r0;
  reg[1] = item->r1;
  reg[2] = item->r2;
  reg[3] = item->r3;
  reg[REG_SP] = 0x03007f00u;
  reg[REG_PC] = RV32IM_BIOS_SWI_PC;
  reg[REG_CPSR] = 0x0000001fu;
  reg[CPU_MODE] = MODE_SYSTEM;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
  idle_loop_target_pc = RV32IM_BIOS_SWI_PC + 4u;
  g_update_calls = 0u;
  g_update_last_cycles = (s32)0x7fffffffu;
  g_read32_calls = 0u;
  g_write32_calls = 0u;

  execute_arm(500000u);
  state_hash = bios_swi_state_hash();
  memory_hash = bios_swi_memory_hash();
  passed = g_update_calls == 1u && g_update_last_cycles <= 0 &&
           reg[REG_PC] == RV32IM_BIOS_SWI_PC + 4u;

  std::printf("result=%s command=bios-swi case=%s backend=interp "
              "swi=0x%08x state_hash=0x%08x memory_hash=0x%08x "
              "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x "
              "pc=0x%08x cpsr=0x%08x update_calls=%u "
              "update_cycles=0x%08x harness_mode=cpu_cc_interpreter "
              "reason=%s\n",
              passed ? "PASS" : "FAIL", item->name, item->swi,
              state_hash, memory_hash, reg[0], reg[1], reg[2], reg[3],
              reg[REG_PC], reg[REG_CPSR], g_update_calls,
              (u32)g_update_last_cycles,
              passed ? "real_bios_swi_executed" :
                       "real_bios_swi_contract_failed");
  return passed;
}

static bool run_bios_swi_cases(void)
{
  for (u32 i = 0; i < RV32IM_BIOS_SWI_CASE_COUNT; i++)
  {
    if (!run_bios_swi_case(&rv32im_bios_swi_cases[i]))
      return false;
  }
  std::printf("result=PASS command=bios-swi case=all backend=interp "
              "cases=%u harness_mode=cpu_cc_interpreter "
              "reason=real_bios_swi_cases_complete\n",
              (u32)RV32IM_BIOS_SWI_CASE_COUNT);
  return true;
}
#endif

int main(void)
{
#if defined(RV32IM_BIOS_SWI_ONLY)
  if (!load_open_gba_bios())
  {
    std::printf("result=FAIL command=bios-swi reason=bios_load_failed "
                "path=%s\n", OPEN_GBA_BIOS);
    return 2;
  }
  return run_bios_swi_cases() ? 0 : 1;
#elif defined(RV32IM_FRONTEND_CONTROL_ONLY)
  return run_frontend_control_cases() ? 0 : 1;
#elif defined(RV32IM_MAPPED_ALU_ONLY)
  return run_mapped_alu_interpreter_cases() ? 0 : 1;
#else
  if (!load_rom())
  {
    std::printf("result=FAIL command=armwrestler-interp reason=rom_load_failed "
                "path=%s\n", ARMWRESTLER_ROM);
    return 2;
  }

  patch_loaded_rom();

  for (u32 i = 0; i < ARMWRESTLER_ARM_TESTS; i++)
  {
    if (!run_armwrestler_test(g_arm_test_ids[i], g_arm_test_results[i]))
    {
      print_aggregate_summary("FAIL", "armwrestler_arm_all_interpreter_failed");
      return 1;
    }
  }
  for (u32 i = 0; i < ARMWRESTLER_THUMB_TESTS; i++)
  {
    if (!run_thumbwrestler_test(g_thumb_test_ids[i], g_thumb_test_results[i]))
    {
      print_aggregate_summary("FAIL", "armwrestler_all_interpreter_failed");
      return 1;
    }
  }

  if (g_total_observed_results != ARMWRESTLER_TOTAL_RESULTS ||
      g_total_failure_mask != 0)
  {
    print_aggregate_summary("FAIL", "armwrestler_all_interpreter_mismatch");
    return 1;
  }

  print_aggregate_summary("PASS", "armwrestler_all_interpreter_passed");
  return 0;
#endif
}
