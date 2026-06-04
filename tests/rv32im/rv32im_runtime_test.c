#include "riscv_runtime_test_shim.h"
#include "riscv_emit.h"

typedef unsigned int usize;

#define SYS_WRITE 64
#define SYS_EXIT 93
#define SYS_MMAP 222
#define SYS_RISCV_FLUSH_ICACHE 259

#define STDOUT_FD 1

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32

#define BLOCK_START_PC 0x08000000u
#define BLOCK_END_PC 0x08000004u
#define BLOCK_CYCLES 7u
#define ADD_R2_R0_R1 0xe0802001u
#define BRANCH_START_PC 0x08000100u
#define BRANCH_TARGET_PC 0x0800010cu
#define BRANCH_CYCLES 9u
#define BRANCH_B_PLUS_12 0xea000001u
#define BRANCH_BLOCK_OFFSET 512u
#define LOAD_START_PC 0x08000200u
#define LOAD_WORD_PC LOAD_START_PC
#define LOAD_BYTE_PC (LOAD_START_PC + 4u)
#define LOAD_END_PC (LOAD_START_PC + 8u)
#define LOAD_WORD_BASE_CYCLES 7u
#define LOAD_BYTE_BASE_CYCLES 5u
#define LOAD_TOTAL_CYCLES \
  ((LOAD_WORD_BASE_CYCLES + 2u) + (LOAD_BYTE_BASE_CYCLES + 2u))
#define LOAD_LDR_R4_R3_0X24 0xe5934024u
#define LOAD_LDRB_R5_R3_0X25 0xe5d35025u
#define LOAD_BASE_ADDR 0x02000040u
#define LOAD_WORD_ADDR (LOAD_BASE_ADDR + 0x24u)
#define LOAD_BYTE_ADDR (LOAD_BASE_ADDR + 0x25u)
#define LOAD_WORD_VALUE 0xa1b2c3d4u
#define LOAD_BYTE_VALUE 0x7eu
#define LOAD_BLOCK_OFFSET 1024u
#define STORE_WORD_START_PC 0x08000300u
#define STORE_BYTE_START_PC 0x08000340u
#define STORE_PC_START_PC 0x08000380u
#define STORE_WORD_END_PC (STORE_WORD_START_PC + 4u)
#define STORE_BYTE_END_PC (STORE_BYTE_START_PC + 4u)
#define STORE_PC_END_PC (STORE_PC_START_PC + 4u)
#define STORE_BASE_CYCLES 6u
#define STORE_TOTAL_CYCLES (STORE_BASE_CYCLES + 1u)
#define STORE_STR_R6_R3_0X28 0xe5836028u
#define STORE_STRB_R6_R3_0X29 0xe5c36029u
#define STORE_STR_R15_R3_0X2C 0xe583f02cu
#define STORE_BASE_ADDR 0x02000100u
#define STORE_WORD_ADDR (STORE_BASE_ADDR + 0x28u)
#define STORE_BYTE_ADDR (STORE_BASE_ADDR + 0x29u)
#define STORE_PC_ADDR (STORE_BASE_ADDR + 0x2cu)
#define STORE_VALUE 0x13579bdfu
#define STORE_PC_VALUE (STORE_PC_START_PC + 12u)
#define STORE_WORD_BLOCK_OFFSET 1536u
#define STORE_BYTE_BLOCK_OFFSET 2048u
#define STORE_PC_BLOCK_OFFSET 2560u
#define BX_START_PC 0x08000400u
#define BX_CYCLES 8u
#define BX_R7 0xe12fff17u
#define BX_ARM_TARGET 0x02001000u
#define BX_THUMB_TARGET_RAW 0x02001001u
#define BX_THUMB_TARGET_PC (BX_THUMB_TARGET_RAW & ~1u)
#define BX_BLOCK_OFFSET 3072u
#define CPSR_T_BIT 0x20u
#define HALF_LDRH_R4_R3_0X24 0xe1d342b4u
#define HALF_STRH_R4_R3_0X24 0xe1c342b4u
#define HALF_LDRSB_R4_R3_0X24 0xe1d342d4u
#define HALF_LDRSH_R4_R3_0X24 0xe1d342f4u
#define FRAME_COMPLETE 0x80000000u

u32 reg[REG_MAX];
u32 rom_cache_watermark;
u32 gamepak_sticky_bit[1024 / 32];

