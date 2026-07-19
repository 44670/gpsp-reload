#ifndef RV32IM_FRONTEND_CONTROL_CASES_H
#define RV32IM_FRONTEND_CONTROL_CASES_H

#define RV32IM_FRONTEND_CONTROL_PC 0x08010000u
#define RV32IM_FRONTEND_CONTROL_MAX_WORDS 16u
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
  u32 stale_update_index_plus_one;
} rv32im_frontend_control_case;

static const rv32im_frontend_control_case rv32im_frontend_control_cases[] =
{
  {
    "arm_b_self_cycle", 6u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xeafffffeu, 0u, 0u, 0u }, 1u, 0u
  },
  {
    "arm_b_self_idle", 100u, RV32IM_FRONTEND_CONTROL_PC, 0x0000001fu, 0u,
    { 0xeafffffeu, 0u, 0u, 0u }, 1u, 0u
  },
  {
    "arm_b_forward_cycle", 6u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xea000000u, 0xe3a09099u, 0xe2800001u, 0xeafffffeu }, 4u, 0u
  },
  {
    "arm_bl_forward_cycle", 6u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xeb000000u, 0xe3a09099u, 0xe2800001u, 0xeafffffeu }, 4u, 0u
  },
  {
    /* Match the GBA BIOS reset path: BL from System mode enters a routine
       that banks LR through Supervisor mode before returning with BX LR. */
    "arm_bl_mode_switch_return", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 8u, 0x0000001fu, 0u,
    {
      0xeb000001u, /* bl target */
      0xe3a08077u, /* mov r8, #0x77 */
      0xeafffffeu, /* b . */
      0xe3a000d3u, /* mov r0, #0xd3 */
      0xe129f000u, /* msr cpsr_fc, r0 */
      0xe3a0e000u, /* mov lr, #0 */
      0xe3a0005fu, /* mov r0, #0x5f */
      0xe129f000u, /* msr cpsr_fc, r0 */
      0xe12fff1eu, /* bx lr */
      0u, 0u, 0u
    }, 9u, 0u
  },
  {
    /* A read helper may clobber caller-saved mapped host registers.  An
       external direct chain must reconstruct every target-entry mapping. */
    "arm_read_then_external_chain", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 32u, 0x0000001fu, 0u,
    {
      0xe3a06402u, /* mov r6, #0x02000000 */
      0xe3a01055u, /* mov r1, #0x55 */
      0xe5965000u, /* ldr r5, [r6] */
      0xea000001u, /* b target */
      0xe3a01000u, /* unreachable */
      0xe1a00000u, /* unreachable */
      0xe2811001u, /* target: add r1, r1, #1 */
      0xe1a08001u, /* mov r8, r1 */
      0xeafffffeu, /* b . */
      0u, 0u, 0u
    }, 9u, 0u
  },
  {
    /* A helper inside a taken conditional body invalidates r0-r4's mapped
       host registers.  The conditional close must repair that predecessor
       before the following unconditional instruction consumes r1. */
    "arm_conditional_read_taken_merge", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 28u, 0x0000001fu, 0u,
    {
      0xe3a00000u, /* mov r0, #0 */
      0xe3a01055u, /* mov r1, #0x55 */
      0xe3a06402u, /* mov r6, #0x02000000 */
      0xe3500000u, /* cmp r0, #0 */
      0x05962000u, /* ldreq r2, [r6] */
      0xe2811001u, /* add r1, r1, #1 */
      0xe1a08001u, /* mov r8, r1 */
      0xeafffffeu, /* b . */
      0u, 0u, 0u, 0u
    }, 8u, 0u
  },
  {
    /* Exercise the other predecessor of the same merge.  The skipped helper
       path retains its entry mappings and must agree with dispatcher-only
       execution without executing the poison sequence. */
    "arm_conditional_read_skipped_merge", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 28u, 0x0000001fu, 0u,
    {
      0xe3a00001u, /* mov r0, #1 */
      0xe3a01055u, /* mov r1, #0x55 */
      0xe3a06402u, /* mov r6, #0x02000000 */
      0xe3500000u, /* cmp r0, #0 */
      0x05962000u, /* ldreq r2, [r6] */
      0xe2811001u, /* add r1, r1, #1 */
      0xe1a08001u, /* mov r8, r1 */
      0xeafffffeu, /* b . */
      0u, 0u, 0u, 0u
    }, 8u, 0u
  },
  {
    /* A proven-RAM word store normally uses the register-preserving leaf,
       but an odd address diverts through its C slow shim.  Consume every
       caller-saved guest mapping after the store, then read back the byte,
       so both the reload contract and the memory side effect are observable. */
    "arm_fast_store_unaligned_slow_reload", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 60u, 0x0000001fu, 0u,
    {
      0xe3a00011u, /* mov r0, #0x11 */
      0xe3a01022u, /* mov r1, #0x22 */
      0xe3a02033u, /* mov r2, #0x33 */
      0xe3a03044u, /* mov r3, #0x44 */
      0xe3a04055u, /* mov r4, #0x55 */
      0xe3a06402u, /* mov r6, #0x02000000 */
      0xe2866001u, /* add r6, r6, #1 */
      0xe5861000u, /* str r1, [r6] */
      0xe0800001u, /* add r0, r0, r1 */
      0xe0822003u, /* add r2, r2, r3 */
      0xe2844001u, /* add r4, r4, #1 */
      0xe5d65000u, /* ldrb r5, [r6] */
      0xe0808002u, /* add r8, r0, r2 */
      0xe0888004u, /* add r8, r8, r4 */
      0xe0888005u, /* add r8, r8, r5 */
      0xeafffffeu  /* b . */
    }, 16u, 0u
  },
  {
    /* Recreate an exception-return boundary without depending on BIOS code.
       Stateful CPSR/SPSR helpers invalidate every mapping; SUBS pc,lr,#4 then
       restores System mode, its banked SP/LR, NZCV, and dynamically enters a
       separately translated target that consumes each restored value. */
    "arm_spsr_exception_return_banked_state", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 36u, 0x0000001fu, 0u,
    {
      0xe3a000d3u, /* mov r0, #0xd3 */
      0xe129f000u, /* msr cpsr_fc, r0 */
      0xe59f1018u, /* ldr r1, [pc, #24] */
      0xe169f001u, /* msr spsr_fc, r1 */
      0xe59fe014u, /* ldr lr, [pc, #20] */
      0xe25ef004u, /* subs pc, lr, #4 */
      0xe1a0800du, /* target: mov r8, sp */
      0xe1a0900eu, /* mov r9, lr */
      0xe2a2a000u, /* adc r10, r2, #0 */
      0xeafffffeu, /* b . */
      0x6000001fu, /* restored CPSR: System, Z=1, C=1 */
      RV32IM_FRONTEND_CONTROL_PC + 28u
    }, 12u, 0u
  },
  {
    /* Cover the analogous STRH rule independently: bit zero is ignored by
       ARM7TDMI stores, and the fast helper's odd-address slow tail must both
       target the aligned halfword and restore r0-r4 before execution resumes. */
    "arm_fast_store_half_unaligned_slow_reload", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 60u, 0x0000001fu, 0u,
    {
      0xe3a00011u, /* mov r0, #0x11 */
      0xe3a01066u, /* mov r1, #0x66 */
      0xe3a02022u, /* mov r2, #0x22 */
      0xe3a03033u, /* mov r3, #0x33 */
      0xe3a04044u, /* mov r4, #0x44 */
      0xe3a06402u, /* mov r6, #0x02000000 */
      0xe2866001u, /* add r6, r6, #1 */
      0xe1c610b0u, /* strh r1, [r6] */
      0xe0800001u, /* add r0, r0, r1 */
      0xe0822003u, /* add r2, r2, r3 */
      0xe2844001u, /* add r4, r4, #1 */
      0xe5d65000u, /* ldrb r5, [r6] */
      0xe0808002u, /* add r8, r0, r2 */
      0xe0888004u, /* add r8, r8, r4 */
      0xe0888005u, /* add r8, r8, r5 */
      0xeafffffeu  /* b . */
    }, 16u, 0u
  },
  {
    /* Keep the value produced by a signed-halfword helper read distinct from
       the following PC-pool load.  The latter calls into the ROM reader and
       clobbers caller-saved host registers before CMP consumes both values. */
    "thumb_ldrsh_pc_pool_cmp", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 20u, 0x0000003fu, 0u,
    {
      0x203c4a07u, /* ldr r2, [pc, #28]; movs r0, #60 */
      0x48075e11u, /* ldrsh r1, [r2, r0]; ldr r0, [pc, #28] */
      0xd0024281u, /* cmp r1, r0; beq bad */
      0xe0012001u, /* movs r0, #1; b done */
      0x20ee46c0u, /* nop; bad: movs r0, #0xee */
      0x46c0e7feu, /* done: b done; nop */
      0u, 0u,
      0x02000000u, /* zero-filled EWRAM base */
      0x00001234u,
      0u, 0u
    }, 10u, 0u
  },
  {
    /* Reproduce the p.gba control-flow shape around 0x0800dc40.  The first
       conditional branch skips an external exit and enters a path that does
       a signed EWRAM read followed by a PC-pool constant comparison. */
    "thumb_internal_join_ldrsh_cmp", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 56u, 0x0000003fu, 0u,
    {
      0x4647b5f0u, /* push {r4-r7,lr}; mov r7,r8 */
      0x4909b480u, /* push {r7}; ldr r1, active-index address */
      0x28ff7808u, /* ldrb r0,[r1]; cmp r0,#255 */
      0xe013d100u, /* bne object-path; b done */
      0x1c014a07u, /* object-path: ldr r2,object-base; mov r1,r0 */
      0x18400108u, /* lsl r0,r1,#4; add r0,r0,r1 */
      0x18820080u, /* lsl r0,r0,#2; add r2,r0,r2 */
      0x5e11203cu, /* movs r0,#60; ldrsh r1,[r2,r0] */
      0x42814804u, /* ldr r0,expected; cmp r1,r0 */
      0xe007d000u, /* beq bad; b done */
      0xe00520eeu, /* bad: movs r0,#0xee; b done */
      0x020227c8u, /* zero active index */
      0x020205acu, /* zero-filled object base */
      0x00001234u,
      0xe7fee7feu, /* done: b done */
      0u
    }, 15u, 0u
  },
  {
    /* Reproduce the p.gba failure at 0x080a8848. The BL pair ends a block at
       halfword slot 25. A stale internal-target marker in that slot used to
       insert a scheduler exit between the two halves; resuming at BLH then
       formed the destination from the caller's old LR. */
    "thumb_bl_pair_stale_exit_slot", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 62u, 0x0000003fu, 0u,
    {
      0x46c046c0u, 0x46c046c0u, 0x46c046c0u, 0x46c046c0u,
      0x46c046c0u, 0x46c046c0u, 0x46c046c0u, 0x46c046c0u,
      0x46c046c0u, 0x46c046c0u, 0x46c046c0u, 0x46c046c0u,
      0xf804f000u, /* bl 0x0801003c */
      0xe7fe20eeu, /* wrong path: movs r0,#0xee; b . */
      0x46c046c0u,
      0xe7fe2042u, /* target: movs r0,#0x42; b . */
    }, 16u, 26u
  },
  {
    /* The scan pass records the BL target even though native Thumb BL exits
       through the dispatcher.  The emission pass must consume that slot so
       the following BNE is patched to its own target, not the BL routine. */
    "thumb_bl_exit_cursor_alignment", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 0x12u, 0x0000003fu, 0u,
    {
      0xd1012800u, /* cmp r0,#0; bne after_bl */
      0xf806f000u, /* bl routine */
      0xd1012802u, /* after_bl: cmp r0,#2; bne good */
      0xe7fe20eeu, /* wrong: movs r0,#0xee; b . */
      0xe7fe2042u, /* good: movs r0,#0x42; b . */
      0x47702001u, /* routine: movs r0,#1; bx lr */
      0u, 0u
    }, 6u, 0u
  },
  {
    /* A PC-pool read invalidates caller-saved mapped registers.  An external
       Thumb B chain must reload them before entering an independently
       translated target block. */
    "thumb_read_then_external_b_chain", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 0x24u, 0x0000003fu, 0u,
    {
      0x48052155u, /* movs r1,#0x55; ldr r0,[pc,#20] */
      0x0000e00cu, /* b target */
      0u, 0u, 0u, 0u,
      0x02000000u, /* PC-pool value */
      0u,
      0x46083101u, /* target: adds r1,#1; mov r0,r1 */
      0x0000e7feu, /* b . */
      0u, 0u
    }, 10u, 0u
  },
  {
    "arm_b_backward_cycle", 18u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xe2800001u, 0xe3500004u, 0x1afffffcu, 0xeafffffeu }, 4u, 0u
  },
  {
    "arm_swi_cycle", 1u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xef000000u, 0u, 0u, 0u }, 1u, 0u
  },
  {
    "arm_long_condition_skip", 100000u,
    RV32IM_FRONTEND_CONTROL_PC +
      RV32IM_FRONTEND_CONTROL_LONG_WORDS * 4u,
    0x4000001fu, RV32IM_FRONTEND_CONTROL_LONG_WORDS,
    { 0u, 0u, 0u, 0u }, 0u, 0u
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
