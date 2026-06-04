typedef unsigned int u32;
typedef signed int s32;
typedef unsigned char u8;
typedef unsigned int usize;

#include "riscv_codegen.h"

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

struct arm_fixture_state
{
  u32 r[16];
};

struct arm_mem_fixture_state
{
  u32 r[16];
  u8 mem[64];
};

struct arm_branch_fixture_state
{
  u32 word[18];
};

struct scheduler_fixture_state
{
  u32 word[3];
};

static const u32 fixture_ops[] =
{
  0xe0802001u, /* add r2, r0, r1 */
  0xe0423001u, /* sub r3, r2, r1 */
  0xe0004001u, /* and r4, r0, r1 */
  0xe1805001u, /* orr r5, r0, r1 */
  0xe0206001u, /* eor r6, r0, r1 */
  0xe3a0707fu, /* mov r7, #0x7f */
  0xe0878002u, /* add r8, r7, r2 */
  0xe1c09001u, /* bic r9, r0, r1 */
  0xe3e0a0ffu, /* mvn r10, #0xff */
  0xe060b001u, /* rsb r11, r0, r1 */
};

static const u32 load_store_ops[] =
{
  0xe58a0000u, /* str r0, [r10] */
  0xe58a1004u, /* str r1, [r10, #4] */
  0xe59a2004u, /* ldr r2, [r10, #4] */
  0xe5ca3008u, /* strb r3, [r10, #8] */
  0xe5da4008u, /* ldrb r4, [r10, #8] */
  0xe59a5000u, /* ldr r5, [r10] */
  0xe58a500cu, /* str r5, [r10, #12] */
  0xe59a600cu, /* ldr r6, [r10, #12] */
};

static const u32 branch_direct_op = 0xea000001u;   /* b +12 */
static const u32 branch_indirect_op = 0xe12fff13u; /* bx r3 */

#define MEM_FIXTURE_BASE_OFFSET (16u * 4u + 16u)
#define MEM_FIXTURE_BYTES 64u

#define BRANCH_WORD_PC 15u
#define BRANCH_WORD_CPSR 16u
#define BRANCH_WORD_EXIT_REASON 17u
#define BRANCH_STATE_WORDS 18u
#define BRANCH_EXIT_DIRECT 1u
#define BRANCH_EXIT_INDIRECT 2u
#define ARM_CPSR_T 0x20u

#define SCHED_WORD_CYCLES 0u
#define SCHED_WORD_ALERT 1u
#define SCHED_WORD_EXIT_REASON 2u
#define SCHED_STATE_WORDS 3u
#define SCHED_CASES 3u
#define SCHED_BLOCK_CYCLES 32
#define SCHED_EXIT_CONTINUE 0u
#define SCHED_EXIT_CYCLES 1u
#define SCHED_EXIT_ALERT 2u

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

static u32 fnv1a_words(const u32 *data, usize words)
{
  u32 hash = 2166136261u;
  usize i;

  for (i = 0; i < words; i++)
  {
    u32 value = data[i];
    unsigned byte;
    for (byte = 0; byte < 4; byte++)
    {
      hash ^= (value >> (byte * 8)) & 0xffu;
      hash *= 16777619u;
    }
  }

  return hash;
}

static u32 fnv1a_bytes(const u8 *data, usize bytes)
{
  u32 hash = 2166136261u;
  usize i;

  for (i = 0; i < bytes; i++)
  {
    hash ^= data[i];
    hash *= 16777619u;
  }

  return hash;
}

static u32 fnv1a_scheduler_states(const struct scheduler_fixture_state *states,
                                  usize count)
{
  u32 hash = 2166136261u;
  usize i;
  usize j;

  for (i = 0; i < count; i++)
  {
    for (j = 0; j < SCHED_STATE_WORDS; j++)
    {
      u32 value = states[i].word[j];
      unsigned byte;
      for (byte = 0; byte < 4; byte++)
      {
        hash ^= (value >> (byte * 8)) & 0xffu;
        hash *= 16777619u;
      }
    }
  }

  return hash;
}

static void init_state(struct arm_fixture_state *state)
{
  unsigned i;
  for (i = 0; i < 16; i++)
    state->r[i] = 0x10000000u + (i * 0x01010101u);
  state->r[0] = 0x11223344u;
  state->r[1] = 0x01020304u;
}

