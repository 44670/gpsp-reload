#ifndef RV32IM_FRONTEND_CONTROL_CASES_H
#define RV32IM_FRONTEND_CONTROL_CASES_H

#define RV32IM_FRONTEND_CONTROL_PC 0x08010000u
#define RV32IM_FRONTEND_CONTROL_MAX_WORDS 96u
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
  u32 resume_updates;
  u32 resume_cycles;
  u32 entry_word_offset;
  u32 pretranslate_disable_arm_mask;
} rv32im_frontend_control_case;

typedef struct
{
  u32 offset;
  u32 word;
} rv32im_frontend_control_bios_word;

static const rv32im_frontend_control_bios_word
rv32im_frontend_control_swi_dispatcher[] =
{
  { 0x008u, 0xea000015u },
  { 0x018u, 0xea000000u }, { 0x020u, 0xe92d500fu },
  { 0x024u, 0xe3a00301u }, { 0x028u, 0xe1a0e00fu },
  { 0x02cu, 0xe510f004u }, { 0x030u, 0xe8bd500fu },
  { 0x034u, 0xe25ef004u },
  { 0x064u, 0xe92d5800u }, { 0x068u, 0xe55ec002u },
  { 0x06cu, 0xe28fb03cu }, { 0x070u, 0xe79bc10cu },
  { 0x074u, 0xe14fb000u }, { 0x078u, 0xe92d0800u },
  { 0x07cu, 0xe20bb080u }, { 0x080u, 0xe38bb01fu },
  { 0x084u, 0xe129f00bu }, { 0x088u, 0xe92d400cu },
  { 0x08cu, 0xe28fe000u }, { 0x090u, 0xe12fff1cu },
  { 0x094u, 0xe8bd400cu }, { 0x098u, 0xe3a0c0d3u },
  { 0x09cu, 0xe129f00cu }, { 0x0a0u, 0xe8bd0800u },
  { 0x0a4u, 0xe169f00bu }, { 0x0a8u, 0xe8bd5800u },
  { 0x0acu, 0xe1b0f00eu }, { 0x0dcu, 0x00000614u },
  { 0x0e0u, 0x00000720u },
  { 0x0e4u, 0x000007e4u }, { 0x0e8u, 0x000008e0u },
};

static const u32 rv32im_frontend_control_cpuset_words[] =
{
  0xe310040eu, 0xe52d4004u, 0x0a000018u, 0xe1a03582u,
  0xe1a034a3u, 0xe3c3360eu, 0xe0833000u, 0xe313040eu,
  0x0a000012u, 0xe3c234ffu, 0xe3120301u, 0xe3c3360eu,
  0x1a000010u, 0xe3120401u, 0x1a00001eu, 0xe3530000u,
  0x0a00000au, 0xe1a02000u, 0xe0803083u, 0xe0601001u,
  0xe3e044f1u, 0xe1520004u, 0x91d2c0b0u, 0x859fc0a0u,
  0xe181c0b2u, 0xe2822002u, 0xe1520003u, 0x1afffff8u,
  0xe8bd0010u, 0xe12fff1eu, 0xe3120401u, 0xe3c11003u,
  0xe3c02003u, 0x1a000015u, 0xe3530000u, 0x10621001u,
  0x13e0c4f1u, 0x0afffff5u, 0xe152000cu, 0x95920000u,
  0x859f0060u, 0xe2533001u, 0xe7810002u, 0xe2822004u,
  0x1afffff8u, 0xeaffffedu, 0xe350040fu, 0x31d020b0u,
  0x259f203cu, 0xe3530000u, 0x0affffe8u, 0xe0813083u,
  0xe0c120b2u, 0xe1510003u, 0x1afffffcu, 0xeaffffe3u,
  0xe352040fu, 0x35922000u, 0x259f2018u, 0xe3530000u,
  0x0affffdeu, 0xe2533001u, 0xe4812004u, 0x1afffffcu,
  0xeaffffdau, 0x00001cadu, 0x1cad1cadu,
};

static const u32 rv32im_frontend_control_cpufastset_words[] =
{
  0xe310040eu, 0xe52d4004u, 0x0a000017u, 0xe1a03582u,
  0xe1a034a3u, 0xe3c3360eu, 0xe0833000u, 0xe313040eu,
  0x0a000011u, 0xe3c244ffu, 0xe3120401u, 0xe3c03003u,
  0xe3c11003u, 0xe3c4460eu, 0x0a00000du, 0xe353040fu,
  0x35932000u, 0x259f2068u, 0xe3540000u, 0x0a000006u,
  0xe2813020u, 0xe4812004u, 0xe1510003u, 0x1afffffcu,
  0xe2444008u, 0xe3540000u, 0xcafffff8u, 0xe8bd0010u,
  0xe12fff1eu, 0xe3540000u, 0x10631001u, 0x13e0c4f1u,
  0x0afffff9u, 0xe2830020u, 0xe153000cu, 0x95932000u,
  0x859f201cu, 0xe7812003u, 0xe2833004u, 0xe1530000u,
  0x1afffff8u, 0xe2444008u, 0xe3540000u, 0xcafffff4u,
  0xeaffffedu, 0xbafffffbu,
};