static u8 *g_lookup_entry;
static u8 *g_data_entry;
static u8 *g_branch_entry;
static u8 *g_load_entry;
static u8 *g_store_word_entry;
static u8 *g_store_byte_entry;
static u8 *g_store_pc_entry;
static u8 *g_bx_entry;
static u32 g_lookup_calls;
static u32 g_lookup_pc;
static u32 g_update_calls;
static s32 g_update_cycles;
static u32 g_execute_calls;
static u32 g_execute_cycles;
static u32 g_execute_pc;
static u32 g_read32_calls;
static u32 g_read32_addr;
static u32 g_read32_pc;
static u32 g_read8_calls;
static u32 g_read8_addr;
static u32 g_read8_pc;
static u32 g_write32_calls;
static u32 g_write32_addr;
static u32 g_write32_value;
static u32 g_write32_pc;
static u32 g_write8_calls;
static u32 g_write8_addr;
static u32 g_write8_value;
static u32 g_write8_pc;
static u32 g_flush_calls;
static u32 g_irq_check_calls;
static cpu_alert_type g_store_alert;

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

static usize cstr_len(const char *text)
{
  usize len = 0;
  while (text[len])
    len++;
  return len;
}

static void put_raw(const char *text)
{
  syscall3(SYS_WRITE, STDOUT_FD, (long)text, (long)cstr_len(text));
}

static void put_chr(char ch)
{
  syscall3(SYS_WRITE, STDOUT_FD, (long)&ch, 1);
}

static char hex_digit(u32 value)
{
  value &= 0xf;
  return (char)(value < 10 ? ('0' + value) : ('a' + value - 10));
}

static void put_u32_hex(u32 value)
{
  int shift;
  put_raw("0x");
  for (shift = 28; shift >= 0; shift -= 4)
    put_chr(hex_digit(value >> (u32)shift));
}

static void put_u32_dec(u32 value)
{
  char buf[10];
  usize pos = 0;

  if (!value)
  {
    put_chr('0');
    return;
  }

  while (value)
  {
    buf[pos++] = (char)('0' + (value % 10));
    value /= 10;
  }

  while (pos)
    put_chr(buf[--pos]);
}

static void fail_u32(const char *test_name, const char *field,
                     u32 got, u32 expected)
{
  put_raw("result=FAIL command=runtime test=");
  put_raw(test_name);
  put_raw(" field=");
  put_raw(field);
  put_raw(" got=");
  put_u32_hex(got);
  put_raw(" expected=");
  put_u32_hex(expected);
  put_raw("\n");
  sys_exit(1);
}

static void *map_exec_page(void)
{
  long ret = syscall6(SYS_MMAP, 0, 4096,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if ((u32)ret >= 0xfffff000u)
    return (void *)0;
  return (void *)ret;
}

static void reset_runtime_observations(u32 pc)
{
  unsigned i;

  for (i = 0; i < REG_MAX; i++)
    reg[i] = 0;
  for (i = 0; i < (1024 / 32); i++)
    gamepak_sticky_bit[i] = 0xffffffffu;

  reg[REG_PC] = pc;
  reg[REG_CPSR] = 0;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
  g_lookup_calls = 0;
  g_lookup_pc = 0;
  g_update_calls = 0;
  g_update_cycles = 0x7fffffff;
  g_execute_calls = 0;
  g_execute_cycles = 0;
  g_execute_pc = 0;
  g_read32_calls = 0;
  g_read32_addr = 0;
  g_read32_pc = 0;
  g_read8_calls = 0;
  g_read8_addr = 0;
  g_read8_pc = 0;
  g_write32_calls = 0;
  g_write32_addr = 0;
  g_write32_value = 0;
  g_write32_pc = 0;
  g_write8_calls = 0;
  g_write8_addr = 0;
  g_write8_value = 0;
  g_write8_pc = 0;
  g_flush_calls = 0;
  g_irq_check_calls = 0;
  g_store_alert = CPU_ALERT_NONE;
}

static u32 build_data_block(u8 *code)
{
  u8 *translation_ptr = code;
  riscv_jit_block_meta *meta;
  u32 code_bytes;

  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_data_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       ADD_R2_R0_R1, BLOCK_CYCLES))
  {
    put_raw("result=FAIL command=runtime reason=native_emit_rejected\n");
    sys_exit(1);
  }

  riscv_emit_block_finalize(meta, &translation_ptr, BLOCK_START_PC,
                            BLOCK_END_PC, false);
  code_bytes = (u32)(translation_ptr - code);
  syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code, (long)(code + code_bytes), 0);
  return code_bytes;
}

static u32 build_branch_block(u8 *code)
{
  u8 *translation_ptr = code;
  riscv_jit_block_meta *meta;
  u32 code_bytes;

  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_branch_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_b(&translation_ptr, meta, BRANCH_B_PLUS_12,
                               BRANCH_START_PC, BRANCH_CYCLES))
  {
    put_raw("result=FAIL command=runtime reason=branch_emit_rejected\n");
    sys_exit(1);
  }

  riscv_emit_block_finalize(meta, &translation_ptr, BRANCH_START_PC,
                            BRANCH_START_PC + 4u, false);
  code_bytes = (u32)(translation_ptr - code);
  syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code, (long)(code + code_bytes), 0);
  return code_bytes;
}