static void init_mem_state(struct arm_mem_fixture_state *state)
{
  unsigned i;

  for (i = 0; i < 16; i++)
    state->r[i] = 0x20000000u + (i * 0x01010101u);
  for (i = 0; i < MEM_FIXTURE_BYTES; i++)
    state->mem[i] = (u8)(0x80u + (i * 3u));

  state->r[0] = 0x11223344u;
  state->r[1] = 0xaabbccddu;
  state->r[2] = 0;
  state->r[3] = 0x000000eeu;
  state->r[4] = 0;
  state->r[5] = 0;
  state->r[6] = 0;
  state->r[10] = MEM_FIXTURE_BASE_OFFSET;
}

static void init_branch_state(struct arm_branch_fixture_state *state)
{
  unsigned i;

  for (i = 0; i < BRANCH_STATE_WORDS; i++)
    state->word[i] = 0x30000000u + (i * 0x01010101u);

  state->word[3] = 0x02000101u;
  state->word[BRANCH_WORD_PC] = 0x08000000u;
  state->word[BRANCH_WORD_CPSR] = 0;
  state->word[BRANCH_WORD_EXIT_REASON] = 0;
}

static void init_scheduler_state(struct scheduler_fixture_state *state,
                                 u32 cycles, u32 alert)
{
  state->word[SCHED_WORD_CYCLES] = cycles;
  state->word[SCHED_WORD_ALERT] = alert;
  state->word[SCHED_WORD_EXIT_REASON] = 0xffffffffu;
}

static u32 arm_expand_imm(u32 opcode)
{
  u32 imm = opcode & 0xffu;
  u32 rot = ((opcode >> 8) & 0xfu) * 2u;
  if (!rot)
    return imm;
  return (imm >> rot) | (imm << (32u - rot));
}

static void run_reference_op(struct arm_fixture_state *state, u32 opcode)
{
  u32 arm_op = (opcode >> 21) & 0xfu;
  u32 imm = (opcode >> 25) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 rm = opcode & 0xfu;
  u32 lhs = state->r[rn];
  u32 rhs = imm ? arm_expand_imm(opcode) : state->r[rm];
  u32 result = 0;

  switch (arm_op)
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
    case 0x3:
      result = rhs - lhs;
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
    case 0xe:
      result = lhs & ~rhs;
      break;
    case 0xf:
      result = ~rhs;
      break;
    default:
      break;
  }

  state->r[rd] = result;
}

static void run_reference(struct arm_fixture_state *state)
{
  unsigned i;
  for (i = 0; i < sizeof(fixture_ops) / sizeof(fixture_ops[0]); i++)
    run_reference_op(state, fixture_ops[i]);
}

static u32 load_le32(const u8 *addr)
{
  return ((u32)addr[0]) |
         ((u32)addr[1] << 8) |
         ((u32)addr[2] << 16) |
         ((u32)addr[3] << 24);
}

static void store_le32(u8 *addr, u32 value)
{
  addr[0] = (u8)value;
  addr[1] = (u8)(value >> 8);
  addr[2] = (u8)(value >> 16);
  addr[3] = (u8)(value >> 24);
}

static void run_reference_load_store_op(struct arm_mem_fixture_state *state,
                                        u32 opcode)
{
  u32 is_byte = (opcode >> 22) & 1u;
  u32 up = (opcode >> 23) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 is_load = (opcode >> 20) & 1u;
  u32 offset = opcode & 0xfffu;
  u32 addr_offset = up ? (state->r[rn] + offset) : (state->r[rn] - offset);
  u8 *addr = ((u8 *)state) + addr_offset;

  if (is_load)
    state->r[rd] = is_byte ? addr[0] : load_le32(addr);
  else if (is_byte)
    addr[0] = (u8)state->r[rd];
  else
    store_le32(addr, state->r[rd]);
}

static void run_reference_load_store(struct arm_mem_fixture_state *state)
{
  unsigned i;
  for (i = 0; i < sizeof(load_store_ops) / sizeof(load_store_ops[0]); i++)
    run_reference_load_store_op(state, load_store_ops[i]);
}

static int arm_branch_delta(u32 opcode)
{
  u32 imm24 = opcode & 0x00ffffffu;
  int signed_imm;

  if (imm24 & 0x00800000u)
    signed_imm = (int)(imm24 | 0xff000000u);
  else
    signed_imm = (int)imm24;

  return (signed_imm * 4) + 8;
}