static const u32 rv32im_frontend_control_bgaffineset_words[] =
{
  0xe3520000u, 0xe92d0ff0u, 0xe2422001u, 0x0a000037u,
  0xe2800014u, 0xe2811010u, 0xe150c0b4u, 0xe1a0c42cu,
  0xe28c3040u, 0xe59f40ccu, 0xe20330ffu, 0xe1a03083u,
  0xe15060f8u, 0xe19430f3u, 0xe0040693u, 0xe59f70b4u,
  0xe1a0c08cu, 0xe15080f6u, 0xe19750fcu, 0xe00c0895u,
  0xe15070bcu, 0xe1a04744u, 0xe0030398u, 0xe0050596u,
  0xe1a07807u, 0xe1a0a804u, 0xe1a07847u, 0xe1a0a84au,
  0xe00a0a97u, 0xe1a0c74cu, 0xe15060bau, 0xe1a0b80cu,
  0xe1a03743u, 0xe1a0b84bu, 0xe1a05745u, 0xe00b0b97u,
  0xe1a06806u, 0xe5107014u, 0xe1a09803u, 0xe1a06846u,
  0xe1a08805u, 0xe1a09849u, 0xe06aa007u, 0xe0090996u,
  0xe1a08848u, 0xe026a698u, 0xe5107010u, 0xe2422001u,
  0xe06b7007u, 0xe2655000u, 0xe0697007u, 0xe3720001u,
  0xe14141b0u, 0xe14150beu, 0xe141c0bcu, 0xe14130bau,
  0xe90100c0u, 0xe2800014u, 0xe2811010u, 0x1affffc9u,
  0xe8bd0ff0u, 0xe12fff1eu, 0x00002150u,
};