static u32 build_load_block(u8 *code)
{
  u8 *translation_ptr = code;
  riscv_jit_block_meta *meta;
  u32 code_bytes;

  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_load_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           LOAD_LDR_R4_R3_0X24,
                                           LOAD_WORD_PC,
                                           LOAD_WORD_BASE_CYCLES))
  {
    put_raw("result=FAIL command=runtime reason=load_word_emit_rejected\n");
    sys_exit(1);
  }

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           LOAD_LDRB_R5_R3_0X25,
                                           LOAD_BYTE_PC,
                                           LOAD_BYTE_BASE_CYCLES))
  {
    put_raw("result=FAIL command=runtime reason=load_byte_emit_rejected\n");
    sys_exit(1);
  }

  riscv_emit_block_finalize(meta, &translation_ptr, LOAD_START_PC,
                            LOAD_END_PC, false);
  code_bytes = (u32)(translation_ptr - code);
  syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code, (long)(code + code_bytes), 0);
  return code_bytes;
}

static u32 build_store_block(u8 *code, u32 opcode, u32 pc, u8 **entry_out)
{
  u8 *translation_ptr = code;
  riscv_jit_block_meta *meta;
  u32 code_bytes;

  riscv_emit_block_prologue(&translation_ptr, &meta);
  *entry_out = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           opcode, pc, STORE_BASE_CYCLES))
  {
    put_raw("result=FAIL command=runtime reason=store_emit_rejected\n");
    sys_exit(1);
  }

  riscv_emit_block_finalize(meta, &translation_ptr, pc, pc + 4u, false);
  code_bytes = (u32)(translation_ptr - code);
  syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code, (long)(code + code_bytes), 0);
  return code_bytes;
}

static u32 build_bx_block(u8 *code)
{
  u8 *translation_ptr = code;
  riscv_jit_block_meta *meta;
  u32 code_bytes;

  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_bx_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_bx(&translation_ptr, meta, BX_R7,
                                BX_START_PC, BX_CYCLES))
  {
    put_raw("result=FAIL command=runtime reason=bx_emit_rejected\n");
    sys_exit(1);
  }

  riscv_emit_block_finalize(meta, &translation_ptr, BX_START_PC,
                            BX_START_PC + 4u, false);
  code_bytes = (u32)(translation_ptr - code);
  syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code, (long)(code + code_bytes), 0);
  return code_bytes;
}

static void expect_halfword_transfers_rejected(u8 *code)
{
  static const u32 rejected_opcodes[] =
  {
    HALF_LDRH_R4_R3_0X24,
    HALF_STRH_R4_R3_0X24,
    HALF_LDRSB_R4_R3_0X24,
    HALF_LDRSH_R4_R3_0X24
  };
  unsigned i;

  for (i = 0; i < sizeof(rejected_opcodes) / sizeof(rejected_opcodes[0]); i++)
  {
    u8 *translation_ptr = code;
    riscv_jit_block_meta *meta;

    riscv_emit_block_prologue(&translation_ptr, &meta);
    if (riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                            rejected_opcodes[i],
                                            LOAD_START_PC,
                                            LOAD_WORD_BASE_CYCLES))
    {
      fail_u32("halfword_reject", "accepted", rejected_opcodes[i], 0);
    }
  }
}

static void expect_stickybits_cleared(const char *test_name)
{
  unsigned i;

  for (i = 0; i < (1024 / 32); i++)
  {
    if (gamepak_sticky_bit[i])
      fail_u32(test_name, "sticky", gamepak_sticky_bit[i], 0);
  }
}

static void run_cycle_boundary_case(void)
{
  const u32 r0 = 0x12345678u;
  const u32 r1 = 0x01020304u;
  const u32 expected_sum = r0 + r1;

  reset_runtime_observations(BLOCK_START_PC);
  g_lookup_entry = g_data_entry;
  reg[0] = r0;
  reg[1] = r1;

  execute_arm_translate_internal(BLOCK_CYCLES, &reg[0]);

  if (reg[2] != expected_sum)
    fail_u32("cycle_boundary", "r2", reg[2], expected_sum);
  if (reg[REG_PC] != BLOCK_END_PC)
    fail_u32("cycle_boundary", "pc", reg[REG_PC], BLOCK_END_PC);
  if (g_lookup_calls != 1)
    fail_u32("cycle_boundary", "lookup_calls", g_lookup_calls, 1);
  if (g_lookup_pc != BLOCK_START_PC)
    fail_u32("cycle_boundary", "lookup_pc", g_lookup_pc, BLOCK_START_PC);
  if (g_update_calls != 1)
    fail_u32("cycle_boundary", "update_calls", g_update_calls, 1);
  if ((u32)g_update_cycles != 0)
    fail_u32("cycle_boundary", "update_cycles", (u32)g_update_cycles, 0);
  if (g_execute_calls != 0)
    fail_u32("cycle_boundary", "execute_calls", g_execute_calls, 0);
  expect_stickybits_cleared("cycle_boundary");
}

