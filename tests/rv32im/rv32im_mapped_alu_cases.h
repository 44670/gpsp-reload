#ifndef RV32IM_MAPPED_ALU_CASES_H
#define RV32IM_MAPPED_ALU_CASES_H

typedef enum rv32im_mapped_alu_case_kind
{
  RV32IM_MAPPED_ALU_DATA_PROC = 0,
  RV32IM_MAPPED_ALU_MULTIPLY
} rv32im_mapped_alu_case_kind;

typedef struct rv32im_mapped_alu_case
{
  const char *name;
  rv32im_mapped_alu_case_kind kind;
  u32 op;
  u32 rn;
  u32 rd;
  u32 rm;
  u32 rs;
} rv32im_mapped_alu_case;

static const rv32im_mapped_alu_case rv32im_mapped_alu_cases[] =
{
  { "add",             RV32IM_MAPPED_ALU_DATA_PROC, 0x4u,  0u,  2u,  1u, 0u },
  { "sub_alias_rn",    RV32IM_MAPPED_ALU_DATA_PROC, 0x2u,  0u,  0u,  1u, 0u },
  { "rsb_alias_rm",    RV32IM_MAPPED_ALU_DATA_PROC, 0x3u,  0u,  1u,  1u, 0u },
  { "eor",             RV32IM_MAPPED_ALU_DATA_PROC, 0x1u,  5u,  8u, 14u, 0u },
  { "and",             RV32IM_MAPPED_ALU_DATA_PROC, 0x0u,  3u,  4u,  2u, 0u },
  { "orr",             RV32IM_MAPPED_ALU_DATA_PROC, 0xcu, 11u, 12u, 10u, 0u },
  { "mov",             RV32IM_MAPPED_ALU_DATA_PROC, 0xdu,  0u,  6u,  9u, 0u },
  { "mvn",             RV32IM_MAPPED_ALU_DATA_PROC, 0xfu,  0u, 10u,  7u, 0u },
  { "bic",             RV32IM_MAPPED_ALU_DATA_PROC, 0xeu,  1u,  3u,  2u, 0u },
  { "bic_alias_rm",    RV32IM_MAPPED_ALU_DATA_PROC, 0xeu,  1u,  0u,  0u, 0u },
  { "add_all_alias",   RV32IM_MAPPED_ALU_DATA_PROC, 0x4u,  4u,  4u,  4u, 0u },
  { "add_sp_lr_alias", RV32IM_MAPPED_ALU_DATA_PROC, 0x4u, 13u, 14u, 14u, 0u },
  { "mul",             RV32IM_MAPPED_ALU_MULTIPLY,  0x0u,  0u,  4u,  1u, 2u },
  { "mul_alias_rs",    RV32IM_MAPPED_ALU_MULTIPLY,  0x0u,  0u,  2u,  1u, 2u },
  { "mul_sources_alias", RV32IM_MAPPED_ALU_MULTIPLY, 0x0u, 0u, 14u, 13u, 13u },
};

#define RV32IM_MAPPED_ALU_CASE_COUNT \
  (sizeof(rv32im_mapped_alu_cases) / sizeof(rv32im_mapped_alu_cases[0]))
#define RV32IM_MAPPED_ALU_FIRST_MULTIPLY_CASE 12u

static u32 rv32im_mapped_alu_opcode(const rv32im_mapped_alu_case *item)
{
  if (item->kind == RV32IM_MAPPED_ALU_MULTIPLY)
  {
    return 0xe0000090u | (item->rd << 16) | (item->rs << 8) | item->rm;
  }

  return 0xe0000000u | (item->op << 21) | (item->rn << 16) |
    (item->rd << 12) | item->rm;
}

static void rv32im_mapped_alu_initial_regs(u32 *values)
{
  static const u32 initial[15] =
  {
    0x00000000u, 0xffffffffu, 0x80000000u, 0x7fffffffu,
    0x01234567u, 0x89abcdefu, 0x55555555u, 0xaaaaaaaau,
    0x0000ffffu, 0xffff0000u, 0x13579bdfu, 0x2468ace0u,
    0xdeadbeefu, 0x03007f00u, 0x10203040u,
  };
  u32 i;

  for (i = 0; i < 15u; i++)
    values[i] = initial[i];
}

static u32 rv32im_mapped_alu_state_hash(const u32 *values)
{
  u32 hash = 2166136261u;
  u32 i;

  for (i = 0; i <= 16u; i++)
  {
    u32 value = values[i];
    u32 byte;

    for (byte = 0; byte < 4u; byte++)
    {
      hash ^= (value >> (byte * 8u)) & 0xffu;
      hash *= 16777619u;
    }
  }
  return hash;
}

#endif