static const u32 rv32im_frontend_control_objaffineset_words[] =
{
  0xe3520000u, 0xe92d07f0u, 0xe2422001u, 0x0a000021u,
  0xe1a07083u, 0xe59fa084u, 0xe0878083u, 0xe2800008u,
  0xe0813003u, 0xe150c0b4u, 0xe1a0c42cu, 0xe28c4040u,
  0xe20440ffu, 0xe1a0c08cu, 0xe15060f8u, 0xe19a50fcu,
  0xe1a0c084u, 0xe19ac0fcu, 0xe15040f6u, 0xe0090596u,
  0xe006069cu, 0xe0050594u, 0xe00c0c94u, 0xe1a09749u,
  0xe2422001u, 0xe1a06746u, 0xe2699000u, 0xe1a05745u,
  0xe1a0c74cu, 0xe3720001u, 0xe1c160b0u, 0xe2800008u,
  0xe1c390b0u, 0xe18150b7u, 0xe183c0b7u, 0xe0811008u,
  0xe0833008u, 0x1affffe2u, 0xe8bd07f0u, 0xe12fff1eu,
  0x00002150u,
};

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
    /* The inverse merge is just as important: an unconditional helper first
       invalidates r1's host mapping, then a false conditional body reloads it
       only on the path that will be skipped.  The merge cannot advertise that
       true-path-only mapping to the following MOV. */
    "arm_conditional_true_only_mapping", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 28u, 0x0000001fu, 0u,
    {
      0xe3a01055u, /* mov r1, #0x55 */
      0xe3a06402u, /* mov r6, #0x02000000 */
      0xe5965000u, /* ldr r5, [r6]: invalidates caller-saved mappings */
      0xea000000u, /* b target: clears constants and creates a join gate */
      0xe1a00000u, /* unreachable */
      0x02812001u, /* target: addeq r2,r1,#1; false-path skips its reload */
      0xe1a08001u, /* mov r8, r1: must reload the merged state */
      0xeafffffeu  /* b . */
    }, 8u, 0u
  },
  {
    /* A late unsupported instruction rewinds the emitted block to its helper
       stub.  An earlier direct-exit patch site is then stale and must not be
       patched after recursive translation reuses that cache range. */
    "arm_unsupported_block_discards_exit_sites", 200u,
    RV32IM_FRONTEND_CONTROL_PC + 48u * 4u, 0x0000001fu, 0u,
    {
      0x1a00000eu, /* source: bne target (external to this short block) */
      0xe5910000u, /* ldr r0,[r1]: deliberately disabled for pretranslation */
      0xeafffffeu, /* b .: ends the unsupported source block */
      0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
      /* target: enough emitted code to overlap the discarded source site. */
      0xe2888001u, 0xe2888001u, 0xe2888001u, 0xe2888001u,
      0xe2888001u, 0xe2888001u, 0xe2888001u, 0xe2888001u,
      0xe2888001u, 0xe2888001u, 0xe2888001u, 0xe2888001u,
      0xe2888001u, 0xe2888001u, 0xe2888001u, 0xe2888001u,
      0xe2888001u, 0xe2888001u, 0xe2888001u, 0xe2888001u,
      0xe2888001u, 0xe2888001u, 0xe2888001u, 0xe2888001u,
      0xe2888001u, 0xe2888001u, 0xe2888001u, 0xe2888001u,
      0xe2888001u, 0xe2888001u, 0xe2888001u, 0xe2888001u,
      0xeafffffeu  /* idle */
    }, 49u, 0u, 0u, 0u, 16u, 0x00001000u
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
    /* The scan pass assigns one exit slot to each direct ARM branch.  If a
       compile-time-false branch is elided, it must not shift the patch target
       consumed by the next emitted branch. */
    "arm_known_false_exit_cursor", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 40u, 0x0000001fu, 0u,
    {
      0xe3a00000u, /* mov r0, #0 */
      0xe3500000u, /* cmp r0, #0: Z is compile-time true */
      0x1a000003u, /* bne bad: compile-time false, scan exit slot 0 */
      0xe3a01001u, /* mov r1, #1 */
      0xe3510001u, /* cmp r1, #1 */
      0x0a000002u, /* beq good: must consume scan exit slot 1 */
      0xe3a08011u, /* wrong fallthrough marker */
      0xe3a080eeu, /* bad: wrong exit target marker */
      0xea000000u, /* b done */
      0xe3a08042u, /* good */
      0xeafffffeu  /* done: b done */
    }, 11u, 0u
  },
  {
    /* A runtime-false post-index store leaves r1 unchanged.  Constant
       propagation must retain both possibilities instead of applying the
       writeback as though the conditional instruction were unconditional. */
    "arm_conditional_post_store_writeback_false", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 24u, 0x0000001fu, 0u,
    {
      0xe3a01402u, /* mov r1, #0x02000000 */
      0xe3a02077u, /* mov r2, #0x77 */
      0x04812004u, /* streq r2, [r1], #4: initial Z=0, so skip */
      0xe3510402u, /* cmp r1, #0x02000000 */
      0x03a08022u, /* moveq r8, #0x22 */
      0x13a08011u, /* movne r8, #0x11 */
      0xeafffffeu  /* b . */
    }, 7u, 0u
  },
  {
    /* HLE Div SWIs are instructions, not scan-pass exits.  Folding a false
       conditional Div must therefore not consume the direct-exit slot owned
       by the following BEQ. */
    "arm_known_false_hle_swi_exit_cursor", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 36u, 0x0000001fu, 0u,
    {
      0xe3a00000u, /* mov r0, #0 */
      0xe3500000u, /* cmp r0, #0: Z is compile-time true */
      0x1f060000u, /* swine 0x060000: folded false, no scan exit slot */
      0xe3a01001u, /* mov r1, #1 */
      0xe3510001u, /* cmp r1, #1 */
      0x0a000001u, /* beq good: owns scan exit slot 0 */
      0xe3a08011u, /* bad */
      0xea000000u, /* b done */
      0xe3a08042u, /* good */
      0xeafffffeu  /* done: b . */
    }, 10u, 0u
  },
  {
    /* Cover the same conditional-writeback merge for a pre-index load.  The
       skipped access must neither load r2 nor advance the constant base. */
    "arm_conditional_pre_load_writeback_false", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 24u, 0x0000001fu, 0u,
    {
      0xe3a01402u, /* mov r1, #0x02000000 */
      0xe3a02077u, /* mov r2, #0x77 */
      0x05b12004u, /* ldreq r2, [r1, #4]!: initial Z=0, so skip */
      0xe3510402u, /* cmp r1, #0x02000000 */
      0x03a08022u, /* moveq r8, #0x22 */
      0x13a08011u, /* movne r8, #0x11 */
      0xeafffffeu  /* b . */
    }, 7u, 0u
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
    /* The register-offset LDRSB may use the destination as its offset too.
       It must invalidate the MOVS constant before the following CMP/BLT;
       otherwise the frontend folds the branch from stale r1 = 6. */
    "thumb_ldrsb_rd_equals_ro", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 28u, 0x0000003fu, 0u,
    {
      0x20ed4a07u, /* ldr r2,[pc,#28]; movs r0,#0xed */
      0x21067190u, /* strb r0,[r2,#6]; movs r1,#6 */
      0x20aa2355u, /* bad marker in r3; good marker in r0 */
      0x29005651u, /* ldrsb r1,[r2,r1]; cmp r1,#0 */
      0x4698db02u, /* blt good; bad: mov r8,r3 */
      0x46c0e002u, /* b done; unreachable nop */
      0x46c04680u, /* good: mov r8,r0; timing nop */
      0x46c0e7feu, /* done: b done; nop */
      0x02000000u
    }, 9u, 0u
  },
  {
    /* SP-relative LDR encodes rd in bits 10:8, while bits 7:0 are only the
       offset.  Constant tracking must invalidate r5 here rather than r0, or
       the following CMP is folded from the stale MOVS r5,#0x77. */
    "thumb_sp_relative_ldr_clears_real_rd", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 18u, 0x0000003fu, 0u,
    {
      0x46854804u, /* ldr r0,[pc,#16]; mov sp,r0 */
      0x9d002577u, /* movs r5,#0x77; ldr r5,[sp,#0] (reads zero) */
      0xd0012d00u, /* cmp r5,#0; beq good */
      0xe0002211u, /* bad: movs r2,#0x11; b done */
      0xe7fe2222u, /* good: movs r2,#0x22; done: b done */
      0x02000000u
    }, 6u, 0u
  },
  {
    /* ADD pc,rm is an exit just like MOV pc,rm.  The unreachable CMP after
       it must not make the carry producer look dead: the separately
       translated target observes that carry before writing any flags. */
    "thumb_add_pc_preserves_exit_flags", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 24u, 0x0000003fu, 0u,
    {
      0x21012006u, /* movs r0,#6; movs r1,#1 */
      0x44870849u, /* lsrs r1,r1,#1 (C=1); add pc,r0 -> +16 */
      0x22112901u, /* unreachable cmp r1,#1 (C=0); movs r2,#0x11 */
      0x46c0e004u, /* b done; nop */
      0x2211d201u, /* target: bcs good; bad: movs r2,#0x11 */
      0x2222e000u, /* b done; good: movs r2,#0x22 */
      0x0000e7feu  /* done: b done */
    }, 7u, 0u
  },
  {
    /* gpSP treats an empty Thumb STM register list as a zero-transfer
       instruction.  Since no store helper runs, the native path must not
       test the stale a0 value left by block entry as though it were an alert
       result and skip the following ADD. */
    "thumb_empty_stm_does_not_take_store_alert", 100u,
    0xffffffffu, 0x0000003fu, 0u,
    {
      0x3101c000u, /* stmia r0!,{}; adds r1,#1 */
      0x0000e7feu  /* b . */
    }, 2u, 0u
  },
  {
    /* A taken conditional data-processing write to PC exits at that exact
       instruction.  The forward-scanned fallthrough remains in the same
       translated block, but must only be reachable when the predicate fails. */
    "arm_conditional_mov_pc_exits_immediately", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 16u, 0x0000001fu, 0u,
    {
      0xe59f000cu, /* ldr r0,[pc,#12]: target literal */
      0xe3510000u, /* cmp r1,#0: Z=1 */
      0x01a0f000u, /* moveq pc,r0: taken */
      0xe2888001u, /* bad fallthrough: must not execute */
      0xeafffffeu, /* target: b target */
      RV32IM_FRONTEND_CONTROL_PC + 16u
    }, 6u, 0u
  },
  {
    /* LDR pc has the same indirect-exit contract.  Merely setting the block's
       PC_WRITTEN flag is insufficient when scan_block retained a false-path
       fallthrough after this conditional instruction. */
    "arm_conditional_ldr_pc_exits_immediately", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 8u, 0x4000001fu, 0u,
    {
      0x059ff008u, /* ldreq pc,[pc,#8]: loads target literal at +16 */
      0xe2888001u, /* bad fallthrough: must not execute */
      0xeafffffeu, /* target: b target */
      0xe1a00000u,
      RV32IM_FRONTEND_CONTROL_PC + 8u
    }, 5u, 0u
  },
  {
    /* PC_WRITTEN describes the taken path of this MOVEQ, not the block's
       false-path finalizer.  With Z clear, execution must run the remaining
       1023 instructions and publish block_end_pc before the scheduler tail. */
    "arm_false_pc_writer_publishes_block_end", 100000u,
    RV32IM_FRONTEND_CONTROL_PC +
      RV32IM_FRONTEND_CONTROL_LONG_WORDS * 4u,
    0x0000001fu, RV32IM_FRONTEND_CONTROL_LONG_WORDS,
    {
      0x01a0f000u /* moveq pc,r0: false; generated ADDNEs execute */
    }, 1u, 0u
  },
  {
    /* The backward edge makes instruction 1 an in-block scheduler boundary.
       ADDS overwrites every flag later, so ordinary linear liveness considers
       CMP dead; nevertheless, cycle exhaustion can expose CMP's CPSR before
       ADDS runs. */
    "arm_cycle_boundary_preserves_dead_flags", 1u,
    0xffffffffu, 0x0000001fu, 0u,
    {
      0xe3500000u, /* cmp r0,#0: scheduler must observe Z=1,C=1 */
      0xe2911001u, /* adds r1,r1,#1: later all-flag overwrite */
      0xeafffffdu  /* b instruction 1 */
    }, 3u, 0u
  },
  {
    /* A conditional flag writer is only a maybe-definition.  When ADDEQS is
       skipped, the following BCS must still see CMP's carry; treating ADDEQS
       as an unconditional liveness kill leaves the block-entry carry instead. */
    "arm_false_conditional_flag_writer_preserves_old_flags", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 28u, 0x0000001fu, 0u,
    {
      0xe3a00205u, /* mov r0,#0x50000000 (Z=1,V=1 payload) */
      0xe128f000u, /* msr cpsr_f,r0 */
      0x12911001u, /* addsne r1,r1,#1: false, must preserve MSR flags */
      0x6a000001u, /* bvs good */
      0xe3a02011u, /* bad: mov r2,#0x11 */
      0xea000000u, /* b done */
      0xe3a02022u, /* good: mov r2,#0x22 */
      0xeafffffeu  /* done: b done */
    }, 8u, 0u
  },
  {
    /* TST changes N/Z but preserves the carry produced by LSRS.  Marking TST
       as a definite C writer lets dead-flag elimination delete that producer
       and flips the following BCS. */
    "thumb_tst_preserves_c", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 16u, 0x0000003fu, 0u,
    {
      0x21ff2001u, /* movs r0,#1; movs r1,#0xff */
      0x42080840u, /* lsrs r0,r0,#1 (C=1); tst r0,r1 */
      0x2211d201u, /* bcs good; bad: movs r2,#0x11 */
      0x2222e000u, /* b done; good: movs r2,#0x22 */
      0x0000e7feu  /* done: b done */
    }, 5u, 0u
  },
  {
    /* TST also preserves V.  0x80000000 - 1 sets overflow before TST, so BVS
       observes whether the upstream V producer survived liveness analysis. */
    "thumb_tst_preserves_v", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 18u, 0x0000003fu, 0u,
    {
      0x20802101u, /* movs r1,#1; movs r0,#0x80 */
      0x38010600u, /* lsls r0,r0,#24; subs r0,#1 (V=1) */
      0xd6014208u, /* tst r0,r1; bvs good */
      0xe0002211u, /* bad: movs r2,#0x11; b done */
      0xe7fe2222u  /* good: movs r2,#0x22; done: b done */
    }, 5u, 0u
  },
  {
    /* All Thumb logical N/Z writers preserve C/V, not only TST.  MOVS used to
       take the same dead-flag path and clear the carry produced by LSRS. */
    "thumb_mov_preserves_c", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 14u, 0x0000003fu, 0u,
    {
      0x08402001u, /* movs r0,#1; lsrs r0,r0,#1 (C=1) */
      0xd2012100u, /* movs r1,#0; bcs good */
      0xe0002211u, /* bad: movs r2,#0x11; b done */
      0xe7fe2222u  /* good: movs r2,#0x22; done: b done */
    }, 4u, 0u
  },
  {
    /* ARM TST updates N/Z (and shifter C when applicable) but always preserves
       V.  Dead-flag code generation must not zero that live V producer. */
    "arm_tst_preserves_v", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 28u, 0x0000001fu, 0u,
    {
      0xe3a00102u, /* mov r0, #0x80000000 */
      0xe2500001u, /* subs r0, r0, #1 (V=1) */
      0xe1100001u, /* tst r0, r1 (preserves V) */
      0x6a000001u, /* bvs good */
      0xe3a08011u, /* bad: mov r8, #0x11 */
      0xea000000u, /* b done */
      0xe3a08022u, /* good: mov r8, #0x22 */
      0xeafffffeu  /* done: b . */
    }, 8u, 0u
  },
  {
    /* MULS writes N/Z only.  C/V are architecturally preserved even when
       their producer is otherwise dead to the generated multiply. */
    "arm_muls_preserves_c", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 36u, 0x0000001fu, 0u,
    {
      0xe3a00000u, /* mov r0, #0 */
      0xe3500000u, /* cmp r0, #0 (C=1) */
      0xe3a01002u, /* mov r1, #2 */
      0xe3a02003u, /* mov r2, #3 */
      0xe0130291u, /* muls r3, r1, r2 (preserves C) */
      0x2a000001u, /* bcs good */
      0xe3a08011u, /* bad: mov r8, #0x11 */
      0xea000000u, /* b done */
      0xe3a08022u, /* good: mov r8, #0x22 */
      0xeafffffeu  /* done: b . */
    }, 10u, 0u
  },
  {
    /* The long-multiply emitter has the same partial-flag contract. */
    "arm_umulls_preserves_v", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 32u, 0x0000001fu, 0u,
    {
      0xe3a00102u, /* mov r0, #0x80000000 */
      0xe2500001u, /* subs r0, r0, #1 (V=1) */
      0xe3a01002u, /* mov r1, #2 */
      0xe0932190u, /* umulls r2, r3, r0, r1 (preserves V) */
      0x6a000001u, /* bvs good */
      0xe3a08011u, /* bad: mov r8, #0x11 */
      0xea000000u, /* b done */
      0xe3a08022u, /* good: mov r8, #0x22 */
      0xeafffffeu  /* done: b . */
    }, 9u, 0u
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
    /* A branch may enter the second half of a BL pair.  In that case BLH
       consumes the architectural LR value; it must not be folded with the
       linearly preceding (but unexecuted) prefix. */
    "thumb_internal_entry_to_blh", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 26u, 0x0000003fu, 0u,
    {
      0x4686a005u, /* adr r0,expected; mov lr,r0 */
      0x29002100u, /* movs r1,#0; cmp r1,#0 */
      0xe000d100u, /* bne prefix (not taken); b blh */
      0xf800f000u, /* unexecuted prefix; target: standalone blh */
      0xe7fe2011u, /* wrong folded target: movs r0,#0x11; b . */
      0x46c046c0u,
      0xe7fe2022u  /* expected: movs r0,#0x22; b . */
    }, 7u, 0u
  },
  {
    /* The converse path reaches the prefix linearly while BLH is also an
       internal entry.  Publishing the high-half LR before the target's
       scheduler gate keeps the ordinary pair path correct as well. */
    "thumb_linear_prefix_to_shared_blh", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 22u, 0x0000003fu, 0u,
    {
      0x29002101u, /* movs r1,#1; cmp r1,#0 */
      0xe000d100u, /* bne prefix (taken); b blh */
      0xf804f000u, /* prefix; shared target: blh +8 */
      0xe7fe2011u, /* wrong stale-LR path: movs r0,#0x11; b . */
      0x46c046c0u,
      0xe7fe2022u  /* expected: movs r0,#0x22; b . */
    }, 6u, 0u
  },
  {
    /* The edge into BLH can also occur later in scan order.  The scanner has
       already allocated a direct-pair exit when it reaches the backward B;
       emission must consume that stale pair slot when BLH becomes standalone,
       or the backward B is patched to the BL pair's nominal destination. */
    "thumb_backward_entry_to_blh", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 26u, 0x0000003fu, 0u,
    {
      0x4686a005u, /* adr r0,expected; mov lr,r0 */
      0x29002101u, /* movs r1,#1; cmp r1,#0 */
      0xf000d101u, /* bne after (taken); unexecuted prefix */
      0xe7fdf800u, /* shared BLH; after: b backward to BLH */
      0xe7fe2011u, /* wrong direct-pair target: movs r0,#0x11; b . */
      0x46c046c0u,
      0xe7fe2022u  /* expected: movs r0,#0x22; b . */
    }, 7u, 0u
  },
  {
    /* A standalone first half is an architectural LR write even when the
       following instruction is not BLH. */
    "thumb_standalone_bl_prefix_writes_lr", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 6u, 0x0000003fu, 0u,
    {
      0x4670f000u, /* prefix (+0); mov r0,lr */
      0xe7fe46c0u  /* nop; b . */
    }, 2u, 0u
  },
  {
    /* A shared BLH is not a direct pair exit in the scan pass.  Recording a
       phantom pair target shifts the exit cursor, so the later BNE is patched
       to BLH's linear target instead of its own good path. */
    "thumb_shared_blh_exit_cursor_alignment", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 26u, 0x0000003fu, 0u,
    {
      0x28002000u, /* movs r0,#0; cmp r0,#0 */
      0xd100d003u, /* beq after_blh; bne prefix */
      0xf000e000u, /* b shared_blh; prefix */
      0x2801f800u, /* shared_blh; after_blh: cmp r0,#1 */
      0x2011d102u, /* bne good; wrong: movs r0,#0x11 */
      0x46c0e7feu, /* wrong idle; nop */
      0xe7fe2022u  /* good: movs r0,#0x22; b . */
    }, 7u, 0u
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
    /* The folded literal still costs a guest load, while the native BNE
       condition gate consumes the accumulated debit exactly once.  A fixed
       scheduler budget makes either omission/double-charge visible in r1. */
    "thumb_pc_pool_conditional_cycle_loop", 40u,
    0xffffffffu, 0x0000003fu, 0u,
    {
      0x31014802u, /* loop: ldr r0,[pc,#8]; adds r1,#1 */
      0xd1fb4281u, /* cmp r1,r0; bne loop */
      0x46c0e7feu, /* b .; nop */
      0xffffffffu, /* literal keeps BNE taken for the measured budget */
      0u, 0u, 0u, 0u
    }, 4u, 0u
  },
  {
    "arm_b_backward_cycle", 18u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xe2800001u, 0xe3500004u, 0x1afffffcu, 0xeafffffeu }, 4u, 0u
  },
  {
    /* A nonterminal word load has the same +2 guest latency as a load at an
       exit.  Omitting it lets the native loop execute extra ADDs. */
    "arm_nonterminal_load_cycle_loop", 26u,
    0xffffffffu, 0x0000001fu, 0u,
    {
      0xe3a02402u, /* mov r2, #0x02000000 */
      0xe5920000u, /* loop: ldr r0, [r2] */
      0xe2811001u, /* add r1, r1, #1 */
      0xeafffffcu  /* b loop */
    }, 4u, 0u
  },
  {
    /* Conditional loads charge their +2 latency only on the taken body.  The
       backend defers that debit to the predicate merge so adjacent loads do
       not each need a cycle-counter round trip. */
    "arm_conditional_load_taken_cycle_loop", 26u,
    0xffffffffu, 0x4000001fu, 0u,
    {
      0xe3a02402u, /* mov r2, #0x02000000 */
      0x05920000u, /* loop: ldreq r0, [r2] (taken, Z=1) */
      0xe2811001u, /* add r1, r1, #1 */
      0xeafffffcu  /* b loop */
    }, 4u, 0u
  },
  {
    /* The complementary skipped path pays only the instruction fetch cost;
       leaking a deferred load debit here changes the loop iteration count. */
    "arm_conditional_load_skipped_cycle_loop", 26u,
    0xffffffffu, 0x0000001fu, 0u,
    {
      0xe3a02402u, /* mov r2, #0x02000000 */
      0x05920000u, /* loop: ldreq r0, [r2] (skipped, Z=0) */
      0xe2811001u, /* add r1, r1, #1 */
      0xeafffffcu  /* b loop */
    }, 4u, 0u
  },
  {
    /* SWP returns through the dispatcher after its store.  A surrounding
       conditional group must not prepay the following ADDEQ, which will be
       charged after dispatch resumes at pc+4. */
    "arm_conditional_swap_cycle_boundary", 24u,
    RV32IM_FRONTEND_CONTROL_PC + 16u, 0x4000001fu, 0u,
    {
      0xe3a02402u, /* mov r2,#0x02000000 */
      0xe3a01055u, /* mov r1,#0x55 */
      0x01020091u, /* swpeq r0,r1,[r2] */
      0x02888001u, /* addeq r8,r8,#1: executes after SWP dispatch */
      0xeafffffeu  /* b . */
    }, 5u, 0u
  },
  {
    /* Two adjacent BEQs carry the same scan exit marker.  The first taken
       branch must not prepay the second branch's fetch before chaining. */
    "arm_conditional_exit_group_cycle_boundary", 9u,
    RV32IM_FRONTEND_CONTROL_PC + 36u, 0x4000001fu, 0u,
    {
      0x0a000006u, /* beq target */
      0x0a000005u, /* adjacent BEQ: must not be prepaid by the first */
      0xeafffffeu, /* source fallthrough idle */
      0u, 0u, 0u, 0u, 0u,
      0xe2888001u, /* target: add r8,r8,#1 */
      0xeafffffeu  /* target idle */
    }, 10u, 0u
  },
  {
    /* The false-path complement is asymmetric: both BEQs are fetched even
       though neither body runs.  Sharing one predicate gate would skip the
       second branch's base cycle and let this loop run too many times. */
    "arm_conditional_exit_group_false_cycles", 30u,
    0xffffffffu, 0x0000001fu, 0u,
    {
      0x0a000003u, /* loop: beq idle (false, Z=0) */
      0x0a000002u, /* beq idle (false, Z=0) */
      0xe2888001u, /* add r8,r8,#1 */
      0xeafffffbu, /* b loop */
      0xe1a00000u,
      0xeafffffeu  /* idle */
    }, 6u, 0u
  },
  {
    /* Stores can return early on an alert, so they cannot prepay a following
       store.  Conversely, when both predicates fail, each STR still incurs
       its own fetch cycle and therefore needs an independent condition gate. */
    "arm_conditional_store_group_false_cycles", 30u,
    0xffffffffu, 0x0000001fu, 0u,
    {
      0xe3a02402u, /* mov r2,#0x02000000 */
      0x05820000u, /* loop: streq r0,[r2] (false, Z=0) */
      0x05821004u, /* streq r1,[r2,#4] (false, Z=0) */
      0xe2888001u, /* add r8,r8,#1 */
      0xeafffffbu  /* b loop */
    }, 5u, 0u
  },
  {
    /* Extra-transfer loads (LDRH/LDRSB/LDRSH) carry the same missing load
       latency risk through a separate frontend macro. */
    "arm_nonterminal_extra_load_cycle_loop", 26u,
    0xffffffffu, 0x0000001fu, 0u,
    {
      0xe3a02402u, /* mov r2, #0x02000000 */
      0xe1d200b0u, /* loop: ldrh r0, [r2] */
      0xe2811001u, /* add r1, r1, #1 */
      0xeafffffcu  /* b loop */
    }, 4u, 0u
  },
  {
    /* Constant-folding a ROM literal removes host memory work, not the guest
       load latency. */
    "arm_pc_pool_load_cycle_loop", 20u,
    0xffffffffu, 0x0000001fu, 0u,
    {
      0xe59f0008u, /* loop: ldr r0, [pc, #8] */
      0xe2811001u, /* add r1, r1, #1 */
      0xeafffffcu, /* b loop */
      0xe1a00000u,
      0x02000000u /* literal */
    }, 5u, 0u
  },
  {
    /* Consecutive instructions sharing a false ARM condition each still pay
       their base fetch cycle.  A store emitter must not erase the second
       debit merely because both bodies share one native condition gate. */
    "arm_false_condition_group_cycle_loop", 30u,
    0xffffffffu, 0x0000001fu, 0u,
    {
      0xe3a02402u, /* mov r2, #0x02000000 */
      0x01a00000u, /* loop: moveq r0, r0 (false, Z=0) */
      0x05821000u, /* streq r1, [r2] (false, Z=0) */
      0xe2833001u, /* add r3, r3, #1 */
      0xeafffffbu  /* b loop */
    }, 5u, 0u
  },
  {
    /* The linear predecessor proves Z=1, but the backward edge reaches the
       same target with Z=0.  Compile-time flag knowledge must be discarded at
       the internal join instead of folding ADDEQ into an unconditional ADD. */
    "arm_internal_join_known_flags", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 24u, 0x0000001fu, 0u,
    {
      0xe3a00000u, /* mov r0, #0 */
      0xe3500000u, /* cmp r0, #0: linear path has Z=1 */
      0x02811001u, /* target: addeq r1, r1, #1 */
      0xe2822001u, /* add r2, r2, #1 */
      0xe3520003u, /* cmp r2, #3: first two back edges have Z=0 */
      0x1afffffbu, /* bne target */
      0xeafffffeu, /* b . */
      0u, 0u
    }, 7u, 0u
  },
  {
    /* An internal edge enters the second instruction of what is a contiguous
       EQ run in linear layout.  The target must get its own predicate gate;
       otherwise the edge lands inside the first gate and executes ADDEQ with
       Z clear. */
    "arm_internal_join_splits_condition_group", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 20u, 0x0000001fu, 0u,
    {
      0xe3a00001u, /* mov r0, #1 (Z remains clear) */
      0xea000000u, /* b target */
      0x02811011u, /* linear-only: addeq r1, r1, #0x11 */
      0x02822022u, /* target: addeq r2, r2, #0x22 (must be skipped) */
      0xe1a08002u, /* mov r8, r2 */
      0xeafffffeu  /* b . */
    }, 6u, 0u
  },
  {
    /* OpenBIOS CpuSet 16-bit fill loop: post-index STRH must preserve the
       original address for the store and publish rn+2 before the back edge. */
    "arm_cpuset_post_index_strh_loop", 1000u,
    RV32IM_FRONTEND_CONTROL_PC + 40u, 0x0000001fu, 0u,
    {
      0xe3a01402u, /* mov r1, #0x02000000 */
      0xe5910000u, /* ldr r0, [r1]: zero, but no longer compile-time known */
      0xe0811000u, /* add r1, r1, r0: retain address, drop const proof */
      0xe3a02034u, /* mov r2, #0x34 */
      0xe2813008u, /* add r3, r1, #8 */
      0xe0c120b2u, /* loop: strh r2, [r1], #2 */
      0xe1510003u, /* cmp r1, r3 */
      0x1afffffcu, /* bne loop */
      0xe2411002u, /* sub r1, r1, #2 */
      0xe1d180b0u, /* ldrh r8, [r1] */
      0xeafffffeu, /* b . */
      0u
    }, 11u, 0u
  },
  {
    "arm_swi_cycle", 1u, 0xffffffffu, 0x0000001fu, 0u,
    { 0xef000000u, 0u, 0u, 0u }, 1u, 0u
  },
  {
    /* Enter the real OpenBIOS SWI dispatcher and CpuSet 16-bit fill path
       across scheduler boundaries.  The harness maps the dispatcher/table
       and CpuSet body into the BIOS region for this resume-enabled case. */
    "arm_openbios_cpuset_fill_resume", 80u,
    RV32IM_FRONTEND_CONTROL_PC + 0x1cu, 0x0000001fu, 0u,
    {
      0xe3a00402u, /* mov r0, #0x02000000 */
      0xe3a0405au, /* mov r4, #0x5a */
      0xe1844404u, /* orr r4, r4, r4, lsl #8 */
      0xe1c040b0u, /* strh r4, [r0] */
      0xe2801a01u, /* add r1, r0, #0x1000 */
      0xe3a02401u, /* mov r2, #0x01000000 */
      0xe3822b02u, /* orr r2, r2, #0x800 */
      0xef0b0000u, /* swi 0x0b0000: CpuSet */
      0xeafffffeu  /* exit: b exit */
    }, 9u, 0u, 2u, 0x7fffu
  },
  {
    /* CpuSet copy mode takes a different OpenBIOS loop from fill mode. */
    "arm_openbios_cpuset_half_copy_resume", 80u,
    RV32IM_FRONTEND_CONTROL_PC + 0x30u, 0x0000001fu, 0u,
    {
      0xe3a00402u, /* mov r0, #0x02000000 */
      0xe3a04011u, /* mov r4, #0x11 */
      0xe1844404u, /* orr r4, r4, r4, lsl #8 */
      0xe1c040b0u, /* strh r4, [r0] */
      0xe2844011u, /* add r4, r4, #0x11 */
      0xe1c040b2u, /* strh r4, [r0, #2] */
      0xe2844011u, /* add r4, r4, #0x11 */
      0xe1c040b4u, /* strh r4, [r0, #4] */
      0xe2844011u, /* add r4, r4, #0x11 */
      0xe1c040b6u, /* strh r4, [r0, #6] */
      0xe2801a01u, /* add r1, r0, #0x1000 */
      0xe3a02004u, /* mov r2, #4 */
      0xef0b0000u, /* swi 0x0b0000: CpuSet */
      0xeafffffeu  /* exit: b exit */
    }, 14u, 0u, 2u, 0x7fffu
  },
  {
    "arm_openbios_cpuset_word_copy_resume", 80u,
    RV32IM_FRONTEND_CONTROL_PC + 0x2cu, 0x0000001fu, 0u,
    {
      0xe3a00402u, /* mov r0, #0x02000000 */
      0xe3a04011u, /* mov r4, #0x11 */
      0xe5804000u, /* str r4, [r0] */
      0xe2844011u, /* add r4, r4, #0x11 */
      0xe5804004u, /* str r4, [r0, #4] */
      0xe2844011u, /* add r4, r4, #0x11 */
      0xe5804008u, /* str r4, [r0, #8] */
      0xe2844011u, /* add r4, r4, #0x11 */
      0xe580400cu, /* str r4, [r0, #12] */
      0xe2801a01u, /* add r1, r0, #0x1000 */
      0xe3a02004u, /* mov r2, #4 */
      0xe3822404u, /* orr r2, r2, #0x04000000 */
      0xef0b0000u, /* swi 0x0b0000: CpuSet */
      0xeafffffeu  /* exit: b exit */
    }, 14u, 0u, 2u, 0x7fffu
  },
  {
    /* Exercise CpuFastSet's eight-word fill batch through the real SWI
       dispatcher and across the same scheduler resume boundary. */
    "arm_openbios_cpufastset_fill_resume", 80u,
    RV32IM_FRONTEND_CONTROL_PC + 0x24u, 0x0000001fu, 0u,
    {
      0xe3a00402u, /* mov r0, #0x02000000 */
      0xe3a040a5u, /* mov r4, #0xa5 */
      0xe1844404u, /* orr r4, r4, r4, lsl #8 */
      0xe1844804u, /* orr r4, r4, r4, lsl #16 */
      0xe5804000u, /* str r4, [r0] */
      0xe2801a01u, /* add r1, r0, #0x1000 */
      0xe3a02401u, /* mov r2, #0x01000000 */
      0xe3822008u, /* orr r2, r2, #8 */
      0xef0c0000u, /* swi 0x0c0000: CpuFastSet */
      0xeafffffeu  /* exit: b exit */
    }, 10u, 0u, 2u, 0x7fffu
  },
  {
    /* Match p.gba's map-transition fill: one word in IWRAM expanded into
       256 words in EWRAM, with the BIOS loop crossing scheduler updates. */
    "arm_openbios_cpufastset_large_iwram_fill_resume", 80u,
    RV32IM_FRONTEND_CONTROL_PC + 0x10u, 0x0000001fu, 0u,
    {
      0xe59f000cu, /* ldr r0, source */
      0xe59f100cu, /* ldr r1, destination */
      0xe59f200cu, /* ldr r2, control */
      0xef0c0000u, /* swi 0x0c0000: CpuFastSet */
      0xeafffffeu, /* exit: b exit */
      0x03007df4u,
      0x02001000u,
      0x01000100u
    }, 8u, 0u, 2u, 0x7fffu
  },
  {
    "arm_openbios_cpufastset_copy_resume", 80u,
    RV32IM_FRONTEND_CONTROL_PC + 0x10u, 0x0000001fu, 0u,
    {
      0xe3a00402u, /* mov r0, #0x02000000 */
      0xe2801a01u, /* add r1, r0, #0x1000 */
      0xe3a02008u, /* mov r2, #8 */
      0xef0c0000u, /* swi 0x0c0000: CpuFastSet */
      0xeafffffeu  /* exit: b exit */
    }, 5u, 0u, 2u, 0x7fffu
  },
  {
    "arm_openbios_bgaffineset_resume", 80u,
    RV32IM_FRONTEND_CONTROL_PC + 0x10u, 0x0000001fu, 0u,
    {
      0xe3a00402u, /* mov r0, #0x02000000 */
      0xe2801a01u, /* add r1, r0, #0x1000 */
      0xe3a02001u, /* mov r2, #1 */
      0xef0d0000u, /* swi 0x0d0000: BgAffineSet */
      0xeafffffeu  /* exit: b exit */
    }, 5u, 0u, 2u, 0x7fffu
  },
  {
    "arm_openbios_objaffineset_resume", 80u,
    RV32IM_FRONTEND_CONTROL_PC + 0x14u, 0x0000001fu, 0u,
    {
      0xe3a00402u, /* mov r0, #0x02000000 */
      0xe2801a01u, /* add r1, r0, #0x1000 */
      0xe3a02001u, /* mov r2, #1 */
      0xe3a03002u, /* mov r3, #2 */
      0xef0e0000u, /* swi 0x0e0000: ObjAffineSet */
      0xeafffffeu  /* exit: b exit */
    }, 6u, 0u, 2u, 0x7fffu
  },
  {
    /* Thumb ALU fast paths also leave their base cycles accumulated.  A
       maximum-sized fallthrough block must flush them even though it has no
       branch/memory emitter at the tail. */
    "thumb_block_tail_cycles", 100u,
    RV32IM_FRONTEND_CONTROL_PC + 0x800u,
    0x0000003fu, 512u,
    { 0u }, 0u, 0u
  },
  {
    /* Constant folding can remove every conditional body at a fixed-size
       block tail.  The accumulated fetch debit still has to reach the
       scheduler before finalization falls through. */
    "arm_known_false_block_tail_cycles", 100u,
    RV32IM_FRONTEND_CONTROL_PC +
      RV32IM_FRONTEND_CONTROL_LONG_WORDS * 4u,
    0x0000001fu, RV32IM_FRONTEND_CONTROL_LONG_WORDS,
    {
      0xe3a00000u, /* mov r0,#0 */
      0xe3500000u  /* cmp r0,#0: every generated ADDNE is known false */
    }, 2u, 0u
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