static void run_remaining_cycles_case(void)
{
  const u32 r0 = 0x20000011u;
  const u32 r1 = 0x00000022u;
  const u32 extra_cycles = 5u;
  const u32 expected_sum = r0 + r1;

  reset_runtime_observations(BLOCK_START_PC);
  g_lookup_entry = g_data_entry;
  reg[0] = r0;
  reg[1] = r1;

  execute_arm_translate_internal(BLOCK_CYCLES + extra_cycles, &reg[0]);

  if (reg[2] != expected_sum)
    fail_u32("remaining_cycles", "r2", reg[2], expected_sum);
  if (reg[REG_PC] != BLOCK_END_PC)
    fail_u32("remaining_cycles", "pc", reg[REG_PC], BLOCK_END_PC);
  if (g_update_calls != 0)
    fail_u32("remaining_cycles", "update_calls", g_update_calls, 0);
  if (g_execute_calls != 1)
    fail_u32("remaining_cycles", "execute_calls", g_execute_calls, 1);
  if (g_execute_cycles != extra_cycles)
    fail_u32("remaining_cycles", "execute_cycles",
             g_execute_cycles, extra_cycles);
  if (g_execute_pc != BLOCK_END_PC)
    fail_u32("remaining_cycles", "execute_pc", g_execute_pc, BLOCK_END_PC);
  expect_stickybits_cleared("remaining_cycles");
}

static void run_branch_boundary_case(void)
{
  reset_runtime_observations(BRANCH_START_PC);
  g_lookup_entry = g_branch_entry;

  execute_arm_translate_internal(BRANCH_CYCLES, &reg[0]);

  if (reg[REG_PC] != BRANCH_TARGET_PC)
    fail_u32("branch_boundary", "pc", reg[REG_PC], BRANCH_TARGET_PC);
  if (g_lookup_calls != 1)
    fail_u32("branch_boundary", "lookup_calls", g_lookup_calls, 1);
  if (g_lookup_pc != BRANCH_START_PC)
    fail_u32("branch_boundary", "lookup_pc", g_lookup_pc, BRANCH_START_PC);
  if (g_update_calls != 1)
    fail_u32("branch_boundary", "update_calls", g_update_calls, 1);
  if ((u32)g_update_cycles != 0)
    fail_u32("branch_boundary", "update_cycles", (u32)g_update_cycles, 0);
  if (g_execute_calls != 0)
    fail_u32("branch_boundary", "execute_calls", g_execute_calls, 0);
  expect_stickybits_cleared("branch_boundary");
}

static void run_branch_remaining_cycles_case(void)
{
  const u32 extra_cycles = 3u;

  reset_runtime_observations(BRANCH_START_PC);
  g_lookup_entry = g_branch_entry;

  execute_arm_translate_internal(BRANCH_CYCLES + extra_cycles, &reg[0]);

  if (reg[REG_PC] != BRANCH_TARGET_PC)
    fail_u32("branch_remaining", "pc", reg[REG_PC], BRANCH_TARGET_PC);
  if (g_update_calls != 0)
    fail_u32("branch_remaining", "update_calls", g_update_calls, 0);
  if (g_execute_calls != 1)
    fail_u32("branch_remaining", "execute_calls", g_execute_calls, 1);
  if (g_execute_cycles != extra_cycles)
    fail_u32("branch_remaining", "execute_cycles",
             g_execute_cycles, extra_cycles);
  if (g_execute_pc != BRANCH_TARGET_PC)
    fail_u32("branch_remaining", "execute_pc",
             g_execute_pc, BRANCH_TARGET_PC);
  expect_stickybits_cleared("branch_remaining");
}

static void run_bx_arm_remaining_cycles_case(void)
{
  const u32 extra_cycles = 6u;

  reset_runtime_observations(BX_START_PC);
  g_lookup_entry = g_bx_entry;
  reg[7] = BX_ARM_TARGET;

  execute_arm_translate_internal(BX_CYCLES + extra_cycles, &reg[0]);

  if (reg[REG_PC] != BX_ARM_TARGET)
    fail_u32("bx_arm_remaining", "pc", reg[REG_PC], BX_ARM_TARGET);
  if (reg[REG_CPSR] & CPSR_T_BIT)
    fail_u32("bx_arm_remaining", "cpsr_t", reg[REG_CPSR] & CPSR_T_BIT, 0);
  if (g_update_calls != 0)
    fail_u32("bx_arm_remaining", "update_calls", g_update_calls, 0);
  if (g_execute_calls != 1)
    fail_u32("bx_arm_remaining", "execute_calls", g_execute_calls, 1);
  if (g_execute_cycles != extra_cycles)
    fail_u32("bx_arm_remaining", "execute_cycles",
             g_execute_cycles, extra_cycles);
  if (g_execute_pc != BX_ARM_TARGET)
    fail_u32("bx_arm_remaining", "execute_pc",
             g_execute_pc, BX_ARM_TARGET);
  expect_stickybits_cleared("bx_arm_remaining");
}