static void run_reference_branch_direct(struct arm_branch_fixture_state *state,
                                        u32 opcode)
{
  state->word[BRANCH_WORD_PC] += (u32)arm_branch_delta(opcode);
  state->word[BRANCH_WORD_EXIT_REASON] = BRANCH_EXIT_DIRECT;
}

static void run_reference_branch_indirect(struct arm_branch_fixture_state *state,
                                          u32 opcode)
{
  u32 rm = opcode & 0xfu;
  u32 target = state->word[rm];

  state->word[BRANCH_WORD_PC] = target & ~1u;
  state->word[BRANCH_WORD_CPSR] = (target & 1u) ? ARM_CPSR_T : 0;
  state->word[BRANCH_WORD_EXIT_REASON] = BRANCH_EXIT_INDIRECT;
}

static void run_reference_scheduler(struct scheduler_fixture_state *state)
{
  s32 cycles = (s32)state->word[SCHED_WORD_CYCLES];

  cycles -= SCHED_BLOCK_CYCLES;
  state->word[SCHED_WORD_CYCLES] = (u32)cycles;

  if (state->word[SCHED_WORD_ALERT])
    state->word[SCHED_WORD_EXIT_REASON] = SCHED_EXIT_ALERT;
  else if (cycles <= 0)
    state->word[SCHED_WORD_EXIT_REASON] = SCHED_EXIT_CYCLES;
  else
    state->word[SCHED_WORD_EXIT_REASON] = SCHED_EXIT_CONTINUE;
}

static void emit_li(u8 **code_ptr, riscv_reg_number rd, u32 value)
{
  u8 *translation_ptr = *code_ptr;

  if (value <= 2047u)
  {
    riscv_emit_addi(rd, riscv_reg_zero, value);
  }
  else
  {
    u32 upper = (value + 0x800u) >> 12;
    int lower = (int)(value - (upper << 12));
    riscv_emit_lui(rd, upper);
    if (lower)
    {
      riscv_emit_addi(rd, rd, lower);
    }
  }

  *code_ptr = translation_ptr;
}

static void emit_operand2(u8 **code_ptr, u32 opcode, riscv_reg_number rd)
{
  u8 *translation_ptr = *code_ptr;

  if ((opcode >> 25) & 1u)
  {
    *code_ptr = translation_ptr;
    emit_li(code_ptr, rd, arm_expand_imm(opcode));
    return;
  }

  riscv_emit_lw(rd, riscv_reg_a0, (opcode & 0xfu) * 4u);
  *code_ptr = translation_ptr;
}

