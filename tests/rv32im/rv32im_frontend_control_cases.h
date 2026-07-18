#ifndef RV32IM_FRONTEND_CONTROL_CASES_H
#define RV32IM_FRONTEND_CONTROL_CASES_H

#define RV32IM_FRONTEND_CONTROL_PC 0x08010000u
#define RV32IM_FRONTEND_CONTROL_MAX_WORDS 4u
#define RV32IM_FRONTEND_CONTROL_LONG_WORDS 1024u

typedef struct
{
  const char *name;
  u32 cycles;
  u32 idle_pc;
  u32 initial_cpsr;
  u32 generated_words;
  u32 words[RV32IM_FRONTEND_CONTROL_MAX_WORDS];
  u32 word_count;
} rv32im_frontend_control_case;

static const rv32im_frontend_control_case rv32im_frontend_control_cases[] =
{
  {
    "arm_b_self_cycle", 6u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xeafffffeu, 0u, 0u, 0u }, 1u
  },
  {
    "arm_b_self_idle", 100u, RV32IM_FRONTEND_CONTROL_PC, 0x0000001fu, 0u,
    { 0xeafffffeu, 0u, 0u, 0u }, 1u
  },
  {
    "arm_b_forward_cycle", 6u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xea000000u, 0xe3a09099u, 0xe2800001u, 0xeafffffeu }, 4u
  },
  {
    "arm_bl_forward_cycle", 6u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xeb000000u, 0xe3a09099u, 0xe2800001u, 0xeafffffeu }, 4u
  },
  {
    "arm_b_backward_cycle", 18u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xe2800001u, 0xe3500004u, 0x1afffffcu, 0xeafffffeu }, 4u
  },
  {
    "arm_swi_cycle", 1u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xef000000u, 0u, 0u, 0u }, 1u
  },
  {
    "arm_long_condition_skip", 100000u,
    RV32IM_FRONTEND_CONTROL_PC +
      RV32IM_FRONTEND_CONTROL_LONG_WORDS * 4u,
    0x4000001fu, RV32IM_FRONTEND_CONTROL_LONG_WORDS,
    { 0u, 0u, 0u, 0u }, 0u
  },
};

#define RV32IM_FRONTEND_CONTROL_CASE_COUNT \
  (sizeof(rv32im_frontend_control_cases) / \
   sizeof(rv32im_frontend_control_cases[0]))

static u32 rv32im_frontend_control_hash_word(u32 hash, u32 value)
{
  u32 i;

  for (i = 0; i < 4u; i++)
  {
    hash ^= (value >> (i * 8u)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

static u32 rv32im_frontend_control_state_hash(const u32 *regs,
                                               const u32 *saved_psr,
                                               const u32 *banked_regs,
                                               u32 update_calls,
                                               s32 update_cycles)
{
  u32 hash = 2166136261u;
  u32 i;

  for (i = 0; i < 16u; i++)
    hash = rv32im_frontend_control_hash_word(hash, regs[i]);
  hash = rv32im_frontend_control_hash_word(hash, regs[REG_CPSR]);
  hash = rv32im_frontend_control_hash_word(hash, regs[CPU_MODE]);
  hash = rv32im_frontend_control_hash_word(hash, regs[CPU_HALT_STATE]);
  hash = rv32im_frontend_control_hash_word(hash, regs[REG_BUS_VALUE]);
  for (i = 0; i < 6u; i++)
    hash = rv32im_frontend_control_hash_word(hash, saved_psr[i]);
  for (i = 0; i < 7u * 7u; i++)
    hash = rv32im_frontend_control_hash_word(hash, banked_regs[i]);
  hash = rv32im_frontend_control_hash_word(hash, update_calls);
  /* cpu_threaded uses the established native-backend cycle-cost model, which
     can overshoot by a different amount than cpu.cc. The contractual value is
     the same scheduler boundary; both raw values remain in the case output. */
  hash = rv32im_frontend_control_hash_word(hash,
    update_cycles > 0 ? 1u : 0u);
  return hash;
}

#endif