static void run_bx_thumb_boundary_case(void)
{
  reset_runtime_observations(BX_START_PC);
  g_lookup_entry = g_bx_entry;
  reg[7] = BX_THUMB_TARGET_RAW;

  execute_arm_translate_internal(BX_CYCLES, &reg[0]);

  if (reg[REG_PC] != BX_THUMB_TARGET_PC)
    fail_u32("bx_thumb_boundary", "pc",
             reg[REG_PC], BX_THUMB_TARGET_PC);
  if ((reg[REG_CPSR] & CPSR_T_BIT) != CPSR_T_BIT)
    fail_u32("bx_thumb_boundary", "cpsr_t",
             reg[REG_CPSR] & CPSR_T_BIT, CPSR_T_BIT);
  if (g_update_calls != 1)
    fail_u32("bx_thumb_boundary", "update_calls", g_update_calls, 1);
  if ((u32)g_update_cycles != 0)
    fail_u32("bx_thumb_boundary", "update_cycles", (u32)g_update_cycles, 0);
  if (g_execute_calls != 0)
    fail_u32("bx_thumb_boundary", "execute_calls", g_execute_calls, 0);
  expect_stickybits_cleared("bx_thumb_boundary");
}

static void expect_load_helpers(const char *test_name)
{
  if (g_read32_calls != 1)
    fail_u32(test_name, "read32_calls", g_read32_calls, 1);
  if (g_read32_addr != LOAD_WORD_ADDR)
    fail_u32(test_name, "read32_addr", g_read32_addr, LOAD_WORD_ADDR);
  if (g_read32_pc != LOAD_WORD_PC)
    fail_u32(test_name, "read32_pc", g_read32_pc, LOAD_WORD_PC);
  if (g_read8_calls != 1)
    fail_u32(test_name, "read8_calls", g_read8_calls, 1);
  if (g_read8_addr != LOAD_BYTE_ADDR)
    fail_u32(test_name, "read8_addr", g_read8_addr, LOAD_BYTE_ADDR);
  if (g_read8_pc != LOAD_BYTE_PC)
    fail_u32(test_name, "read8_pc", g_read8_pc, LOAD_BYTE_PC);
}

static void run_load_boundary_case(void)
{
  reset_runtime_observations(LOAD_START_PC);
  g_lookup_entry = g_load_entry;
  reg[3] = LOAD_BASE_ADDR;

  execute_arm_translate_internal(LOAD_TOTAL_CYCLES, &reg[0]);

  if (reg[4] != LOAD_WORD_VALUE)
    fail_u32("load_boundary", "r4", reg[4], LOAD_WORD_VALUE);
  if (reg[5] != LOAD_BYTE_VALUE)
    fail_u32("load_boundary", "r5", reg[5], LOAD_BYTE_VALUE);
  if (reg[REG_PC] != LOAD_END_PC)
    fail_u32("load_boundary", "pc", reg[REG_PC], LOAD_END_PC);
  if (g_lookup_calls != 1)
    fail_u32("load_boundary", "lookup_calls", g_lookup_calls, 1);
  if (g_lookup_pc != LOAD_START_PC)
    fail_u32("load_boundary", "lookup_pc", g_lookup_pc, LOAD_START_PC);
  if (g_update_calls != 1)
    fail_u32("load_boundary", "update_calls", g_update_calls, 1);
  if ((u32)g_update_cycles != 0)
    fail_u32("load_boundary", "update_cycles", (u32)g_update_cycles, 0);
  if (g_execute_calls != 0)
    fail_u32("load_boundary", "execute_calls", g_execute_calls, 0);
  expect_load_helpers("load_boundary");
  expect_stickybits_cleared("load_boundary");
}

static void run_load_remaining_cycles_case(void)
{
  const u32 extra_cycles = 4u;

  reset_runtime_observations(LOAD_START_PC);
  g_lookup_entry = g_load_entry;
  reg[3] = LOAD_BASE_ADDR;

  execute_arm_translate_internal(LOAD_TOTAL_CYCLES + extra_cycles, &reg[0]);

  if (reg[4] != LOAD_WORD_VALUE)
    fail_u32("load_remaining", "r4", reg[4], LOAD_WORD_VALUE);
  if (reg[5] != LOAD_BYTE_VALUE)
    fail_u32("load_remaining", "r5", reg[5], LOAD_BYTE_VALUE);
  if (reg[REG_PC] != LOAD_END_PC)
    fail_u32("load_remaining", "pc", reg[REG_PC], LOAD_END_PC);
  if (g_update_calls != 0)
    fail_u32("load_remaining", "update_calls", g_update_calls, 0);
  if (g_execute_calls != 1)
    fail_u32("load_remaining", "execute_calls", g_execute_calls, 1);
  if (g_execute_cycles != extra_cycles)
    fail_u32("load_remaining", "execute_cycles",
             g_execute_cycles, extra_cycles);
  if (g_execute_pc != LOAD_END_PC)
    fail_u32("load_remaining", "execute_pc", g_execute_pc, LOAD_END_PC);
  expect_load_helpers("load_remaining");
  expect_stickybits_cleared("load_remaining");
}