static void emit_arm_data_proc_op(u8 **code_ptr, u32 opcode)
{
  u32 arm_op = (opcode >> 21) & 0xfu;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u8 *translation_ptr = *code_ptr;

  if (arm_op != 0xd && arm_op != 0xf)
  {
    riscv_emit_lw(riscv_reg_t0, riscv_reg_a0, rn * 4u);
  }
  *code_ptr = translation_ptr;

  emit_operand2(code_ptr, opcode, riscv_reg_t1);
  translation_ptr = *code_ptr;

  switch (arm_op)
  {
    case 0x0:
      riscv_emit_and(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      break;
    case 0x1:
      riscv_emit_xor(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      break;
    case 0x2:
      riscv_emit_sub(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      break;
    case 0x3:
      riscv_emit_sub(riscv_reg_t2, riscv_reg_t1, riscv_reg_t0);
      break;
    case 0x4:
      riscv_emit_add(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      break;
    case 0xc:
      riscv_emit_or(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      break;
    case 0xd:
      riscv_emit_add(riscv_reg_t2, riscv_reg_t1, riscv_reg_zero);
      break;
    case 0xe:
      riscv_emit_xori(riscv_reg_t1, riscv_reg_t1, -1);
      riscv_emit_and(riscv_reg_t2, riscv_reg_t0, riscv_reg_t1);
      break;
    case 0xf:
      riscv_emit_xori(riscv_reg_t2, riscv_reg_t1, -1);
      break;
    default:
      riscv_emit_add(riscv_reg_t2, riscv_reg_zero, riscv_reg_zero);
      break;
  }

  riscv_emit_sw(riscv_reg_t2, riscv_reg_a0, rd * 4u);
  *code_ptr = translation_ptr;
}

static void emit_arm_load_store_op(u8 **code_ptr, u32 opcode)
{
  u32 is_byte = (opcode >> 22) & 1u;
  u32 up = (opcode >> 23) & 1u;
  u32 rn = (opcode >> 16) & 0xfu;
  u32 rd = (opcode >> 12) & 0xfu;
  u32 is_load = (opcode >> 20) & 1u;
  u32 offset = opcode & 0xfffu;
  int rv_offset = up ? (int)offset : -(int)offset;
  u8 *translation_ptr = *code_ptr;

  riscv_emit_lw(riscv_reg_t0, riscv_reg_a0, rn * 4u);
  riscv_emit_add(riscv_reg_t0, riscv_reg_t0, riscv_reg_a0);

  if (is_load)
  {
    if (is_byte)
    {
      riscv_emit_lbu(riscv_reg_t1, riscv_reg_t0, rv_offset);
    }
    else
    {
      riscv_emit_lw(riscv_reg_t1, riscv_reg_t0, rv_offset);
    }
    riscv_emit_sw(riscv_reg_t1, riscv_reg_a0, rd * 4u);
  }
  else
  {
    riscv_emit_lw(riscv_reg_t1, riscv_reg_a0, rd * 4u);
    if (is_byte)
    {
      riscv_emit_sb(riscv_reg_t1, riscv_reg_t0, rv_offset);
    }
    else
    {
      riscv_emit_sw(riscv_reg_t1, riscv_reg_t0, rv_offset);
    }
  }

  *code_ptr = translation_ptr;
}

static u32 emit_branch_direct_block(u8 *code, u32 opcode)
{
  u8 *translation_ptr = code;

  riscv_emit_lw(riscv_reg_t0, riscv_reg_a0, BRANCH_WORD_PC * 4u);
  riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, arm_branch_delta(opcode));
  riscv_emit_sw(riscv_reg_t0, riscv_reg_a0, BRANCH_WORD_PC * 4u);
  riscv_emit_addi(riscv_reg_t1, riscv_reg_zero, BRANCH_EXIT_DIRECT);
  riscv_emit_sw(riscv_reg_t1, riscv_reg_a0, BRANCH_WORD_EXIT_REASON * 4u);
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);

  return (u32)(translation_ptr - code);
}

static u32 emit_branch_indirect_block(u8 *code, u32 opcode)
{
  u32 rm = opcode & 0xfu;
  u8 *translation_ptr = code;

  riscv_emit_lw(riscv_reg_t0, riscv_reg_a0, rm * 4u);
  riscv_emit_andi(riscv_reg_t1, riscv_reg_t0, -2);
  riscv_emit_sw(riscv_reg_t1, riscv_reg_a0, BRANCH_WORD_PC * 4u);
  riscv_emit_andi(riscv_reg_t2, riscv_reg_t0, 1);
  riscv_emit_slli(riscv_reg_t2, riscv_reg_t2, 5);
  riscv_emit_sw(riscv_reg_t2, riscv_reg_a0, BRANCH_WORD_CPSR * 4u);
  riscv_emit_addi(riscv_reg_t1, riscv_reg_zero, BRANCH_EXIT_INDIRECT);
  riscv_emit_sw(riscv_reg_t1, riscv_reg_a0, BRANCH_WORD_EXIT_REASON * 4u);
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);

  return (u32)(translation_ptr - code);
}

static u32 emit_scheduler_block(u8 *code)
{
  u8 *translation_ptr = code;

  riscv_emit_lw(riscv_reg_t0, riscv_reg_a0, SCHED_WORD_CYCLES * 4u);
  riscv_emit_addi(riscv_reg_t0, riscv_reg_t0, -SCHED_BLOCK_CYCLES);
  riscv_emit_sw(riscv_reg_t0, riscv_reg_a0, SCHED_WORD_CYCLES * 4u);
  riscv_emit_lw(riscv_reg_t1, riscv_reg_a0, SCHED_WORD_ALERT * 4u);
  riscv_emit_bne(riscv_reg_t1, riscv_reg_zero, 20);
  riscv_emit_bge(riscv_reg_zero, riscv_reg_t0, 28);
  riscv_emit_addi(riscv_reg_t2, riscv_reg_zero, SCHED_EXIT_CONTINUE);
  riscv_emit_sw(riscv_reg_t2, riscv_reg_a0, SCHED_WORD_EXIT_REASON * 4u);
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);
  riscv_emit_addi(riscv_reg_t2, riscv_reg_zero, SCHED_EXIT_ALERT);
  riscv_emit_sw(riscv_reg_t2, riscv_reg_a0, SCHED_WORD_EXIT_REASON * 4u);
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);
  riscv_emit_addi(riscv_reg_t2, riscv_reg_zero, SCHED_EXIT_CYCLES);
  riscv_emit_sw(riscv_reg_t2, riscv_reg_a0, SCHED_WORD_EXIT_REASON * 4u);
  riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);

  return (u32)(translation_ptr - code);
}

