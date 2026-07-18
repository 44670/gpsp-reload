#include "riscv_runtime_test_shim.h"
#include "riscv_emit.h"
#if !defined(RV32IM_PERF_BASELINE_PROFILE)
#include "rv32im_mapped_alu_cases.h"
#endif

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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define PERF_CODE_BYTES (128u * 1024u)
#define PERF_HASH_INIT 2166136261u
#define PERF_FRAME_COMPLETE 0x80000000u
#define PERF_MEMORY_BASE 0x02001000u
#define PERF_MAX_BLOCKS 32u
#define PERF_WARMUP_RUNS 4u
#define PERF_ALU_INSNS 256u
#define PERF_MEMORY_INSNS 48u
#define PERF_BRANCH_BLOCKS 32u
#define PERF_SCHEDULER_INSNS 1u
#define PERF_MIXED_ALU0_INSNS 32u
#define PERF_MIXED_MEMORY_INSNS 12u
#define PERF_MIXED_ALU1_INSNS 16u
#define PERF_ALU_WARM_RUNS 128u
#define PERF_MEMORY_WARM_RUNS 64u
#define PERF_BRANCH_WARM_RUNS 128u
#define PERF_SCHEDULER_WARM_RUNS 2048u
#define PERF_MIXED_WARM_RUNS 64u

#define PERF_ALU_START_PC 0x08010000u
#define PERF_MEMORY_START_PC 0x08011000u
#define PERF_BRANCH_START_PC 0x08012000u
#define PERF_MIXED_START_PC 0x08013000u
#define PERF_SCHEDULER_START_PC 0x08014000u

typedef enum perf_workload_kind
{
  PERF_WORKLOAD_MAPPED_ALU = 0,
  PERF_WORKLOAD_MEMORY_READ,
  PERF_WORKLOAD_BRANCH_CHAIN,
  PERF_WORKLOAD_SCHEDULER,
  PERF_WORKLOAD_MIXED
} perf_workload_kind;

typedef struct perf_program
{
  perf_workload_kind kind;
  const char *name;
  u8 *entries[PERF_MAX_BLOCKS];
  u32 pcs[PERF_MAX_BLOCKS];
  u32 entry_count;
  u32 block_count;
  u32 code_bytes;
  u32 start_pc;
  u32 end_pc;
  u32 guest_insns_per_run;
  u32 guest_cycles_per_run;
  u32 direct_chains_per_run;
  u32 warm_runs;
  riscv_runtime_stats emit_stats;
} perf_program;

typedef struct perf_observation
{
  u32 lookup_calls;
  u32 terminal_calls;
  u32 update_calls;
  u32 read8_calls;
  u32 read16_calls;
  u32 read32_calls;
  u32 execute_arm_calls;
  u32 fallbacks;
  u32 initial_lookup_fallbacks;
  u32 relookup_fallbacks;
  u32 unsupported_fallbacks;
  u32 state_hash;
  u32 memory_hash;
  u32 scheduler_hash;
  u32 trace_hash;
} perf_observation;

u32 reg[REG_MAX];
u32 spsr[6];
u32 reg_mode[7][7];
u32 idle_loop_target_pc;
u32 rom_cache_watermark;
u32 gamepak_sticky_bit[1024 / 32];

static u8 *g_perf_code;
static perf_program g_program;
static u32 g_lookup_calls;
static u32 g_terminal_calls;
static u32 g_update_calls;
static u32 g_read8_calls;
static u32 g_read16_calls;
static u32 g_read32_calls;
static u32 g_execute_arm_calls;
static u32 g_fallbacks;
static u32 g_initial_lookup_fallbacks;
static u32 g_relookup_fallbacks;
static u32 g_unsupported_fallbacks;
static u32 g_memory_hash;
static u32 g_scheduler_hash;
static u32 g_trace_hash;
static s32 g_last_update_cycles;
static u32 g_last_update_pc;
static u32 g_rdinstret_csr_overhead;
static volatile u32 g_control_sink;

/* qemu-user exposes instret as a host-tick-derived value when icount is not
 * available. These unique, out-of-line markers let the Makefile sum the RV32
 * instructions represented by executed QEMU translation blocks. This is an
 * executed-instruction trace metric, not an architectural retired-instruction
 * counter. Raw rdinstret values remain diagnostic-only until independently
 * verified reliable. */
__attribute__((noinline))
void rv32im_perf_measure_begin(void)
{
  __asm__ volatile("addi zero, zero, 17" ::: "memory");
}

__attribute__((noinline))
void rv32im_perf_measure_end(void)
{
  __asm__ volatile("addi zero, zero, 19" ::: "memory");
}

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

static void put_u32_dec(u32 value)
{
  char digits[10];
  u32 count = 0;

  if (!value)
  {
    put_chr('0');
    return;
  }

  while (value)
  {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  }
  while (count)
    put_chr(digits[--count]);
}

static char hex_digit(u32 value)
{
  value &= 0xfu;
  return (char)(value < 10u ? '0' + value : 'a' + value - 10u);
}

static void put_u32_hex(u32 value)
{
  s32 shift;

  put_raw("0x");
  for (shift = 28; shift >= 0; shift -= 4)
    put_chr(hex_digit(value >> (u32)shift));
}