static void expect_store_word(const char *test_name, u32 pc)
{
  if (g_write32_calls != 1)
    fail_u32(test_name, "write32_calls", g_write32_calls, 1);
  if (g_write32_addr != STORE_WORD_ADDR)
    fail_u32(test_name, "write32_addr", g_write32_addr, STORE_WORD_ADDR);
  if (g_write32_value != STORE_VALUE)
    fail_u32(test_name, "write32_value", g_write32_value, STORE_VALUE);
  if (g_write32_pc != pc)
    fail_u32(test_name, "write32_pc", g_write32_pc, pc);
}

static void run_store_word_boundary_case(void)
{
  reset_runtime_observations(STORE_WORD_START_PC);
  g_lookup_entry = g_store_word_entry;
  reg[3] = STORE_BASE_ADDR;
  reg[6] = STORE_VALUE;

  execute_arm_translate_internal(STORE_TOTAL_CYCLES, &reg[0]);

  expect_store_word("store_word_boundary", STORE_WORD_END_PC);
  if (reg[REG_PC] != STORE_WORD_END_PC)
    fail_u32("store_word_boundary", "pc", reg[REG_PC], STORE_WORD_END_PC);
  if (g_update_calls != 1)
    fail_u32("store_word_boundary", "update_calls", g_update_calls, 1);
  if ((u32)g_update_cycles != 0)
    fail_u32("store_word_boundary", "update_cycles", (u32)g_update_cycles, 0);
  if (g_execute_calls != 0)
    fail_u32("store_word_boundary", "execute_calls", g_execute_calls, 0);
  if (g_flush_calls != 0)
    fail_u32("store_word_boundary", "flush_calls", g_flush_calls, 0);
  if (g_irq_check_calls != 0)
    fail_u32("store_word_boundary", "irq_calls", g_irq_check_calls, 0);
  expect_stickybits_cleared("store_word_boundary");
}

static void run_store_word_remaining_cycles_case(void)
{
  const u32 extra_cycles = 5u;

  reset_runtime_observations(STORE_WORD_START_PC);
  g_lookup_entry = g_store_word_entry;
  reg[3] = STORE_BASE_ADDR;
  reg[6] = STORE_VALUE;

  execute_arm_translate_internal(STORE_TOTAL_CYCLES + extra_cycles, &reg[0]);

  expect_store_word("store_word_remaining", STORE_WORD_END_PC);
  if (reg[REG_PC] != STORE_WORD_END_PC)
    fail_u32("store_word_remaining", "pc", reg[REG_PC], STORE_WORD_END_PC);
  if (g_update_calls != 0)
    fail_u32("store_word_remaining", "update_calls", g_update_calls, 0);
  if (g_execute_calls != 1)
    fail_u32("store_word_remaining", "execute_calls", g_execute_calls, 1);
  if (g_execute_cycles != extra_cycles)
    fail_u32("store_word_remaining", "execute_cycles",
             g_execute_cycles, extra_cycles);
  if (g_execute_pc != STORE_WORD_END_PC)
    fail_u32("store_word_remaining", "execute_pc",
             g_execute_pc, STORE_WORD_END_PC);
  expect_stickybits_cleared("store_word_remaining");
}

static void run_store_pc_value_case(void)
{
  const u32 extra_cycles = 2u;

  reset_runtime_observations(STORE_PC_START_PC);
  g_lookup_entry = g_store_pc_entry;
  reg[3] = STORE_BASE_ADDR;

  execute_arm_translate_internal(STORE_TOTAL_CYCLES + extra_cycles, &reg[0]);

  if (g_write32_calls != 1)
    fail_u32("store_pc_value", "write32_calls", g_write32_calls, 1);
  if (g_write32_addr != STORE_PC_ADDR)
    fail_u32("store_pc_value", "write32_addr", g_write32_addr, STORE_PC_ADDR);
  if (g_write32_value != STORE_PC_VALUE)
    fail_u32("store_pc_value", "write32_value",
             g_write32_value, STORE_PC_VALUE);
  if (g_write32_pc != STORE_PC_END_PC)
    fail_u32("store_pc_value", "write32_pc",
             g_write32_pc, STORE_PC_END_PC);
  if (reg[REG_PC] != STORE_PC_END_PC)
    fail_u32("store_pc_value", "pc", reg[REG_PC], STORE_PC_END_PC);
  if (g_execute_calls != 1)
    fail_u32("store_pc_value", "execute_calls", g_execute_calls, 1);
  if (g_execute_cycles != extra_cycles)
    fail_u32("store_pc_value", "execute_cycles",
             g_execute_cycles, extra_cycles);
  expect_stickybits_cleared("store_pc_value");
}