static u32 emit_fixture_block(u8 *code)
{
  u8 *translation_ptr = code;
  unsigned i;

  for (i = 0; i < sizeof(fixture_ops) / sizeof(fixture_ops[0]); i++)
  {
    u8 *next = translation_ptr;
    emit_arm_data_proc_op(&next, fixture_ops[i]);
    translation_ptr = next;
  }

  riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);
  return (u32)(translation_ptr - code);
}

static u32 emit_load_store_block(u8 *code)
{
  u8 *translation_ptr = code;
  unsigned i;

  for (i = 0; i < sizeof(load_store_ops) / sizeof(load_store_ops[0]); i++)
  {
    u8 *next = translation_ptr;
    emit_arm_load_store_op(&next, load_store_ops[i]);
    translation_ptr = next;
  }

  riscv_emit_jalr(riscv_reg_zero, riscv_reg_ra, 0);
  return (u32)(translation_ptr - code);
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

void _start(void)
{
  struct arm_fixture_state interp_state;
  struct arm_fixture_state jit_state;
  struct arm_mem_fixture_state interp_mem_state;
  struct arm_mem_fixture_state jit_mem_state;
  struct arm_branch_fixture_state interp_branch_state;
  struct arm_branch_fixture_state jit_branch_state;
  struct scheduler_fixture_state interp_sched_state[SCHED_CASES];
  struct scheduler_fixture_state jit_sched_state[SCHED_CASES];
  typedef void (*jit_block_fn)(void *);
  u8 *code = (u8 *)map_exec_page();
  u32 code_bytes;
  u32 flush_ret;
  u32 interp_hash;
  u32 jit_hash;
  u32 interp_mem_reg_hash;
  u32 jit_mem_reg_hash;
  u32 interp_mem_hash;
  u32 jit_mem_hash;
  u32 interp_branch_hash;
  u32 jit_branch_hash;
  u32 direct_code_bytes;
  u32 indirect_code_bytes;
  u32 interp_sched_hash;
  u32 jit_sched_hash;
  u32 sched_code_bytes;
  unsigned i;
  unsigned j;

  if (!code)
  {
    put_raw("result=FAIL command=exec_data_proc reason=mmap_failed\n");
    sys_exit(1);
  }

  init_state(&interp_state);
  init_state(&jit_state);

  code_bytes = emit_fixture_block(code);
  flush_ret = (u32)syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code,
                            (long)(code + code_bytes), 0);

  run_reference(&interp_state);
  ((jit_block_fn)code)(&jit_state);

  interp_hash = fnv1a_words(interp_state.r, 16);
  jit_hash = fnv1a_words(jit_state.r, 16);

  for (i = 0; i < 16; i++)
  {
    if (interp_state.r[i] != jit_state.r[i])
    {
      put_raw("result=FAIL command=exec_data_proc reg=");
      put_u32_dec(i);
      put_raw(" interp=");
      put_u32_hex(interp_state.r[i]);
      put_raw(" rv32im=");
      put_u32_hex(jit_state.r[i]);
      put_raw(" interp_hash=");
      put_u32_hex(interp_hash);
      put_raw(" rv32im_hash=");
      put_u32_hex(jit_hash);
      put_raw(" reason=state_mismatch\n");
      sys_exit(1);
    }
  }

  put_raw("result=PASS command=exec_data_proc ops=");
  put_u32_dec((u32)(sizeof(fixture_ops) / sizeof(fixture_ops[0])));
  put_raw(" code_bytes=");
  put_u32_dec(code_bytes);
  put_raw(" flush_ret=");
  put_u32_hex(flush_ret);
  put_raw(" interp_hash=");
  put_u32_hex(interp_hash);
  put_raw(" rv32im_hash=");
  put_u32_hex(jit_hash);
  put_raw(" reason=state_equal\n");

  init_mem_state(&interp_mem_state);
  init_mem_state(&jit_mem_state);

  code_bytes = emit_load_store_block(code);
  flush_ret = (u32)syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code,
                            (long)(code + code_bytes), 0);

  run_reference_load_store(&interp_mem_state);
  ((jit_block_fn)code)(&jit_mem_state);

  interp_mem_reg_hash = fnv1a_words(interp_mem_state.r, 16);
  jit_mem_reg_hash = fnv1a_words(jit_mem_state.r, 16);
  interp_mem_hash = fnv1a_bytes(interp_mem_state.mem, MEM_FIXTURE_BYTES);
  jit_mem_hash = fnv1a_bytes(jit_mem_state.mem, MEM_FIXTURE_BYTES);

  for (i = 0; i < 16; i++)
  {
    if (interp_mem_state.r[i] != jit_mem_state.r[i])
    {
      put_raw("result=FAIL command=exec_load_store reg=");
      put_u32_dec(i);
      put_raw(" interp=");
      put_u32_hex(interp_mem_state.r[i]);
      put_raw(" rv32im=");
      put_u32_hex(jit_mem_state.r[i]);
      put_raw(" interp_reg_hash=");
      put_u32_hex(interp_mem_reg_hash);
      put_raw(" rv32im_reg_hash=");
      put_u32_hex(jit_mem_reg_hash);
      put_raw(" reason=reg_mismatch\n");
      sys_exit(1);
    }
  }

  for (i = 0; i < MEM_FIXTURE_BYTES; i++)
  {
    if (interp_mem_state.mem[i] != jit_mem_state.mem[i])
    {
      put_raw("result=FAIL command=exec_load_store mem=");
      put_u32_dec(i);
      put_raw(" interp=");
      put_u32_hex(interp_mem_state.mem[i]);
      put_raw(" rv32im=");
      put_u32_hex(jit_mem_state.mem[i]);
      put_raw(" interp_mem_hash=");
      put_u32_hex(interp_mem_hash);
      put_raw(" rv32im_mem_hash=");
      put_u32_hex(jit_mem_hash);
      put_raw(" reason=mem_mismatch\n");
      sys_exit(1);
    }
  }

  put_raw("result=PASS command=exec_load_store ops=");
  put_u32_dec((u32)(sizeof(load_store_ops) / sizeof(load_store_ops[0])));
  put_raw(" code_bytes=");
  put_u32_dec(code_bytes);
  put_raw(" flush_ret=");
  put_u32_hex(flush_ret);
  put_raw(" interp_reg_hash=");
  put_u32_hex(interp_mem_reg_hash);
  put_raw(" rv32im_reg_hash=");
  put_u32_hex(jit_mem_reg_hash);
  put_raw(" interp_mem_hash=");
  put_u32_hex(interp_mem_hash);
  put_raw(" rv32im_mem_hash=");
  put_u32_hex(jit_mem_hash);
  put_raw(" reason=state_equal\n");

  init_branch_state(&interp_branch_state);
  init_branch_state(&jit_branch_state);

  direct_code_bytes = emit_branch_direct_block(code, branch_direct_op);
  flush_ret = (u32)syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code,
                            (long)(code + direct_code_bytes), 0);

  run_reference_branch_direct(&interp_branch_state, branch_direct_op);
  ((jit_block_fn)code)(&jit_branch_state);

  indirect_code_bytes = emit_branch_indirect_block(code, branch_indirect_op);
  flush_ret |= (u32)syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code,
                             (long)(code + indirect_code_bytes), 0);

  run_reference_branch_indirect(&interp_branch_state, branch_indirect_op);
  ((jit_block_fn)code)(&jit_branch_state);

  interp_branch_hash = fnv1a_words(interp_branch_state.word,
                                   BRANCH_STATE_WORDS);
  jit_branch_hash = fnv1a_words(jit_branch_state.word, BRANCH_STATE_WORDS);

  for (i = 0; i < BRANCH_STATE_WORDS; i++)
  {
    if (interp_branch_state.word[i] != jit_branch_state.word[i])
    {
      put_raw("result=FAIL command=exec_branch_exit word=");
      put_u32_dec(i);
      put_raw(" interp=");
      put_u32_hex(interp_branch_state.word[i]);
      put_raw(" rv32im=");
      put_u32_hex(jit_branch_state.word[i]);
      put_raw(" interp_hash=");
      put_u32_hex(interp_branch_hash);
      put_raw(" rv32im_hash=");
      put_u32_hex(jit_branch_hash);
      put_raw(" reason=branch_state_mismatch\n");
      sys_exit(1);
    }
  }

  put_raw("result=PASS command=exec_branch_exit direct_code_bytes=");
  put_u32_dec(direct_code_bytes);
  put_raw(" indirect_code_bytes=");
  put_u32_dec(indirect_code_bytes);
  put_raw(" flush_ret=");
  put_u32_hex(flush_ret);
  put_raw(" pc=");
  put_u32_hex(jit_branch_state.word[BRANCH_WORD_PC]);
  put_raw(" cpsr=");
  put_u32_hex(jit_branch_state.word[BRANCH_WORD_CPSR]);
  put_raw(" exit_reason=");
  put_u32_hex(jit_branch_state.word[BRANCH_WORD_EXIT_REASON]);
  put_raw(" interp_hash=");
  put_u32_hex(interp_branch_hash);
  put_raw(" rv32im_hash=");
  put_u32_hex(jit_branch_hash);
  put_raw(" reason=state_equal\n");

  init_scheduler_state(&interp_sched_state[0], 96u, 0);
  init_scheduler_state(&jit_sched_state[0], 96u, 0);
  init_scheduler_state(&interp_sched_state[1], 32u, 0);
  init_scheduler_state(&jit_sched_state[1], 32u, 0);
  init_scheduler_state(&interp_sched_state[2], 96u, 0x10u);
  init_scheduler_state(&jit_sched_state[2], 96u, 0x10u);

  sched_code_bytes = emit_scheduler_block(code);
  flush_ret = (u32)syscall3(SYS_RISCV_FLUSH_ICACHE, (long)code,
                            (long)(code + sched_code_bytes), 0);

  for (i = 0; i < SCHED_CASES; i++)
  {
    run_reference_scheduler(&interp_sched_state[i]);
    ((jit_block_fn)code)(&jit_sched_state[i]);
  }

  interp_sched_hash = fnv1a_scheduler_states(interp_sched_state, SCHED_CASES);
  jit_sched_hash = fnv1a_scheduler_states(jit_sched_state, SCHED_CASES);

  for (i = 0; i < SCHED_CASES; i++)
  {
    for (j = 0; j < SCHED_STATE_WORDS; j++)
    {
      if (interp_sched_state[i].word[j] != jit_sched_state[i].word[j])
      {
        put_raw("result=FAIL command=exec_scheduler_exit case=");
        put_u32_dec(i);
        put_raw(" word=");
        put_u32_dec(j);
        put_raw(" interp=");
        put_u32_hex(interp_sched_state[i].word[j]);
        put_raw(" rv32im=");
        put_u32_hex(jit_sched_state[i].word[j]);
        put_raw(" interp_hash=");
        put_u32_hex(interp_sched_hash);
        put_raw(" rv32im_hash=");
        put_u32_hex(jit_sched_hash);
        put_raw(" reason=scheduler_state_mismatch\n");
        sys_exit(1);
      }
    }
  }

  put_raw("result=PASS command=exec_scheduler_exit cases=");
  put_u32_dec(SCHED_CASES);
  put_raw(" code_bytes=");
  put_u32_dec(sched_code_bytes);
  put_raw(" flush_ret=");
  put_u32_hex(flush_ret);
  put_raw(" continue_exit=");
  put_u32_hex(jit_sched_state[0].word[SCHED_WORD_EXIT_REASON]);
  put_raw(" cycle_exit=");
  put_u32_hex(jit_sched_state[1].word[SCHED_WORD_EXIT_REASON]);
  put_raw(" alert_exit=");
  put_u32_hex(jit_sched_state[2].word[SCHED_WORD_EXIT_REASON]);
  put_raw(" interp_hash=");
  put_u32_hex(interp_sched_hash);
  put_raw(" rv32im_hash=");
  put_u32_hex(jit_sched_hash);
  put_raw(" reason=state_equal\n");
  sys_exit(0);
}