static u32 fnv_u32(u32 hash, u32 value)
{
  u32 i;

  for (i = 0; i < 4u; i++)
  {
    hash ^= (value >> (i * 8u)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

static u32 read_instret(void)
{
  u32 value;
  __asm__ volatile("csrr %0, instret" : "=r"(value));
  return value;
}

__attribute__((noinline))
static u32 run_control_window(u32 iterations)
{
  u32 i;
  u32 value = 0x13579bdfu;

  for (i = 0; i < iterations; i++)
    value = value * 1664525u + 1013904223u;
  g_control_sink = value;
  return value;
}

static u32 memory_mix(u32 address)
{
  u32 value = address ^ 0x9e3779b9u;
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  return value;
}

static u32 memory_value(u32 width, u32 address)
{
  u32 value = memory_mix(address);

  if (width == 8u)
    return value & 0xffu;
  if (width == 16u)
    return value & 0xffffu;
  return value;
}

static u32 memory_event_hash(u32 hash, u32 width, u32 address, u32 value)
{
  hash = fnv_u32(hash, width);
  hash = fnv_u32(hash, address);
  return fnv_u32(hash, value);
}

static void reset_observation_counters(void)
{
  g_lookup_calls = 0;
  g_terminal_calls = 0;
  g_update_calls = 0;
  g_read8_calls = 0;
  g_read16_calls = 0;
  g_read32_calls = 0;
  g_execute_arm_calls = 0;
  g_fallbacks = 0;
  g_initial_lookup_fallbacks = 0;
  g_relookup_fallbacks = 0;
  g_unsupported_fallbacks = 0;
  g_memory_hash = PERF_HASH_INIT;
  g_scheduler_hash = PERF_HASH_INIT;
  g_trace_hash = PERF_HASH_INIT;
  g_last_update_cycles = 0;
  g_last_update_pc = 0;
}

static void fill_initial_regs(u32 *values, const perf_program *program)
{
  u32 i;

  /* Only the architectural/dynarec prefix is guest reset state. */
  for (i = 0; i < REG_USERDEF; i++)
    values[i] = 0;
  for (i = 0; i < REG_PC; i++)
    values[i] = (0x01020304u * (i + 1u)) ^ (0x11111111u * i);

  values[REG_PC] = program->start_pc;
  values[REG_CPSR] = 0x2000001fu;
  values[CPU_MODE] = 0x1fu;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  if (program->kind == PERF_WORKLOAD_MEMORY_READ ||
      program->kind == PERF_WORKLOAD_MIXED)
    values[12] = PERF_MEMORY_BASE;
}

static void reset_guest_state(const perf_program *program)
{
  u32 i;

  fill_initial_regs(&reg[0], program);
  for (i = 0; i < ARRAY_SIZE(spsr); i++)
    spsr[i] = 0;
  for (i = 0; i < ARRAY_SIZE(reg_mode) * ARRAY_SIZE(reg_mode[0]); i++)
    ((u32 *)reg_mode)[i] = 0;
  idle_loop_target_pc = 0xffffffffu;
}

static u32 state_hash(const u32 *values)
{
  u32 hash = PERF_HASH_INIT;
  u32 i;

  for (i = 0; i <= REG_BUS_VALUE; i++)
    hash = fnv_u32(hash, values[i]);
  return hash;
}

static void alu_fields(u32 index, u32 *op, u32 *rn, u32 *rd, u32 *rm)
{
  static const u8 ops[] = {0x4, 0x2, 0x1, 0xc, 0x0, 0xd, 0xf};

  *op = ops[index % ARRAY_SIZE(ops)];
  *rn = (index * 5u + 1u) % 12u;
  *rd = (index * 7u + 2u) % 12u;
  *rm = (index * 3u + 4u) % 12u;
  if ((*op == 0xdu || *op == 0xfu) && *rd == *rm)
    *rd = (*rd + 1u) % 12u;
}

static u32 arm_alu_opcode(u32 index)
{
  u32 op;
  u32 rn;
  u32 rd;
  u32 rm;

  alu_fields(index, &op, &rn, &rd, &rm);
  return 0xe0000000u | (op << 21) | (rn << 16) | (rd << 12) | rm;
}

static void reference_alu(u32 *values, u32 index)
{
  u32 op;
  u32 rn;
  u32 rd;
  u32 rm;
  u32 lhs;
  u32 rhs;
  u32 result;

  alu_fields(index, &op, &rn, &rd, &rm);
  lhs = values[rn];
  rhs = values[rm];
  switch (op)
  {
    case 0x0:
      result = lhs & rhs;
      break;
    case 0x1:
      result = lhs ^ rhs;
      break;
    case 0x2:
      result = lhs - rhs;
      break;
    case 0x4:
      result = lhs + rhs;
      break;
    case 0xc:
      result = lhs | rhs;
      break;
    case 0xd:
      result = rhs;
      break;
    default:
      result = ~rhs;
      break;
  }
  values[rd] = result;
}

static void memory_fields(u32 index, u32 *width, u32 *rd, u32 *offset)
{
  *width = index % 3u == 0u ? 32u : (index % 3u == 1u ? 8u : 16u);
  *rd = (index * 7u + 1u) % 12u;
  *offset = (index * 5u + index % 3u) & 0xffu;
}

static u32 arm_memory_opcode(u32 index)
{
  u32 width;
  u32 rd;
  u32 offset;

  memory_fields(index, &width, &rd, &offset);
  if (width == 8u)
    return 0xe5dc0000u | (rd << 12) | offset;
  if (width == 16u)
  {
    return 0xe1dc00b0u | (rd << 12) |
      ((offset & 0xf0u) << 4) | (offset & 0x0fu);
  }
  return 0xe59c0000u | (rd << 12) | offset;
}

static void reference_memory(u32 *values, u32 index, u32 *hash,
                             u32 *read8, u32 *read16, u32 *read32)
{
  u32 width;
  u32 rd;
  u32 offset;
  u32 address;
  u32 value;

  memory_fields(index, &width, &rd, &offset);
  address = values[12] + offset;
  value = memory_value(width, address);
  values[rd] = value;
  *hash = memory_event_hash(*hash, width, address, value);
  if (width == 8u)
    (*read8)++;
  else if (width == 16u)
    (*read16)++;
  else
    (*read32)++;
}

static int emit_alu_range(u8 **ptr, riscv_jit_block_meta *meta,
                          u32 index_base, u32 count, u32 pc_base,
                          u32 final_cycles, int emit_final_cycles)
{
  u32 i;

  for (i = 0; i < count; i++)
  {
    bool cycles_emitted = false;
    bool emit_cycles = emit_final_cycles && i + 1u == count;

    if (!riscv_emit_native_arm_data_proc_with_pc_ex(
          ptr, meta, arm_alu_opcode(index_base + i), pc_base + i * 4u,
          final_cycles, 0, emit_cycles, &cycles_emitted))
      return 0;
    if (cycles_emitted != emit_cycles)
      return 0;
  }
  return 1;
}

static int emit_memory_range(u8 **ptr, riscv_jit_block_meta *meta,
                             u32 index_base, u32 count, u32 pc_base,
                             u32 final_cycles, int emit_final_cycles)
{
  u32 i;

  for (i = 0; i < count; i++)
  {
    bool cycles_emitted = false;
    bool emit_cycles = emit_final_cycles && i + 1u == count;

    if (!riscv_emit_native_arm_access_memory_ex(
          ptr, meta, arm_memory_opcode(index_base + i), pc_base + i * 4u,
          final_cycles, emit_cycles, &cycles_emitted))
      return 0;
    if (cycles_emitted != emit_cycles)
      return 0;
  }
  return 1;
}

#if !defined(RV32IM_PERF_BASELINE_PROFILE)
typedef struct mapped_alu_encoding_case
{
  const char *name;
  u32 opcode;
  u32 expected[2];
  u32 expected_words;
} mapped_alu_encoding_case;

static u32 encode_rv32_r(u32 funct7, u32 funct3, riscv_reg_number rd,
                         riscv_reg_number rs1, riscv_reg_number rs2)
{
  return ((funct7 & 0x7fu) << 25) |
    (((u32)rs2 & 0x1fu) << 20) |
    (((u32)rs1 & 0x1fu) << 15) |
    ((funct3 & 0x7u) << 12) |
    (((u32)rd & 0x1fu) << 7) | 0x33u;
}

static u32 encode_rv32_i(u32 funct3, riscv_reg_number rd,
                         riscv_reg_number rs1, s32 immediate)
{
  return (((u32)immediate & 0xfffu) << 20) |
    (((u32)rs1 & 0x1fu) << 15) |
    ((funct3 & 0x7u) << 12) |
    (((u32)rd & 0x1fu) << 7) | 0x13u;
}

static u32 encode_arm_reg_alu(u32 op, u32 rn, u32 rd, u32 rm)
{
  return 0xe0000000u | (op << 21) | (rn << 16) | (rd << 12) | rm;
}

static int verify_mapped_alu_encodings(void)
{
  mapped_alu_encoding_case cases[12];
  u32 case_count = 0;
  u32 i;

  if (RV32IM_MAPPED_ALU_CASE_COUNT != 15u)
  {
    put_raw("result=FAIL command=rv32im-perf workload=mapped_alu_encoding ");
    put_raw("phase=check backend=rv32im reason=shared_case_count_changed\n");
    return 0;
  }

#define ADD_ENCODING_CASE(case_name, arm_op, arm_rn, arm_rd, arm_rm, words)   \
  do                                                                          \
  {                                                                           \
    cases[case_count].name = (case_name);                                      \
    cases[case_count].opcode =                                                 \
      encode_arm_reg_alu((arm_op), (arm_rn), (arm_rd), (arm_rm));             \
    cases[case_count].expected_words = (words);                                \
    case_count++;                                                              \
  } while (0)

  ADD_ENCODING_CASE("add", 0x4, 0, 2, 1, 1);
  cases[0].expected[0] =
    encode_rv32_r(0x00, 0x0, riscv_reg_a5, riscv_reg_a3, riscv_reg_a4);
  ADD_ENCODING_CASE("sub_alias_rn", 0x2, 0, 0, 1, 1);
  cases[1].expected[0] =
    encode_rv32_r(0x20, 0x0, riscv_reg_a3, riscv_reg_a3, riscv_reg_a4);
  ADD_ENCODING_CASE("rsb_alias_rm", 0x3, 0, 1, 1, 1);
  cases[2].expected[0] =
    encode_rv32_r(0x20, 0x0, riscv_reg_a4, riscv_reg_a4, riscv_reg_a3);
  ADD_ENCODING_CASE("eor", 0x1, 5, 8, 14, 1);
  cases[3].expected[0] =
    encode_rv32_r(0x00, 0x4, riscv_reg_s4, riscv_reg_s1, riscv_reg_s10);
  ADD_ENCODING_CASE("and", 0x0, 3, 4, 2, 1);
  cases[4].expected[0] =
    encode_rv32_r(0x00, 0x7, riscv_reg_a7, riscv_reg_a6, riscv_reg_a5);
  ADD_ENCODING_CASE("orr", 0xc, 11, 12, 10, 1);
  cases[5].expected[0] =
    encode_rv32_r(0x00, 0x6, riscv_reg_s8, riscv_reg_s7, riscv_reg_s6);
  ADD_ENCODING_CASE("mov", 0xd, 0, 6, 9, 1);
  cases[6].expected[0] =
    encode_rv32_i(0x0, riscv_reg_s2, riscv_reg_s5, 0);
  ADD_ENCODING_CASE("mvn", 0xf, 0, 10, 7, 1);
  cases[7].expected[0] =
    encode_rv32_i(0x4, riscv_reg_s6, riscv_reg_s3, -1);
  ADD_ENCODING_CASE("bic", 0xe, 1, 3, 2, 2);
  cases[8].expected[0] =
    encode_rv32_i(0x4, riscv_reg_t0, riscv_reg_a5, -1);
  cases[8].expected[1] =
    encode_rv32_r(0x00, 0x7, riscv_reg_a6, riscv_reg_a4, riscv_reg_t0);
  ADD_ENCODING_CASE("bic_alias_rm", 0xe, 1, 0, 0, 2);
  cases[9].expected[0] =
    encode_rv32_i(0x4, riscv_reg_t0, riscv_reg_a3, -1);
  cases[9].expected[1] =
    encode_rv32_r(0x00, 0x7, riscv_reg_a3, riscv_reg_a4, riscv_reg_t0);
  ADD_ENCODING_CASE("add_all_alias", 0x4, 4, 4, 4, 1);
  cases[10].expected[0] =
    encode_rv32_r(0x00, 0x0, riscv_reg_a7, riscv_reg_a7, riscv_reg_a7);
  ADD_ENCODING_CASE("add_sp_lr_alias", 0x4, 13, 14, 14, 1);
  cases[11].expected[0] =
    encode_rv32_r(0x00, 0x0, riscv_reg_s10, riscv_reg_s9, riscv_reg_s10);

#undef ADD_ENCODING_CASE

  init_emitter(false);
  for (i = 0; i < case_count; i++)
  {
    u8 *ptr = g_perf_code;
    u8 *body;
    riscv_jit_block_meta *meta;
    bool cycles_emitted = true;
    u32 actual_words;
    u32 word;

    if (cases[i].opcode != rv32im_mapped_alu_opcode(
          &rv32im_mapped_alu_cases[i]))
    {
      put_raw("result=FAIL command=rv32im-perf workload=mapped_alu_encoding ");
      put_raw("phase=check backend=rv32im case=");
      put_raw(cases[i].name);
      put_raw(" reason=shared_opcode_mismatch\n");
      return 0;
    }

    riscv_emit_block_prologue(&ptr, &meta);
    body = ptr;
    if (!riscv_emit_native_arm_data_proc_with_pc_ex(
          &ptr, meta, cases[i].opcode, PERF_ALU_START_PC, 1u, 0,
          false, &cycles_emitted))
    {
      put_raw("result=FAIL command=rv32im-perf workload=mapped_alu_encoding ");
      put_raw("phase=check backend=rv32im case=");
      put_raw(cases[i].name);
      put_raw(" reason=lowering_rejected\n");
      return 0;
    }
    actual_words = (u32)(ptr - body) / 4u;
    if (cycles_emitted || ((u32)(ptr - body) & 3u) != 0 ||
        actual_words != cases[i].expected_words)
    {
      put_raw("result=FAIL command=rv32im-perf workload=mapped_alu_encoding ");
      put_raw("phase=check backend=rv32im case=");
      put_raw(cases[i].name);
      put_raw(" expected_words=");
      put_u32_dec(cases[i].expected_words);
      put_raw(" actual_words=");
      put_u32_dec(actual_words);
      put_raw(" reason=instruction_count_mismatch\n");
      return 0;
    }
    for (word = 0; word < actual_words; word++)
    {
      u32 actual = ((u32 *)(void *)body)[word];

      if (actual != cases[i].expected[word])
      {
        put_raw("result=FAIL command=rv32im-perf workload=mapped_alu_encoding ");
        put_raw("phase=check backend=rv32im case=");
        put_raw(cases[i].name);
        put_raw(" word=");
        put_u32_dec(word);
        put_raw(" expected=");
        put_u32_hex(cases[i].expected[word]);
        put_raw(" actual=");
        put_u32_hex(actual);
        put_raw(" reason=encoding_mismatch\n");
        return 0;
      }
    }
  }

  {
    u8 *ptr = g_perf_code;
    u8 *body;
    riscv_jit_block_meta *meta;
    u32 expected =
      encode_rv32_r(0x01, 0x0, riscv_reg_a7, riscv_reg_a4, riscv_reg_a5);
    u32 actual_words;

    riscv_emit_block_prologue(&ptr, &meta);
    body = ptr;
    if (!riscv_emit_native_arm_multiply(
          &ptr, meta,
          rv32im_mapped_alu_opcode(
            &rv32im_mapped_alu_cases[
              RV32IM_MAPPED_ALU_FIRST_MULTIPLY_CASE]),
          0u))
    {
      put_raw("result=FAIL command=rv32im-perf workload=mapped_alu_encoding ");
      put_raw("phase=check backend=rv32im case=mul reason=lowering_rejected\n");
      return 0;
    }
    actual_words = (u32)(ptr - body) / 4u;
    if (actual_words != 1u || ((u32)(ptr - body) & 3u) != 0)
    {
      put_raw("result=FAIL command=rv32im-perf workload=mapped_alu_encoding ");
      put_raw("phase=check backend=rv32im case=mul expected_words=1 actual_words=");
      put_u32_dec(actual_words);
      put_raw(" reason=instruction_count_mismatch\n");
      return 0;
    }
    if (((u32 *)(void *)body)[0] != expected)
    {
      put_raw("result=FAIL command=rv32im-perf workload=mapped_alu_encoding ");
      put_raw("phase=check backend=rv32im case=mul expected=");
      put_u32_hex(expected);
      put_raw(" actual=");
      put_u32_hex(((u32 *)(void *)body)[0]);
      put_raw(" reason=encoding_mismatch\n");
      return 0;
    }
  }

  put_raw("result=PASS command=rv32im-perf workload=mapped_alu_encoding ");
  put_raw("phase=check backend=rv32im cases=");
  put_u32_dec(case_count + 1u);
  put_raw(" harness_mode=runtime_fixture reason=exact_mapped_alu_encodings\n");
  return 1;
}
#endif

static void clear_program(perf_workload_kind kind, const char *name,
                          u32 start_pc, u32 warm_runs)
{
  u32 i;

  g_program.kind = kind;
  g_program.name = name;
  g_program.entry_count = 0;
  g_program.block_count = 0;
  g_program.code_bytes = 0;
  g_program.start_pc = start_pc;
  g_program.end_pc = start_pc;
  g_program.guest_insns_per_run = 0;
  g_program.guest_cycles_per_run = 0;
  g_program.direct_chains_per_run = 0;
  g_program.warm_runs = warm_runs;
  for (i = 0; i < PERF_MAX_BLOCKS; i++)
  {
    g_program.entries[i] = (u8 *)0;
    g_program.pcs[i] = 0;
  }
}

static int build_mapped_alu(const char **reason)
{
  u8 *ptr = g_perf_code;
  riscv_jit_block_meta *meta;

  clear_program(PERF_WORKLOAD_MAPPED_ALU, "mapped_alu",
                PERF_ALU_START_PC, PERF_ALU_WARM_RUNS);
  riscv_emit_block_prologue(&ptr, &meta);
  g_program.entries[0] = ((u8 *)meta) + block_prologue_size;
  g_program.pcs[0] = PERF_ALU_START_PC;
  g_program.entry_count = 1;
  if (!emit_alu_range(&ptr, meta, 0, PERF_ALU_INSNS,
                      PERF_ALU_START_PC, PERF_ALU_INSNS, 1))
  {
    *reason = "mapped_alu_emit_failed";
    return 0;
  }
  g_program.end_pc = PERF_ALU_START_PC + PERF_ALU_INSNS * 4u;
  riscv_emit_block_finalize(meta, &ptr, PERF_ALU_START_PC,
                            g_program.end_pc, false);
  g_program.block_count = 1;
  g_program.guest_insns_per_run = PERF_ALU_INSNS;
  g_program.guest_cycles_per_run = PERF_ALU_INSNS;
  g_program.code_bytes = (u32)(ptr - g_perf_code);
  return 1;
}

static int build_memory_read(const char **reason)
{
  u8 *ptr = g_perf_code;
  riscv_jit_block_meta *meta;

  clear_program(PERF_WORKLOAD_MEMORY_READ, "memory_read",
                PERF_MEMORY_START_PC, PERF_MEMORY_WARM_RUNS);
  riscv_emit_block_prologue(&ptr, &meta);
  g_program.entries[0] = ((u8 *)meta) + block_prologue_size;
  g_program.pcs[0] = PERF_MEMORY_START_PC;
  g_program.entry_count = 1;
  if (!emit_memory_range(&ptr, meta, 0, PERF_MEMORY_INSNS,
                         PERF_MEMORY_START_PC, PERF_MEMORY_INSNS, 1))
  {
    *reason = "memory_read_emit_failed";
    return 0;
  }
  g_program.end_pc = PERF_MEMORY_START_PC + PERF_MEMORY_INSNS * 4u;
  riscv_emit_block_finalize(meta, &ptr, PERF_MEMORY_START_PC,
                            g_program.end_pc, false);
  g_program.block_count = 1;
  g_program.guest_insns_per_run = PERF_MEMORY_INSNS;
  /* ARM loads add their two-cycle memory adjustment when the frontend emits
   * the block's final cycle checkpoint. */
  g_program.guest_cycles_per_run = PERF_MEMORY_INSNS + 2u;
  g_program.code_bytes = (u32)(ptr - g_perf_code);
  return 1;
}

static int build_branch_chain(const char **reason)
{
  u8 *ptr = g_perf_code;
  u8 *patches[PERF_BRANCH_BLOCKS - 1u];
  u32 i;

  clear_program(PERF_WORKLOAD_BRANCH_CHAIN, "branch_chain",
                PERF_BRANCH_START_PC, PERF_BRANCH_WARM_RUNS);
  for (i = 0; i < PERF_BRANCH_BLOCKS; i++)
  {
    riscv_jit_block_meta *meta;
    u32 pc = PERF_BRANCH_START_PC + i * 8u;
    bool cycles_emitted = false;

    riscv_emit_block_prologue(&ptr, &meta);
    g_program.entries[i] = ((u8 *)meta) + block_prologue_size;
    g_program.pcs[i] = pc;
    if (!riscv_emit_native_arm_data_proc_with_pc_ex(
          &ptr, meta, 0xe0800001u, pc, 1u, 0,
          i + 1u == PERF_BRANCH_BLOCKS, &cycles_emitted))
    {
      *reason = "branch_chain_add_emit_failed";
      return 0;
    }

    if (i + 1u < PERF_BRANCH_BLOCKS)
    {
      if (cycles_emitted ||
          !riscv_emit_native_arm_b_patchable(
            &ptr, meta, &patches[i], 0xeaffffffu, pc + 4u, 2u,
            true, true) || !patches[i])
      {
        *reason = "branch_chain_branch_emit_failed";
        return 0;
      }
      riscv_emit_block_finalize(meta, &ptr, pc, pc + 8u, false);
    }
    else
    {
      if (!cycles_emitted)
      {
        *reason = "branch_chain_cycles_missing";
        return 0;
      }
      riscv_emit_block_finalize(meta, &ptr, pc, pc + 4u, false);
      g_program.end_pc = pc + 4u;
    }
  }

  for (i = 0; i + 1u < PERF_BRANCH_BLOCKS; i++)
    riscv_patch_unconditional_branch_short(patches[i],
                                           g_program.entries[i + 1u]);

  g_program.entry_count = PERF_BRANCH_BLOCKS;
  g_program.block_count = PERF_BRANCH_BLOCKS;
  g_program.guest_insns_per_run = (PERF_BRANCH_BLOCKS - 1u) * 2u + 1u;
  g_program.guest_cycles_per_run = g_program.guest_insns_per_run;
  g_program.direct_chains_per_run = PERF_BRANCH_BLOCKS - 1u;
  g_program.code_bytes = (u32)(ptr - g_perf_code);
  return 1;
}

static int build_scheduler(const char **reason)
{
  u8 *ptr = g_perf_code;
  riscv_jit_block_meta *meta;

  clear_program(PERF_WORKLOAD_SCHEDULER, "scheduler",
                PERF_SCHEDULER_START_PC, PERF_SCHEDULER_WARM_RUNS);
  riscv_emit_block_prologue(&ptr, &meta);
  g_program.entries[0] = ((u8 *)meta) + block_prologue_size;
  g_program.pcs[0] = PERF_SCHEDULER_START_PC;
  g_program.entry_count = 1;
  if (!emit_alu_range(&ptr, meta, 0, PERF_SCHEDULER_INSNS,
                      PERF_SCHEDULER_START_PC, PERF_SCHEDULER_INSNS, 1))
  {
    *reason = "scheduler_emit_failed";
    return 0;
  }
  g_program.end_pc = PERF_SCHEDULER_START_PC + PERF_SCHEDULER_INSNS * 4u;
  riscv_emit_block_finalize(meta, &ptr, PERF_SCHEDULER_START_PC,
                            g_program.end_pc, false);
  g_program.block_count = 1;
  g_program.guest_insns_per_run = PERF_SCHEDULER_INSNS;
  g_program.guest_cycles_per_run = PERF_SCHEDULER_INSNS;
  g_program.code_bytes = (u32)(ptr - g_perf_code);
  return 1;
}

static int build_mixed(const char **reason)
{
  u8 *ptr = g_perf_code;
  riscv_jit_block_meta *meta;
  u8 *patch = (u8 *)0;
  u32 branch_pc;
  u32 second_pc;
  u32 second_insns = PERF_MIXED_MEMORY_INSNS + PERF_MIXED_ALU1_INSNS;

  clear_program(PERF_WORKLOAD_MIXED, "mixed",
                PERF_MIXED_START_PC, PERF_MIXED_WARM_RUNS);
  riscv_emit_block_prologue(&ptr, &meta);
  g_program.entries[0] = ((u8 *)meta) + block_prologue_size;
  g_program.pcs[0] = PERF_MIXED_START_PC;
  if (!emit_alu_range(&ptr, meta, 1000u, PERF_MIXED_ALU0_INSNS,
                      PERF_MIXED_START_PC, 0, 0))
  {
    *reason = "mixed_alu0_emit_failed";
    return 0;
  }
  branch_pc = PERF_MIXED_START_PC + PERF_MIXED_ALU0_INSNS * 4u;
  second_pc = branch_pc + 8u;
  if (!riscv_emit_native_arm_b_patchable(
        &ptr, meta, &patch, 0xea000000u, branch_pc,
        PERF_MIXED_ALU0_INSNS + 1u, true, true) || !patch)
  {
    *reason = "mixed_branch_emit_failed";
    return 0;
  }
  riscv_emit_block_finalize(meta, &ptr, PERF_MIXED_START_PC,
                            branch_pc + 4u, false);

  riscv_emit_block_prologue(&ptr, &meta);
  g_program.entries[1] = ((u8 *)meta) + block_prologue_size;
  g_program.pcs[1] = second_pc;
  if (!emit_memory_range(&ptr, meta, 1000u, PERF_MIXED_MEMORY_INSNS,
                         second_pc, 0, 0) ||
      !emit_alu_range(&ptr, meta, 2000u, PERF_MIXED_ALU1_INSNS,
                      second_pc + PERF_MIXED_MEMORY_INSNS * 4u,
                      second_insns, 1))
  {
    *reason = "mixed_second_block_emit_failed";
    return 0;
  }
  g_program.end_pc = second_pc + second_insns * 4u;
  riscv_emit_block_finalize(meta, &ptr, second_pc, g_program.end_pc, false);
  riscv_patch_unconditional_branch_short(patch, g_program.entries[1]);

  g_program.entry_count = 2;
  g_program.block_count = 2;
  g_program.guest_insns_per_run =
    PERF_MIXED_ALU0_INSNS + 1u + second_insns;
  g_program.guest_cycles_per_run = g_program.guest_insns_per_run;
  g_program.direct_chains_per_run = 1;
  g_program.code_bytes = (u32)(ptr - g_perf_code);
  return 1;
}

static int build_program(perf_workload_kind kind, const char **reason)
{
  int ok;
  long flush_result;

  init_emitter(false);
  if (kind == PERF_WORKLOAD_MAPPED_ALU)
    ok = build_mapped_alu(reason);
  else if (kind == PERF_WORKLOAD_MEMORY_READ)
    ok = build_memory_read(reason);
  else if (kind == PERF_WORKLOAD_BRANCH_CHAIN)
    ok = build_branch_chain(reason);
  else if (kind == PERF_WORKLOAD_SCHEDULER)
    ok = build_scheduler(reason);
  else
    ok = build_mixed(reason);
  if (!ok)
    return 0;
  if (!g_program.code_bytes || g_program.code_bytes > PERF_CODE_BYTES)
  {
    *reason = "generated_code_size_invalid";
    return 0;
  }

  flush_result = syscall3(SYS_RISCV_FLUSH_ICACHE, (long)g_perf_code,
                          (long)(g_perf_code + g_program.code_bytes), 0);
  if (flush_result != 0)
  {
    *reason = "icache_sync_failed";
    return 0;
  }
  riscv_get_runtime_stats(&g_program.emit_stats);
  return 1;
}

#if !defined(RV32IM_PERF_BASELINE_PROFILE)
static void reset_mapped_alu_case_state(u32 pc)
{
  u32 i;

  for (i = 0; i < REG_USERDEF; i++)
    reg[i] = 0;
  rv32im_mapped_alu_initial_regs(&reg[0]);
  reg[REG_PC] = pc;
  reg[REG_CPSR] = 0x2000001fu;
  reg[CPU_MODE] = 0x1fu;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
  for (i = 0; i < ARRAY_SIZE(spsr); i++)
    spsr[i] = 0;
  for (i = 0; i < ARRAY_SIZE(reg_mode) * ARRAY_SIZE(reg_mode[0]); i++)
    ((u32 *)reg_mode)[i] = 0;
  idle_loop_target_pc = 0xffffffffu;
}

static int verify_mapped_alu_semantics(void)
{
  u32 i;

  for (i = 0; i < RV32IM_MAPPED_ALU_CASE_COUNT; i++)
  {
    const rv32im_mapped_alu_case *item = &rv32im_mapped_alu_cases[i];
    u32 pc = 0x08015000u + i * 0x100u;
    u8 *ptr = g_perf_code;
    riscv_jit_block_meta *meta;
    bool cycles_emitted = false;
    long flush_result;
    riscv_runtime_stats stats;
    int emitted;
    int passed;

    init_emitter(false);
    clear_program(PERF_WORKLOAD_MAPPED_ALU, "mapped_alu_semantics", pc, 1u);
    riscv_emit_block_prologue(&ptr, &meta);
    g_program.entries[0] = ((u8 *)meta) + block_prologue_size;
    g_program.pcs[0] = pc;
    g_program.entry_count = 1;

    if (item->kind == RV32IM_MAPPED_ALU_MULTIPLY)
    {
      emitted = riscv_emit_native_arm_multiply(
        &ptr, meta, rv32im_mapped_alu_opcode(item), 1u);
      cycles_emitted = true;
    }
    else
    {
      emitted = riscv_emit_native_arm_data_proc_with_pc_ex(
        &ptr, meta, rv32im_mapped_alu_opcode(item), pc, 1u, 0,
        true, &cycles_emitted);
    }

    if (!emitted || !cycles_emitted)
    {
      put_raw("result=FAIL command=rv32im-mapped-alu-semantics case=");
      put_raw(item->name);
      put_raw(" backend=rv32im reason=lowering_rejected\n");
      return 0;
    }

    g_program.end_pc = pc + 4u;
    riscv_emit_block_finalize(meta, &ptr, pc, g_program.end_pc, false);
    g_program.block_count = 1;
    g_program.code_bytes = (u32)(ptr - g_perf_code);
    flush_result = syscall3(SYS_RISCV_FLUSH_ICACHE, (long)g_perf_code,
                            (long)(g_perf_code + g_program.code_bytes), 0);
    if (flush_result != 0)
    {
      put_raw("result=FAIL command=rv32im-mapped-alu-semantics case=");
      put_raw(item->name);
      put_raw(" backend=rv32im reason=icache_sync_failed\n");
      return 0;
    }

    reset_mapped_alu_case_state(pc);
    reset_observation_counters();
    execute_arm_translate_internal(1u, &reg[0]);
    riscv_get_runtime_stats(&stats);
    passed = reg[REG_PC] == pc + 4u && reg[REG_CPSR] == 0x2000001fu &&
      g_fallbacks == 0 && g_execute_arm_calls == 0 &&
      g_lookup_calls == 1 && g_update_calls == 1 &&
      stats.blocks_executed > 0;

    put_raw("result=");
    put_raw(passed ? "PASS" : "FAIL");
    put_raw(" command=rv32im-mapped-alu-semantics case=");
    put_raw(item->name);
    put_raw(" backend=rv32im opcode=");
    put_u32_hex(rv32im_mapped_alu_opcode(item));
    put_raw(" rd=");
    put_u32_dec(item->rd);
    put_raw(" rd_value=");
    put_u32_hex(reg[item->rd]);
    put_raw(" pc=");
    put_u32_hex(reg[REG_PC]);
    put_raw(" cpsr=");
    put_u32_hex(reg[REG_CPSR]);
    put_raw(" guest_state_hash=");
    put_u32_hex(rv32im_mapped_alu_state_hash(&reg[0]));
    put_raw(" generated_bytes=");
    put_u32_dec(g_program.code_bytes);
    put_raw(" native_blocks=");
    put_u32_dec(stats.blocks_executed);
    put_raw(" fallbacks=");
    put_u32_dec(g_fallbacks);
    put_raw(" execute_arm_calls=");
    put_u32_dec(g_execute_arm_calls);
    put_raw(" harness_mode=runtime_fixture reason=");
    put_raw(passed ? "native_case_executed" : "native_case_contract_mismatch");
    put_chr('\n');
    if (!passed)
      return 0;
  }

  put_raw("result=PASS command=rv32im-mapped-alu-semantics case=all ");
  put_raw("backend=rv32im cases=");
  put_u32_dec((u32)RV32IM_MAPPED_ALU_CASE_COUNT);
  put_raw(" operations=and,eor,sub,rsb,add,orr,mov,mvn,bic,mul ");
  put_raw("aliases=rd_rn,rd_rm,rn_rm,mul_rd_rs,mul_rm_rs ");
  put_raw("caller_and_callee_mappings=1 ");
  put_raw("reason=native_semantic_cases_complete\n");
  return 1;
}
#endif

static void run_program(const perf_program *program, u32 iterations)
{
  u32 i;

  for (i = 0; i < iterations; i++)
  {
    reg[REG_PC] = program->start_pc;
    execute_arm_translate_internal(program->guest_cycles_per_run, &reg[0]);
  }
}

static void capture_observation(perf_observation *observation)
{
  observation->lookup_calls = g_lookup_calls;
  observation->terminal_calls = g_terminal_calls;
  observation->update_calls = g_update_calls;
  observation->read8_calls = g_read8_calls;
  observation->read16_calls = g_read16_calls;
  observation->read32_calls = g_read32_calls;
  observation->execute_arm_calls = g_execute_arm_calls;
  observation->fallbacks = g_fallbacks;
  observation->initial_lookup_fallbacks = g_initial_lookup_fallbacks;
  observation->relookup_fallbacks = g_relookup_fallbacks;
  observation->unsupported_fallbacks = g_unsupported_fallbacks;
  observation->state_hash = state_hash(&reg[0]);
  observation->memory_hash = g_memory_hash;
  observation->scheduler_hash = g_scheduler_hash;
  observation->trace_hash = g_trace_hash;
}

static void reference_run(const perf_program *program, u32 iterations,
                          perf_observation *expected)
{
  u32 values[REG_MAX];
  u32 memory_hash = PERF_HASH_INIT;
  u32 scheduler_hash = PERF_HASH_INIT;
  u32 trace_hash = PERF_HASH_INIT;
  u32 read8 = 0;
  u32 read16 = 0;
  u32 read32 = 0;
  u32 run;

  fill_initial_regs(values, program);
  for (run = 0; run < iterations; run++)
  {
    u32 i;

    values[REG_PC] = program->start_pc;
    trace_hash = fnv_u32(trace_hash, program->start_pc);
    if (program->kind == PERF_WORKLOAD_MAPPED_ALU)
    {
      for (i = 0; i < PERF_ALU_INSNS; i++)
        reference_alu(values, i);
    }
    else if (program->kind == PERF_WORKLOAD_MEMORY_READ)
    {
      for (i = 0; i < PERF_MEMORY_INSNS; i++)
        reference_memory(values, i, &memory_hash,
                         &read8, &read16, &read32);
    }
    else if (program->kind == PERF_WORKLOAD_BRANCH_CHAIN)
    {
      for (i = 0; i < PERF_BRANCH_BLOCKS; i++)
        values[0] += values[1];
    }
    else if (program->kind == PERF_WORKLOAD_SCHEDULER)
    {
      reference_alu(values, 0);
    }
    else
    {
      for (i = 0; i < PERF_MIXED_ALU0_INSNS; i++)
        reference_alu(values, 1000u + i);
      for (i = 0; i < PERF_MIXED_MEMORY_INSNS; i++)
        reference_memory(values, 1000u + i, &memory_hash,
                         &read8, &read16, &read32);
      for (i = 0; i < PERF_MIXED_ALU1_INSNS; i++)
        reference_alu(values, 2000u + i);
    }
    values[REG_PC] = program->end_pc;
    scheduler_hash = fnv_u32(scheduler_hash, 0);
    scheduler_hash = fnv_u32(scheduler_hash, PERF_FRAME_COMPLETE);
    scheduler_hash = fnv_u32(scheduler_hash, program->end_pc);
  }

  expected->lookup_calls = iterations;
  expected->terminal_calls = iterations;
  expected->update_calls = iterations;
  expected->read8_calls = read8;
  expected->read16_calls = read16;
  expected->read32_calls = read32;
  expected->execute_arm_calls = 0;
  expected->fallbacks = 0;
  expected->initial_lookup_fallbacks = 0;
  expected->relookup_fallbacks = 0;
  expected->unsupported_fallbacks = 0;
  expected->state_hash = state_hash(values);
  expected->memory_hash = memory_hash;
  expected->scheduler_hash = scheduler_hash;
  expected->trace_hash = trace_hash;
}

static int observations_equal(const perf_observation *actual,
                              const perf_observation *expected)
{
  return actual->lookup_calls == expected->lookup_calls &&
    actual->terminal_calls == expected->terminal_calls &&
    actual->update_calls == expected->update_calls &&
    actual->read8_calls == expected->read8_calls &&
    actual->read16_calls == expected->read16_calls &&
    actual->read32_calls == expected->read32_calls &&
    actual->execute_arm_calls == expected->execute_arm_calls &&
    actual->fallbacks == expected->fallbacks &&
    actual->initial_lookup_fallbacks == expected->initial_lookup_fallbacks &&
    actual->relookup_fallbacks == expected->relookup_fallbacks &&
    actual->unsupported_fallbacks == expected->unsupported_fallbacks &&
    actual->state_hash == expected->state_hash &&
    actual->memory_hash == expected->memory_hash &&
    actual->scheduler_hash == expected->scheduler_hash &&
    actual->trace_hash == expected->trace_hash;
}

static void print_perf_result(const char *result, const char *phase,
                              const char *reason, u32 iterations,
                              u32 raw_instret,
                              const perf_observation *observation,
                              u32 translations, u32 cache_sync_bytes)
{
  u32 guest_insns = g_program.guest_insns_per_run * iterations;
  u32 guest_cycles = g_program.guest_cycles_per_run * iterations;
  u32 net_instret = raw_instret > g_rdinstret_csr_overhead ?
    raw_instret - g_rdinstret_csr_overhead : 0;
  u32 total_helpers = observation->read8_calls + observation->read16_calls +
    observation->read32_calls + observation->terminal_calls;
  u32 mapped_flushes =
    g_program.emit_stats.perf_mapped_flush_sites * iterations;
  u32 mapped_stores =
    g_program.emit_stats.perf_mapped_store_ops * iterations;
  u32 mapped_invalidates =
    g_program.emit_stats.perf_mapped_invalidate_sites * iterations;
  u32 mapped_reloads = iterations +
    g_program.emit_stats.perf_mapped_reload_sites * iterations;
  u32 mapped_reload_ops = 16u * iterations +
    g_program.emit_stats.perf_mapped_reload_ops * iterations;

  put_raw("result=");
  put_raw(result);
  put_raw(" command=rv32im-perf workload=");
  put_raw(g_program.name);
  put_raw(" phase=");
  put_raw(phase);
  put_raw(" backend=rv32im guest_insns=");
  put_u32_dec(guest_insns);
  put_raw(" guest_cycles=");
  put_u32_dec(guest_cycles);
  put_raw(" rdinstret_csr_raw=");
  put_u32_dec(raw_instret);
  put_raw(" rdinstret_csr_overhead=");
  put_u32_dec(g_rdinstret_csr_overhead);
  put_raw(" rdinstret_csr_net=");
  put_u32_dec(net_instret);
  put_raw(" generated_blocks=");
  put_u32_dec(g_program.block_count);
  put_raw(" generated_bytes=");
  put_u32_dec(g_program.code_bytes);
  put_raw(" translations=");
  put_u32_dec(translations);
  put_raw(" cache_sync_bytes=");
  put_u32_dec(cache_sync_bytes);
  put_raw(" helpers=");
  put_u32_dec(total_helpers);
  put_raw(" helper_read8=");
  put_u32_dec(observation->read8_calls);
  put_raw(" helper_read16=");
  put_u32_dec(observation->read16_calls);
  put_raw(" helper_read32=");
  put_u32_dec(observation->read32_calls);
  put_raw(" helper_terminal=");
  put_u32_dec(observation->terminal_calls);
  put_raw(" emitted_helper_sites=");
  put_u32_dec(g_program.emit_stats.perf_helper_call_sites);
  put_raw(" emitted_terminal_sites=");
  put_u32_dec(g_program.emit_stats.perf_terminal_call_sites);
  put_raw(" direct_chain_attempts=");
  put_u32_dec(g_program.direct_chains_per_run * iterations);
  put_raw(" direct_chain_hits=");
  put_u32_dec(g_program.direct_chains_per_run * iterations);
  put_raw(" fallthrough_lookups=0 indirect_lookups=0 lookups=");
  put_u32_dec(observation->lookup_calls);
  put_raw(" scheduler_exits=");
  put_u32_dec(observation->update_calls);
  put_raw(" mapped_flushes=");
  put_u32_dec(mapped_flushes);
  put_raw(" mapped_store_ops=");
  put_u32_dec(mapped_stores);
  put_raw(" mapped_invalidates=");
  put_u32_dec(mapped_invalidates);
  put_raw(" mapped_reloads=");
  put_u32_dec(mapped_reloads);
  put_raw(" mapped_reload_ops=");
  put_u32_dec(mapped_reload_ops);
  put_raw(" fallbacks=");
  put_u32_dec(observation->fallbacks);
  put_raw(" initial_lookup_fallbacks=");
  put_u32_dec(observation->initial_lookup_fallbacks);
  put_raw(" relookup_fallbacks=");
  put_u32_dec(observation->relookup_fallbacks);
  put_raw(" unsupported_fallbacks=");
  put_u32_dec(observation->unsupported_fallbacks);
  put_raw(" execute_arm_calls=");
  put_u32_dec(observation->execute_arm_calls);
  put_raw(" state_hash=");
  put_u32_hex(observation->state_hash);
  put_raw(" memory_hash=");
  put_u32_hex(observation->memory_hash);
  put_raw(" scheduler_hash=");
  put_u32_hex(observation->scheduler_hash);
  put_raw(" trace_hash=");
  put_u32_hex(observation->trace_hash);
  put_raw(" harness_mode=runtime_fixture counter_source=rdinstret_csr_diagnostic");
  put_raw(" rdinstret_csr_verified=0");
  put_raw(phase[0] == 'c' ?
          " measurement_scope=translate_sync_first_execute" :
          " measurement_scope=steady_state_execute");
  put_raw(" instrumentation_counter_semantics=emission_sites_x_iterations");
  put_raw(" reason=");
  put_raw(reason);
  put_chr('\n');
}

static int measure_phase(const char *phase, u32 iterations,
                         u32 start_instret, u32 translations,
                         u32 cache_sync_bytes)
{
  perf_observation actual;
  perf_observation expected;
  u32 end_instret;
  u32 raw_instret;
  int equal;

  run_program(&g_program, iterations);
  end_instret = read_instret();
  rv32im_perf_measure_end();
  raw_instret = end_instret - start_instret;
  capture_observation(&actual);
  reference_run(&g_program, iterations, &expected);
  equal = observations_equal(&actual, &expected);
  print_perf_result(equal ? "PASS" : "FAIL", phase,
                    equal ? "state_equal" : "state_or_counter_mismatch",
                    iterations, raw_instret, &actual,
                    translations, cache_sync_bytes);
  if (!equal)
  {
    put_raw("result=FAIL command=rv32im-perf-debug workload=");
    put_raw(g_program.name);
    put_raw(" phase=");
    put_raw(phase);
    put_raw(" actual_state=");
    put_u32_hex(actual.state_hash);
    put_raw(" expected_state=");
    put_u32_hex(expected.state_hash);
    put_raw(" actual_memory=");
    put_u32_hex(actual.memory_hash);
    put_raw(" expected_memory=");
    put_u32_hex(expected.memory_hash);
    put_raw(" actual_scheduler=");
    put_u32_hex(actual.scheduler_hash);
    put_raw(" expected_scheduler=");
    put_u32_hex(expected.scheduler_hash);
    put_raw(" actual_trace=");
    put_u32_hex(actual.trace_hash);
    put_raw(" expected_trace=");
    put_u32_hex(expected.trace_hash);
    put_raw(" actual_reads=");
    put_u32_dec(actual.read8_calls);
    put_chr(',');
    put_u32_dec(actual.read16_calls);
    put_chr(',');
    put_u32_dec(actual.read32_calls);
    put_raw(" expected_reads=");
    put_u32_dec(expected.read8_calls);
    put_chr(',');
    put_u32_dec(expected.read16_calls);
    put_chr(',');
    put_u32_dec(expected.read32_calls);
    put_raw(" actual_lookup_terminal_update=");
    put_u32_dec(actual.lookup_calls);
    put_chr(',');
    put_u32_dec(actual.terminal_calls);
    put_chr(',');
    put_u32_dec(actual.update_calls);
    put_raw(" expected_lookup_terminal_update=");
    put_u32_dec(expected.lookup_calls);
    put_chr(',');
    put_u32_dec(expected.terminal_calls);
    put_chr(',');
    put_u32_dec(expected.update_calls);
    put_raw(" last_update_cycles=");
    put_u32_hex((u32)g_last_update_cycles);
    put_raw(" last_update_pc=");
    put_u32_hex(g_last_update_pc);
    put_chr('\n');
  }
  return equal;
}

static int run_workload(perf_workload_kind kind)
{
  const char *reason = "unknown";
  u32 start_instret;
  u32 warm_start;
  u32 cold_code_bytes;

  reset_observation_counters();
  rv32im_perf_measure_begin();
  start_instret = read_instret();
  if (!build_program(kind, &reason))
  {
    rv32im_perf_measure_end();
    put_raw("result=FAIL command=rv32im-perf workload=unknown phase=cold ");
    put_raw("backend=rv32im reason=");
    put_raw(reason);
    put_chr('\n');
    return 0;
  }
  cold_code_bytes = g_program.code_bytes;
  reset_guest_state(&g_program);
  reset_observation_counters();
  if (!measure_phase("cold", 1, start_instret,
                     g_program.block_count, cold_code_bytes))
    return 0;

  reset_guest_state(&g_program);
  reset_observation_counters();
  run_program(&g_program, PERF_WARMUP_RUNS);
  reset_guest_state(&g_program);
  reset_observation_counters();
  rv32im_perf_measure_begin();
  warm_start = read_instret();
  if (!measure_phase("warm", g_program.warm_runs, warm_start, 0, 0))
    return 0;
  return 1;
}

void execute_arm(u32 cycles)
{
  (void)cycles;
  g_execute_arm_calls++;
}

u32 function_cc read_memory8(u32 address)
{
  u32 value = memory_value(8, address);
  g_read8_calls++;
  g_memory_hash = memory_event_hash(g_memory_hash, 8, address, value);
  return value;
}

u32 function_cc read_memory8s(u32 address)
{
  u32 value = read_memory8(address);
  return (u32)(s32)(s8)value;
}

u32 function_cc read_memory16(u32 address)
{
  u32 value = memory_value(16, address);
  g_read16_calls++;
  g_memory_hash = memory_event_hash(g_memory_hash, 16, address, value);
  return value;
}

u32 function_cc read_memory16s(u32 address)
{
  u32 value = read_memory16(address);
  return (u32)(s32)(s16)value;
}

u32 function_cc read_memory32(u32 address)
{
  u32 value = memory_value(32, address);
  g_read32_calls++;
  g_memory_hash = memory_event_hash(g_memory_hash, 32, address, value);
  return value;
}

cpu_alert_type function_cc write_memory8(u32 address, u8 value)
{
  (void)address;
  (void)value;
  return CPU_ALERT_NONE;
}

cpu_alert_type function_cc write_memory16(u32 address, u16 value)
{
  (void)address;
  (void)value;
  return CPU_ALERT_NONE;
}

cpu_alert_type function_cc write_memory32(u32 address, u32 value)
{
  (void)address;
  (void)value;
  return CPU_ALERT_NONE;
}

u32 check_and_raise_interrupts(void)
{
  return 0;
}

void flush_translation_cache_ram(void)
{
}

void set_cpu_mode(u32 new_mode)
{
  reg[CPU_MODE] = new_mode;
}

u32 function_cc update_gba(int remaining_cycles)
{
  g_update_calls++;
  g_last_update_cycles = remaining_cycles;
  g_last_update_pc = reg[REG_PC];
  g_scheduler_hash = fnv_u32(g_scheduler_hash, (u32)remaining_cycles);
  g_scheduler_hash = fnv_u32(g_scheduler_hash, PERF_FRAME_COMPLETE);
  g_scheduler_hash = fnv_u32(g_scheduler_hash, reg[REG_PC]);
  return PERF_FRAME_COMPLETE;
}

u8 function_cc *block_lookup_address_arm(u32 pc)
{
  u32 i;

  g_lookup_calls++;
  g_trace_hash = fnv_u32(g_trace_hash, pc);
  for (i = 0; i < g_program.entry_count; i++)
  {
    if (g_program.pcs[i] == pc)
      return g_program.entries[i];
  }
  return (u8 *)0;
}

u8 function_cc *block_lookup_address_thumb(u32 pc)
{
  (void)pc;
  return (u8 *)0;
}

void init_bios_hooks(void)
{
}

void riscv_note_runtime_fallback(u32 kind, u32 pc, u32 thumb,
                                 u32 lookup_result, u32 cycles_remaining)
{
  (void)pc;
  (void)thumb;
  (void)lookup_result;
  (void)cycles_remaining;
  g_fallbacks++;
  if (kind == RISCV_RUNTIME_FALLBACK_INITIAL_LOOKUP)
    g_initial_lookup_fallbacks++;
  else if (kind == RISCV_RUNTIME_FALLBACK_RELOOKUP)
    g_relookup_fallbacks++;
  else if (kind == RISCV_RUNTIME_FALLBACK_UNSUPPORTED)
    g_unsupported_fallbacks++;
}

void riscv_note_runtime_block_emit(u32 start_pc, u32 end_pc, u32 thumb,
                                   u32 code_bytes)
{
  (void)start_pc;
  (void)end_pc;
  (void)thumb;
  (void)code_bytes;
}

void riscv_note_runtime_block_execute(u32 start_pc, u32 end_pc, u32 thumb)
{
  (void)start_pc;
  (void)end_pc;
  (void)thumb;
  g_terminal_calls++;
}

static int run_rdinstret_probe(void)
{
  u32 first;
  u32 second;
  u32 control_start;
  u32 control_end;
  u32 control_raw;
  u32 control_hash;

  rv32im_perf_measure_begin();
  first = read_instret();
  second = read_instret();
  rv32im_perf_measure_end();
  g_rdinstret_csr_overhead = second - first;
  if (!g_rdinstret_csr_overhead)
  {
    put_raw("result=FAIL command=rv32im-perf workload=control phase=probe ");
    put_raw("backend=rv32im reason=rdinstret_not_monotonic\n");
    return 0;
  }

  rv32im_perf_measure_begin();
  control_start = read_instret();
  control_hash = run_control_window(4096);
  control_end = read_instret();
  rv32im_perf_measure_end();
  control_raw = control_end - control_start;
  if (control_raw <= g_rdinstret_csr_overhead)
  {
    put_raw("result=FAIL command=rv32im-perf workload=control phase=probe ");
    put_raw("backend=rv32im reason=rdinstret_control_invalid\n");
    return 0;
  }

  put_raw("result=PASS command=rv32im-perf workload=control phase=probe ");
  put_raw("backend=rv32im guest_insns=0 guest_cycles=0 rdinstret_csr_raw=");
  put_u32_dec(control_raw);
  put_raw(" rdinstret_csr_overhead=");
  put_u32_dec(g_rdinstret_csr_overhead);
  put_raw(" rdinstret_csr_net=");
  put_u32_dec(control_raw - g_rdinstret_csr_overhead);
  put_raw(" control_iterations=4096 control_hash=");
  put_u32_hex(control_hash);
  put_raw(" harness_mode=runtime_fixture counter_source=rdinstret_csr_diagnostic ");
  put_raw("rdinstret_csr_verified=0 reason=rdinstret_csr_diagnostic_available\n");
  return 1;
}

void _start(void)
{
  long map_result = syscall6(SYS_MMAP, 0, PERF_CODE_BYTES,
                             PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if ((u32)map_result >= 0xfffff000u)
  {
    put_raw("result=FAIL command=rv32im-perf workload=control phase=setup ");
    put_raw("backend=rv32im reason=exec_mmap_failed\n");
    sys_exit(1);
  }
  g_perf_code = (u8 *)map_result;

  if (!run_rdinstret_probe() ||
#if !defined(RV32IM_PERF_BASELINE_PROFILE)
      !verify_mapped_alu_encodings() ||
      !verify_mapped_alu_semantics() ||
#endif
      !run_workload(PERF_WORKLOAD_MAPPED_ALU) ||
      !run_workload(PERF_WORKLOAD_MEMORY_READ) ||
      !run_workload(PERF_WORKLOAD_BRANCH_CHAIN) ||
      !run_workload(PERF_WORKLOAD_SCHEDULER) ||
      !run_workload(PERF_WORKLOAD_MIXED))
  {
    sys_exit(1);
  }

  put_raw("result=PASS command=rv32im-perf workload=all phase=summary ");
  put_raw("backend=rv32im workloads=5 cold=5 warm=5 repeatable=1 profile=");
#if defined(RV32IM_PERF_BASELINE_PROFILE)
  put_raw("mapped_alu_baseline ");
#else
  put_raw("optimized ");
#endif
  put_raw("reason=perf_suite_complete\n");
  sys_exit(0);
}