static void run_store_byte_remaining_cycles_case(void)
{
  const u32 extra_cycles = 3u;

  reset_runtime_observations(STORE_BYTE_START_PC);
  g_lookup_entry = g_store_byte_entry;
  reg[3] = STORE_BASE_ADDR;
  reg[6] = STORE_VALUE;

  execute_arm_translate_internal(STORE_TOTAL_CYCLES + extra_cycles, &reg[0]);

  if (g_write8_calls != 1)
    fail_u32("store_byte_remaining", "write8_calls", g_write8_calls, 1);
  if (g_write8_addr != STORE_BYTE_ADDR)
    fail_u32("store_byte_remaining", "write8_addr",
             g_write8_addr, STORE_BYTE_ADDR);
  if (g_write8_value != (STORE_VALUE & 0xffu))
    fail_u32("store_byte_remaining", "write8_value",
             g_write8_value, STORE_VALUE & 0xffu);
  if (g_write8_pc != STORE_BYTE_END_PC)
    fail_u32("store_byte_remaining", "write8_pc",
             g_write8_pc, STORE_BYTE_END_PC);
  if (reg[REG_PC] != STORE_BYTE_END_PC)
    fail_u32("store_byte_remaining", "pc", reg[REG_PC], STORE_BYTE_END_PC);
  if (g_execute_calls != 1)
    fail_u32("store_byte_remaining", "execute_calls", g_execute_calls, 1);
  if (g_execute_cycles != extra_cycles)
    fail_u32("store_byte_remaining", "execute_cycles",
             g_execute_cycles, extra_cycles);
  expect_stickybits_cleared("store_byte_remaining");
}

static void run_store_smc_irq_alert_case(void)
{
  const u32 extra_cycles = 4u;

  reset_runtime_observations(STORE_WORD_START_PC);
  g_lookup_entry = g_store_word_entry;
  g_store_alert = CPU_ALERT_SMC | CPU_ALERT_IRQ;
  reg[3] = STORE_BASE_ADDR;
  reg[6] = STORE_VALUE;

  execute_arm_translate_internal(STORE_TOTAL_CYCLES + extra_cycles, &reg[0]);

  expect_store_word("store_smc_irq", STORE_WORD_END_PC);
  if (g_flush_calls != 1)
    fail_u32("store_smc_irq", "flush_calls", g_flush_calls, 1);
  if (g_irq_check_calls != 1)
    fail_u32("store_smc_irq", "irq_calls", g_irq_check_calls, 1);
  if (g_update_calls != 0)
    fail_u32("store_smc_irq", "update_calls", g_update_calls, 0);
  if (g_execute_calls != 1)
    fail_u32("store_smc_irq", "execute_calls", g_execute_calls, 1);
  if (g_execute_cycles != extra_cycles)
    fail_u32("store_smc_irq", "execute_cycles",
             g_execute_cycles, extra_cycles);
  expect_stickybits_cleared("store_smc_irq");
}

static void run_store_halt_alert_case(void)
{
  const u32 extra_cycles = 2u;

  reset_runtime_observations(STORE_WORD_START_PC);
  g_lookup_entry = g_store_word_entry;
  g_store_alert = CPU_ALERT_HALT;
  reg[3] = STORE_BASE_ADDR;
  reg[6] = STORE_VALUE;

  execute_arm_translate_internal(STORE_TOTAL_CYCLES + extra_cycles, &reg[0]);

  expect_store_word("store_halt", STORE_WORD_END_PC);
  if (g_update_calls != 1)
    fail_u32("store_halt", "update_calls", g_update_calls, 1);
  if ((u32)g_update_cycles != extra_cycles)
    fail_u32("store_halt", "update_cycles",
             (u32)g_update_cycles, extra_cycles);
  if (g_execute_calls != 0)
    fail_u32("store_halt", "execute_calls", g_execute_calls, 0);
  expect_stickybits_cleared("store_halt");
}

void execute_arm(u32 cycles)
{
  g_execute_calls++;
  g_execute_cycles = cycles;
  g_execute_pc = reg[REG_PC];
}

u32 function_cc read_memory32(u32 address)
{
  g_read32_calls++;
  g_read32_addr = address;
  g_read32_pc = reg[REG_PC];
  return LOAD_WORD_VALUE;
}

u32 function_cc read_memory8(u32 address)
{
  g_read8_calls++;
  g_read8_addr = address;
  g_read8_pc = reg[REG_PC];
  return LOAD_BYTE_VALUE;
}

cpu_alert_type function_cc write_memory32(u32 address, u32 value)
{
  g_write32_calls++;
  g_write32_addr = address;
  g_write32_value = value;
  g_write32_pc = reg[REG_PC];
  if (g_store_alert & CPU_ALERT_HALT)
    reg[CPU_HALT_STATE] = CPU_HALT;
  return g_store_alert;
}

cpu_alert_type function_cc write_memory8(u32 address, u8 value)
{
  g_write8_calls++;
  g_write8_addr = address;
  g_write8_value = value;
  g_write8_pc = reg[REG_PC];
  if (g_store_alert & CPU_ALERT_HALT)
    reg[CPU_HALT_STATE] = CPU_HALT;
  return g_store_alert;
}

u32 check_and_raise_interrupts(void)
{
  g_irq_check_calls++;
  return 0;
}

void flush_translation_cache_ram(void)
{
  g_flush_calls++;
}

u32 function_cc update_gba(int remaining_cycles)
{
  g_update_calls++;
  g_update_cycles = remaining_cycles;
  return FRAME_COMPLETE;
}

u8 function_cc *block_lookup_address_arm(u32 pc)
{
  g_lookup_calls++;
  g_lookup_pc = pc;
  return g_lookup_entry;
}

u8 function_cc *block_lookup_address_thumb(u32 pc)
{
  (void)pc;
  return (u8 *)0;
}

void init_bios_hooks(void)
{
}

void _start(void)
{
  u8 *code = (u8 *)map_exec_page();
  u32 data_code_bytes;
  u32 branch_code_bytes;
  u32 load_code_bytes;
  u32 store_word_code_bytes;
  u32 store_byte_code_bytes;
  u32 store_pc_code_bytes;
  u32 bx_code_bytes;

  if (!code)
  {
    put_raw("result=FAIL command=runtime reason=mmap_failed\n");
    sys_exit(1);
  }

  data_code_bytes = build_data_block(code);
  branch_code_bytes = build_branch_block(code + BRANCH_BLOCK_OFFSET);
  load_code_bytes = build_load_block(code + LOAD_BLOCK_OFFSET);
  store_word_code_bytes =
    build_store_block(code + STORE_WORD_BLOCK_OFFSET,
                      STORE_STR_R6_R3_0X28,
                      STORE_WORD_START_PC,
                      &g_store_word_entry);
  store_byte_code_bytes =
    build_store_block(code + STORE_BYTE_BLOCK_OFFSET,
                      STORE_STRB_R6_R3_0X29,
                      STORE_BYTE_START_PC,
                      &g_store_byte_entry);
  store_pc_code_bytes =
    build_store_block(code + STORE_PC_BLOCK_OFFSET,
                      STORE_STR_R15_R3_0X2C,
                      STORE_PC_START_PC,
                      &g_store_pc_entry);
  bx_code_bytes = build_bx_block(code + BX_BLOCK_OFFSET);
  expect_halfword_transfers_rejected(code + BX_BLOCK_OFFSET + 512u);
  run_cycle_boundary_case();
  run_remaining_cycles_case();
  run_branch_boundary_case();
  run_branch_remaining_cycles_case();
  run_bx_arm_remaining_cycles_case();
  run_bx_thumb_boundary_case();
  run_load_boundary_case();
  run_load_remaining_cycles_case();
  run_store_word_boundary_case();
  run_store_word_remaining_cycles_case();
  run_store_pc_value_case();
  run_store_byte_remaining_cycles_case();
  run_store_smc_irq_alert_case();
  run_store_halt_alert_case();

  put_raw("result=PASS command=runtime code_bytes=");
  put_u32_dec(data_code_bytes);
  put_raw(" branch_code_bytes=");
  put_u32_dec(branch_code_bytes);
  put_raw(" load_code_bytes=");
  put_u32_dec(load_code_bytes);
  put_raw(" store_word_code_bytes=");
  put_u32_dec(store_word_code_bytes);
  put_raw(" store_byte_code_bytes=");
  put_u32_dec(store_byte_code_bytes);
  put_raw(" store_pc_code_bytes=");
  put_u32_dec(store_pc_code_bytes);
  put_raw(" bx_code_bytes=");
  put_u32_dec(bx_code_bytes);
  put_raw(" data_entry=");
  put_u32_hex((u32)g_data_entry);
  put_raw(" branch_entry=");
  put_u32_hex((u32)g_branch_entry);
  put_raw(" load_entry=");
  put_u32_hex((u32)g_load_entry);
  put_raw(" store_word_entry=");
  put_u32_hex((u32)g_store_word_entry);
  put_raw(" store_byte_entry=");
  put_u32_hex((u32)g_store_byte_entry);
  put_raw(" store_pc_entry=");
  put_u32_hex((u32)g_store_pc_entry);
  put_raw(" bx_entry=");
  put_u32_hex((u32)g_bx_entry);
  put_raw(" reason=state_equal\n");
  sys_exit(0);
}
