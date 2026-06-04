#include "riscv_runtime_test_shim.h"
#include "riscv_emit.h"

typedef unsigned int usize;

#define SYS_OPENAT 56
#define SYS_CLOSE 57
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_EXIT 93
#define SYS_MMAP 222
#define SYS_RISCV_FLUSH_ICACHE 259

#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32

#define STDIN_FD 0
#define STDOUT_FD 1

#define FRAME_W 240
#define FRAME_H 160
#define FRAME_BYTES (FRAME_W * FRAME_H * 2)
#define HARNESS_MODE "synthetic"
#define RUNTIME_FIXTURE_MODE "runtime_fixture"
#define PNG_RAW_STRIDE (FRAME_W * 3 + 1)
#define PNG_RAW_SIZE (PNG_RAW_STRIDE * FRAME_H)
#define ZLIB_BLOCK_MAX 65535u
#define ZLIB_BLOCKS ((PNG_RAW_SIZE + ZLIB_BLOCK_MAX - 1) / ZLIB_BLOCK_MAX)
#define ZLIB_SIZE (2 + PNG_RAW_SIZE + (ZLIB_BLOCKS * 5) + 4)
#define RUNTIME_EXEC_MAP_BYTES 24064u
#define RUNTIME_LOAD_BLOCK_OFFSET 512u
#define RUNTIME_STORE_BLOCK_OFFSET 1024u
#define RUNTIME_BRANCH_BLOCK_OFFSET 1536u
#define RUNTIME_BRANCH_TARGET_BLOCK_OFFSET 2048u
#define RUNTIME_UNSUPPORTED_BLOCK_OFFSET 2560u
#define RUNTIME_BX_BLOCK_OFFSET 3072u
#define RUNTIME_BX_TARGET_BLOCK_OFFSET 3584u
#define RUNTIME_PATCH_BRANCH_BLOCK_OFFSET 4096u
#define RUNTIME_PATCH_BRANCH_TARGET_BLOCK_OFFSET 4608u
#define RUNTIME_BL_BLOCK_OFFSET 5120u
#define RUNTIME_BL_TARGET_BLOCK_OFFSET 5632u
#define RUNTIME_SWI_BLOCK_OFFSET 6144u
#define RUNTIME_SWI_TARGET_BLOCK_OFFSET 6656u
#define RUNTIME_COND_BLOCK_OFFSET 7168u
#define RUNTIME_PC_WRITE_MOVS_BLOCK_OFFSET 7680u
#define RUNTIME_SWP_BLOCK_OFFSET 8192u
#define RUNTIME_MULTIPLY_BLOCK_OFFSET 8704u
#define RUNTIME_FLAG_ADDS_BLOCK_OFFSET 9216u
#define RUNTIME_FLAG_CMP_BLOCK_OFFSET 9728u
#define RUNTIME_PSR_BLOCK_OFFSET 10240u
#define RUNTIME_MSR_CPSR_FLAGS_BLOCK_OFFSET 10752u
#define RUNTIME_MSR_SPSR_BLOCK_OFFSET 11264u
#define RUNTIME_HALF_LOAD_BLOCK_OFFSET 11776u
#define RUNTIME_HALF_STORE_BLOCK_OFFSET 12288u
#define RUNTIME_BLOCK_MEM_STM_BLOCK_OFFSET 12800u
#define RUNTIME_BLOCK_MEM_LDM_BLOCK_OFFSET 13312u
#define RUNTIME_HLE_DIV_BLOCK_OFFSET 13824u
#define RUNTIME_HLE_DIVARM_BLOCK_OFFSET 14336u
#define RUNTIME_PC_SOURCE_BLOCK_OFFSET 14848u
#define RUNTIME_WRITEBACK_STORE_BLOCK_OFFSET 15872u
#define RUNTIME_WRITEBACK_LOAD_BLOCK_OFFSET 16384u
#define RUNTIME_REG_OFFSET_LOAD_BLOCK_OFFSET 16896u
#define RUNTIME_SHIFTED_REG_OFFSET_BLOCK_OFFSET 17408u
#define RUNTIME_REG_OFFSET_RRX_LOAD_BLOCK_OFFSET 17920u
#define RUNTIME_HALF_REG_LOAD_BLOCK_OFFSET 18432u
#define RUNTIME_HALF_REG_STORE_BLOCK_OFFSET 18944u
#define RUNTIME_BLOCK_MEM_PUSH_BLOCK_OFFSET 19456u
#define RUNTIME_BLOCK_MEM_LDM_PC_BLOCK_OFFSET 19968u
#define RUNTIME_BLOCK_MEM_LDM_PC_S_BLOCK_OFFSET 20480u
#define RUNTIME_MULTIPLY_LONG_BLOCK_OFFSET 20992u
#define RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_BLOCK_OFFSET 21504u
#define RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_BLOCK_OFFSET 22016u
#define RUNTIME_MULTIPLY_LONG_ACC_BLOCK_OFFSET 22528u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_BLOCK_OFFSET 23040u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_BLOCK_OFFSET 23552u
#define RUNTIME_START_PC 0x08000000u
#define RUNTIME_END_PC (RUNTIME_START_PC + 4u)
#define RUNTIME_CYCLES 7u
#define RUNTIME_ADD_R2_R0_R1 0xe0802001u
#define RUNTIME_MULTIPLY_START_PC 0x08000b00u
#define RUNTIME_MULTIPLY_END_PC (RUNTIME_MULTIPLY_START_PC + 8u)
#define RUNTIME_MULTIPLY_MUL_CYCLES 5u
#define RUNTIME_MULTIPLY_MLA_CYCLES 6u
#define RUNTIME_MULTIPLY_TOTAL_CYCLES \
  (RUNTIME_MULTIPLY_MUL_CYCLES + RUNTIME_MULTIPLY_MLA_CYCLES)
#define RUNTIME_MULTIPLY_MUL_R4_R1_R2 0xe0040291u
#define RUNTIME_MULTIPLY_MLA_R5_R1_R2_R3 0xe0253291u
#define RUNTIME_CPSR_LOW_VALUE 0x9fu
#define RUNTIME_CPSR_CV_LOW_VALUE \
  (0x30000000u | RUNTIME_CPSR_LOW_VALUE)
#define RUNTIME_MULTIPLY_LONG_START_PC 0x08001160u
#define RUNTIME_MULTIPLY_LONG_END_PC \
  (RUNTIME_MULTIPLY_LONG_START_PC + 8u)
#define RUNTIME_MULTIPLY_LONG_UMULL_CYCLES 7u
#define RUNTIME_MULTIPLY_LONG_SMULL_CYCLES 6u
#define RUNTIME_MULTIPLY_LONG_TOTAL_CYCLES \
  (RUNTIME_MULTIPLY_LONG_UMULL_CYCLES + \
   RUNTIME_MULTIPLY_LONG_SMULL_CYCLES)
#define RUNTIME_MULTIPLY_LONG_UMULL_R8_R9_R1_R2 0xe0898291u
#define RUNTIME_MULTIPLY_LONG_SMULL_R10_R11_R3_R4 0xe0cba493u
#define RUNTIME_MULTIPLY_LONG_UMULL_R1_VALUE 0xffffffffu
#define RUNTIME_MULTIPLY_LONG_UMULL_R2_VALUE 0x00000002u
#define RUNTIME_MULTIPLY_LONG_UMULL_LO_VALUE 0xfffffffeu
#define RUNTIME_MULTIPLY_LONG_UMULL_HI_VALUE 0x00000001u
#define RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE 0xfffffffeu
#define RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE 0x00000003u
#define RUNTIME_MULTIPLY_LONG_SMULL_LO_VALUE 0xfffffffau
#define RUNTIME_MULTIPLY_LONG_SMULL_HI_VALUE 0xffffffffu
#define RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_START_PC 0x08001180u
#define RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_END_PC \
  (RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_START_PC + 4u)
#define RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_START_PC 0x080011a0u
#define RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_END_PC \
  (RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_START_PC + 4u)
#define RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_CYCLES 7u
#define RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_CYCLES 6u
#define RUNTIME_MULTIPLY_LONG_UMULLS_R8_R9_R1_R2 0xe0998291u
#define RUNTIME_MULTIPLY_LONG_SMULLS_R10_R11_R3_R4 0xe0dba493u
#define RUNTIME_MULTIPLY_LONG_UMLAL_R8_R9_R1_R2 0xe0a98291u
#define RUNTIME_MULTIPLY_LONG_SMLAL_R10_R11_R3_R4 0xe0eba493u
#define RUNTIME_MULTIPLY_LONG_UMLALS_R8_R9_R1_R2 0xe0b98291u
#define RUNTIME_MULTIPLY_LONG_SMLALS_R10_R11_R3_R4 0xe0fba493u
#define RUNTIME_MULTIPLY_LONG_FLAG_ZERO_R1_VALUE 0x00000000u
#define RUNTIME_MULTIPLY_LONG_FLAG_R2_VALUE 0x12345678u
#define RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_LO_VALUE 0x00000000u
#define RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_HI_VALUE 0x00000000u
#define RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_CPSR_VALUE \
  (0x40000000u | RUNTIME_CPSR_CV_LOW_VALUE)
#define RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_CPSR_VALUE \
  (0x80000000u | RUNTIME_CPSR_CV_LOW_VALUE)
#define RUNTIME_MULTIPLY_LONG_ACC_START_PC 0x080011c0u
#define RUNTIME_MULTIPLY_LONG_ACC_END_PC \
  (RUNTIME_MULTIPLY_LONG_ACC_START_PC + 8u)
#define RUNTIME_MULTIPLY_LONG_UMLAL_CYCLES 7u
#define RUNTIME_MULTIPLY_LONG_SMLAL_CYCLES 8u
#define RUNTIME_MULTIPLY_LONG_ACC_TOTAL_CYCLES \
  (RUNTIME_MULTIPLY_LONG_UMLAL_CYCLES + \
   RUNTIME_MULTIPLY_LONG_SMLAL_CYCLES)
#define RUNTIME_MULTIPLY_LONG_UMLAL_OLD_LO_VALUE 0x00000002u
#define RUNTIME_MULTIPLY_LONG_UMLAL_OLD_HI_VALUE 0x00000003u
#define RUNTIME_MULTIPLY_LONG_UMLAL_LO_VALUE 0x00000000u
#define RUNTIME_MULTIPLY_LONG_UMLAL_HI_VALUE 0x00000005u
#define RUNTIME_MULTIPLY_LONG_SMLAL_OLD_LO_VALUE 0x00000008u
#define RUNTIME_MULTIPLY_LONG_SMLAL_OLD_HI_VALUE 0x00000001u
#define RUNTIME_MULTIPLY_LONG_SMLAL_LO_VALUE 0x00000002u
#define RUNTIME_MULTIPLY_LONG_SMLAL_HI_VALUE 0x00000001u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_START_PC 0x080011e0u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_END_PC \
  (RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_START_PC + 4u)
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_START_PC 0x08001200u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_END_PC \
  (RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_START_PC + 4u)
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_CYCLES 7u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_CYCLES 8u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_OLD_LO_VALUE 0x00000002u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_OLD_HI_VALUE 0xfffffffeu
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_LO_VALUE 0x00000000u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_HI_VALUE 0x00000000u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_CPSR_VALUE \
  (0x40000000u | RUNTIME_CPSR_CV_LOW_VALUE)
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_OLD_LO_VALUE 0x00000000u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_OLD_HI_VALUE 0x00000000u
#define RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_CPSR_VALUE \
  (0x80000000u | RUNTIME_CPSR_CV_LOW_VALUE)
#define RUNTIME_FLAG_ADDS_START_PC 0x08000c00u
#define RUNTIME_FLAG_ADDS_END_PC (RUNTIME_FLAG_ADDS_START_PC + 4u)
#define RUNTIME_FLAG_ADDS_CYCLES 5u
#define RUNTIME_ADDS_R7_R1_0X1 0xe2917001u
#define RUNTIME_FLAG_ADDS_R1_VALUE 0x7fffffffu
#define RUNTIME_FLAG_ADDS_R7_VALUE 0x80000000u
#define RUNTIME_FLAG_ADDS_CPSR_VALUE \
  (0x90000000u | RUNTIME_CPSR_LOW_VALUE)
#define RUNTIME_FLAG_CMP_START_PC 0x08000c20u
#define RUNTIME_FLAG_CMP_END_PC (RUNTIME_FLAG_CMP_START_PC + 4u)
#define RUNTIME_FLAG_CMP_CYCLES 5u
#define RUNTIME_CMP_R0_0X20 0xe3500020u
#define RUNTIME_FLAG_CMP_R0_VALUE 0x00000020u
#define RUNTIME_FLAG_CMP_CPSR_VALUE \
  (0x60000000u | RUNTIME_CPSR_LOW_VALUE)
#define RUNTIME_PSR_START_PC 0x08000d00u
#define RUNTIME_PSR_END_PC (RUNTIME_PSR_START_PC + 8u)
#define RUNTIME_PSR_MRS_CPSR_CYCLES 5u
#define RUNTIME_PSR_MRS_SPSR_CYCLES 6u
#define RUNTIME_PSR_TOTAL_CYCLES \
  (RUNTIME_PSR_MRS_CPSR_CYCLES + RUNTIME_PSR_MRS_SPSR_CYCLES)
#define RUNTIME_MRS_R6_CPSR 0xe10f6000u
#define RUNTIME_MRS_R7_SPSR 0xe14f7000u
#define RUNTIME_PSR_CPSR_VALUE 0xa000009fu
#define RUNTIME_PSR_CPU_MODE_VALUE MODE_SUPERVISOR
#define RUNTIME_PSR_SPSR_VALUE 0x12345678u
#define RUNTIME_MSR_CPSR_FLAGS_START_PC 0x08000d20u
#define RUNTIME_MSR_CPSR_FLAGS_END_PC \
  (RUNTIME_MSR_CPSR_FLAGS_START_PC + 4u)
#define RUNTIME_MSR_SPSR_START_PC 0x08000d40u
#define RUNTIME_MSR_SPSR_END_PC (RUNTIME_MSR_SPSR_START_PC + 4u)
#define RUNTIME_MSR_CPSR_FLAGS_CYCLES 7u
#define RUNTIME_MSR_SPSR_CYCLES 9u
#define RUNTIME_MSR_CPSR_FLAGS_IMM 0xe328f20au
#define RUNTIME_MSR_SPSR_R1 0xe169f001u
#define RUNTIME_MSR_CPSR_FLAGS_INITIAL 0x30000093u
#define RUNTIME_MSR_CPSR_FLAGS_EXPECTED 0xa0000093u
#define RUNTIME_MSR_SPSR_OLD_VALUE 0x12345678u
#define RUNTIME_MSR_SPSR_SOURCE 0xa00000d3u
#define RUNTIME_MSR_SPSR_EXPECTED 0xa23456d3u
#define RUNTIME_UNSUPPORTED_START_PC 0x08000020u
#define RUNTIME_UNSUPPORTED_END_PC (RUNTIME_UNSUPPORTED_START_PC + 4u)
#define RUNTIME_UNSUPPORTED_CYCLES 11u
#define RUNTIME_THUMB_FALLBACK_START_PC 0x02001020u
#define RUNTIME_THUMB_FALLBACK_CYCLES 13u
#define RUNTIME_NO_UPDATE_CYCLES 0x7fffffffu
#define RUNTIME_CPSR_T_BIT 0x20u
#define RUNTIME_BRANCH_START_PC 0x08000100u
#define RUNTIME_BRANCH_TARGET_PC 0x0800010cu
#define RUNTIME_BRANCH_TARGET_END_PC (RUNTIME_BRANCH_TARGET_PC + 4u)
#define RUNTIME_BRANCH_CYCLES 9u
#define RUNTIME_BRANCH_TARGET_CYCLES 5u
#define RUNTIME_BRANCH_TOTAL_CYCLES \
  (RUNTIME_BRANCH_CYCLES + RUNTIME_BRANCH_TARGET_CYCLES)
#define RUNTIME_BRANCH_IDLE_EXTRA_CYCLES 5u
#define RUNTIME_BRANCH_IDLE_TOTAL_CYCLES \
  (RUNTIME_BRANCH_CYCLES + RUNTIME_BRANCH_IDLE_EXTRA_CYCLES)
#define RUNTIME_BRANCH_B_PLUS_12 0xea000001u
#define RUNTIME_BRANCH_TARGET_ADD_R3_R2_R1 0xe0823001u
#define RUNTIME_BX_START_PC 0x08000400u
#define RUNTIME_BX_TARGET_PC 0x02001000u
#define RUNTIME_BX_TARGET_END_PC (RUNTIME_BX_TARGET_PC + 4u)
#define RUNTIME_BX_CYCLES 8u
#define RUNTIME_BX_TARGET_CYCLES 5u
#define RUNTIME_BX_TOTAL_CYCLES \
  (RUNTIME_BX_CYCLES + RUNTIME_BX_TARGET_CYCLES)
#define RUNTIME_BX_R7 0xe12fff17u
#define RUNTIME_BX_TARGET_ADD_R4_R1_R2 0xe0814002u
#define RUNTIME_PATCH_BRANCH_START_PC 0x08000500u
#define RUNTIME_PATCH_BRANCH_TARGET_PC \
  (RUNTIME_PATCH_BRANCH_START_PC + 12u)
#define RUNTIME_PATCH_BRANCH_TARGET_END_PC \
  (RUNTIME_PATCH_BRANCH_TARGET_PC + 4u)
#define RUNTIME_PATCH_BRANCH_CYCLES 10u
#define RUNTIME_PATCH_BRANCH_TARGET_CYCLES 6u
#define RUNTIME_PATCH_BRANCH_TOTAL_CYCLES \
  (RUNTIME_PATCH_BRANCH_CYCLES + RUNTIME_PATCH_BRANCH_TARGET_CYCLES)
#define RUNTIME_PATCH_BRANCH_B_PLUS_12 0xea000001u
#define RUNTIME_PATCH_BRANCH_TARGET_ADD_R5_R1_R2 0xe0815002u
#define RUNTIME_BL_START_PC 0x08000600u
#define RUNTIME_BL_TARGET_PC (RUNTIME_BL_START_PC + 16u)
#define RUNTIME_BL_TARGET_END_PC (RUNTIME_BL_TARGET_PC + 4u)
#define RUNTIME_BL_LINK_PC (RUNTIME_BL_START_PC + 4u)
#define RUNTIME_BL_CYCLES 10u
#define RUNTIME_BL_TARGET_CYCLES 5u
#define RUNTIME_BL_TOTAL_CYCLES \
  (RUNTIME_BL_CYCLES + RUNTIME_BL_TARGET_CYCLES)
#define RUNTIME_BL_PLUS_16 0xeb000002u
#define RUNTIME_BL_TARGET_ADD_R8_R1_R2 0xe0818002u
#define RUNTIME_SWI_START_PC 0x08000700u
#define RUNTIME_SWI_END_PC (RUNTIME_SWI_START_PC + 4u)
#define RUNTIME_SWI_TARGET_PC 0x00000008u
#define RUNTIME_SWI_TARGET_END_PC (RUNTIME_SWI_TARGET_PC + 4u)
#define RUNTIME_SWI_LINK_PC (RUNTIME_SWI_START_PC + 4u)
#define RUNTIME_SWI_CYCLES 9u
#define RUNTIME_SWI_TARGET_CYCLES 5u
#define RUNTIME_SWI_TOTAL_CYCLES \
  (RUNTIME_SWI_CYCLES + RUNTIME_SWI_TARGET_CYCLES)
#define RUNTIME_SWI_OPCODE_5 0xef050000u
#define RUNTIME_SWI_INITIAL_CPSR 0xa000001fu
#define RUNTIME_SWI_CPSR_VALUE \
  ((RUNTIME_SWI_INITIAL_CPSR & ~0x3fu) | MODE_SUPERVISOR | 0x80u)
#define RUNTIME_SWI_BUS_VALUE 0xe3a02004u
#define RUNTIME_SWI_TARGET_ADD_R9_R1_R2 0xe0819002u
#define RUNTIME_COND_START_PC 0x08000800u
#define RUNTIME_COND_END_PC (RUNTIME_COND_START_PC + 4u)
#define RUNTIME_COND_CYCLES 4u
#define RUNTIME_COND_NE 0x1u
#define RUNTIME_COND_ADD_R10_R1_R2 0xe081a002u
#define RUNTIME_CPSR_Z_BIT 0x40000000u
#define RUNTIME_PC_WRITE_MOVS_START_PC 0x08000900u
#define RUNTIME_PC_WRITE_MOVS_END_PC (RUNTIME_PC_WRITE_MOVS_START_PC + 4u)
#define RUNTIME_PC_WRITE_MOVS_CYCLES 5u
#define RUNTIME_MOVS_PC_R14 0xe1b0f00eu
#define RUNTIME_PC_WRITE_MOVS_TARGET 0x02009000u
#define RUNTIME_PC_WRITE_MOVS_SPSR_VALUE \
  (RUNTIME_CPSR_Z_BIT | MODE_SUPERVISOR)
#define RUNTIME_LOAD_START_PC 0x08000200u
#define RUNTIME_LOAD_WORD_PC RUNTIME_LOAD_START_PC
#define RUNTIME_LOAD_BYTE_PC (RUNTIME_LOAD_START_PC + 4u)
#define RUNTIME_LOAD_END_PC (RUNTIME_LOAD_START_PC + 8u)
#define RUNTIME_LOAD_WORD_BASE_CYCLES 7u
#define RUNTIME_LOAD_BYTE_BASE_CYCLES 5u
#define RUNTIME_LOAD_TOTAL_CYCLES \
  ((RUNTIME_LOAD_WORD_BASE_CYCLES + 2u) + \
   (RUNTIME_LOAD_BYTE_BASE_CYCLES + 2u))
#define RUNTIME_LOAD_LDR_R4_R3_0X24 0xe5934024u
#define RUNTIME_LOAD_LDRB_R5_R3_0X25 0xe5d35025u
#define RUNTIME_LOAD_BASE_ADDR 0x02000040u
#define RUNTIME_LOAD_WORD_ADDR (RUNTIME_LOAD_BASE_ADDR + 0x24u)
#define RUNTIME_LOAD_BYTE_ADDR (RUNTIME_LOAD_BASE_ADDR + 0x25u)
#define RUNTIME_STORE_START_PC 0x08000300u
#define RUNTIME_STORE_END_PC (RUNTIME_STORE_START_PC + 4u)
#define RUNTIME_STORE_BASE_CYCLES 6u
#define RUNTIME_STORE_TOTAL_CYCLES (RUNTIME_STORE_BASE_CYCLES + 1u)
#define RUNTIME_STORE_STR_R6_R3_0X28 0xe5836028u
#define RUNTIME_STORE_BASE_ADDR 0x02000100u
#define RUNTIME_STORE_WORD_ADDR (RUNTIME_STORE_BASE_ADDR + 0x28u)
#define RUNTIME_HALF_LOAD_START_PC 0x08000e00u
#define RUNTIME_HALF_LDRH_PC RUNTIME_HALF_LOAD_START_PC
#define RUNTIME_HALF_LDRSB_PC (RUNTIME_HALF_LOAD_START_PC + 4u)
#define RUNTIME_HALF_LDRSH_PC (RUNTIME_HALF_LOAD_START_PC + 8u)
#define RUNTIME_HALF_LOAD_END_PC (RUNTIME_HALF_LOAD_START_PC + 12u)
#define RUNTIME_HALF_LDRH_BASE_CYCLES 4u
#define RUNTIME_HALF_LDRSB_BASE_CYCLES 5u
#define RUNTIME_HALF_LDRSH_BASE_CYCLES 6u
#define RUNTIME_HALF_LOAD_TOTAL_CYCLES \
  ((RUNTIME_HALF_LDRH_BASE_CYCLES + 2u) + \
   (RUNTIME_HALF_LDRSB_BASE_CYCLES + 2u) + \
   (RUNTIME_HALF_LDRSH_BASE_CYCLES + 2u))
#define RUNTIME_HALF_STORE_START_PC 0x08000e40u
#define RUNTIME_HALF_STORE_END_PC (RUNTIME_HALF_STORE_START_PC + 4u)
#define RUNTIME_HALF_STORE_BASE_CYCLES 6u
#define RUNTIME_HALF_STORE_TOTAL_CYCLES \
  (RUNTIME_HALF_STORE_BASE_CYCLES + 1u)
#define RUNTIME_HALF_LDRH_R4_R3_0X24 0xe1d342b4u
#define RUNTIME_HALF_LDRSB_R5_R3_0X25 0xe1d352d5u
#define RUNTIME_HALF_LDRSH_R6_R3_0X26 0xe1d362f6u
#define RUNTIME_HALF_STRH_R7_R3_0X28 0xe1c372b8u
#define RUNTIME_HALF_BASE_ADDR 0x02000240u
#define RUNTIME_HALF_U16_ADDR (RUNTIME_HALF_BASE_ADDR + 0x24u)
#define RUNTIME_HALF_S8_ADDR (RUNTIME_HALF_BASE_ADDR + 0x25u)
#define RUNTIME_HALF_S16_ADDR (RUNTIME_HALF_BASE_ADDR + 0x26u)
#define RUNTIME_HALF_STORE_ADDR (RUNTIME_HALF_BASE_ADDR + 0x28u)
#define RUNTIME_HALF_U16_VALUE 0x000089abu
#define RUNTIME_HALF_S8_VALUE 0xfffffff0u
#define RUNTIME_HALF_S16_VALUE 0xffff8123u
#define RUNTIME_HALF_STORE_VALUE 0x2468ace1u
#define RUNTIME_HALF_STORE_U16_VALUE \
  (RUNTIME_HALF_STORE_VALUE & 0xffffu)
#define RUNTIME_HALF_REG_LOAD_START_PC 0x08001080u
#define RUNTIME_HALF_REG_LDRH_PC RUNTIME_HALF_REG_LOAD_START_PC
#define RUNTIME_HALF_REG_LDRSB_PC \
  (RUNTIME_HALF_REG_LOAD_START_PC + 4u)
#define RUNTIME_HALF_REG_LDRSH_PC \
  (RUNTIME_HALF_REG_LOAD_START_PC + 8u)
#define RUNTIME_HALF_REG_LOAD_END_PC \
  (RUNTIME_HALF_REG_LOAD_START_PC + 12u)
#define RUNTIME_HALF_REG_STORE_START_PC 0x080010c0u
#define RUNTIME_HALF_REG_STORE_END_PC \
  (RUNTIME_HALF_REG_STORE_START_PC + 4u)
#define RUNTIME_HALF_REG_LDRH_R8_R3_R2 0xe19380b2u
#define RUNTIME_HALF_REG_LDRSB_R9_R3_NEG_R2 0xe11390d2u
#define RUNTIME_HALF_REG_LDRSH_R10_R3_R2 0xe193a0f2u
#define RUNTIME_HALF_REG_STRH_R9_R3_NEG_R2 0xe10390b2u
#define RUNTIME_HALF_REG_OFFSET_VALUE 0x24u
#define RUNTIME_HALF_REG_U16_ADDR \
  (RUNTIME_HALF_BASE_ADDR + RUNTIME_HALF_REG_OFFSET_VALUE)
#define RUNTIME_HALF_REG_S8_ADDR \
  (RUNTIME_HALF_BASE_ADDR - RUNTIME_HALF_REG_OFFSET_VALUE)
#define RUNTIME_HALF_REG_S16_ADDR RUNTIME_HALF_REG_U16_ADDR
#define RUNTIME_HALF_REG_STORE_ADDR RUNTIME_HALF_REG_S8_ADDR
#define RUNTIME_BLOCK_MEM_STM_START_PC 0x08000e80u
#define RUNTIME_BLOCK_MEM_STM_END_PC \
  (RUNTIME_BLOCK_MEM_STM_START_PC + 4u)
#define RUNTIME_BLOCK_MEM_LDM_START_PC 0x08000ea0u
#define RUNTIME_BLOCK_MEM_LDM_END_PC \
  (RUNTIME_BLOCK_MEM_LDM_START_PC + 4u)
#define RUNTIME_BLOCK_MEM_PUSH_START_PC 0x08001100u
#define RUNTIME_BLOCK_MEM_PUSH_END_PC \
  (RUNTIME_BLOCK_MEM_PUSH_START_PC + 4u)
#define RUNTIME_BLOCK_MEM_LDM_PC_START_PC 0x08001120u
#define RUNTIME_BLOCK_MEM_LDM_PC_END_PC \
  (RUNTIME_BLOCK_MEM_LDM_PC_START_PC + 4u)
#define RUNTIME_BLOCK_MEM_LDM_PC_S_START_PC 0x08001140u
#define RUNTIME_BLOCK_MEM_LDM_PC_S_END_PC \
  (RUNTIME_BLOCK_MEM_LDM_PC_S_START_PC + 4u)
#define RUNTIME_BLOCK_MEM_STM_CYCLES 6u
#define RUNTIME_BLOCK_MEM_LDM_CYCLES 7u
#define RUNTIME_BLOCK_MEM_PUSH_CYCLES 8u
#define RUNTIME_BLOCK_MEM_LDM_PC_CYCLES 7u
#define RUNTIME_BLOCK_MEM_XFER_COUNT 3u
#define RUNTIME_BLOCK_MEM_STM_TOTAL_CYCLES \
  (RUNTIME_BLOCK_MEM_STM_CYCLES + RUNTIME_BLOCK_MEM_XFER_COUNT)
#define RUNTIME_BLOCK_MEM_LDM_TOTAL_CYCLES \
  (RUNTIME_BLOCK_MEM_LDM_CYCLES + RUNTIME_BLOCK_MEM_XFER_COUNT)
#define RUNTIME_BLOCK_MEM_PUSH_TOTAL_CYCLES \
  (RUNTIME_BLOCK_MEM_PUSH_CYCLES + RUNTIME_BLOCK_MEM_XFER_COUNT)
#define RUNTIME_BLOCK_MEM_LDM_PC_TOTAL_CYCLES \
  (RUNTIME_BLOCK_MEM_LDM_PC_CYCLES + RUNTIME_BLOCK_MEM_XFER_COUNT)
#define RUNTIME_BLOCK_MEM_LDM_PC_CHAIN_TOTAL_CYCLES \
  (RUNTIME_BLOCK_MEM_LDM_PC_TOTAL_CYCLES + RUNTIME_BRANCH_TARGET_CYCLES)
#define RUNTIME_STMIA_R3_WB_R0_R2_R5 0xe8a30025u
#define RUNTIME_LDMIA_R4_WB_R1_R6_R7 0xe8b400c2u
#define RUNTIME_STMDB_R13_WB_R0_R1_R14 0xe92d4003u
#define RUNTIME_LDMIA_R4_WB_R1_R6_PC 0xe8b48042u
#define RUNTIME_LDMIA_R4_WB_S_R1_R6_PC 0xe8f48042u
#define RUNTIME_BLOCK_MEM_BASE 0x02000700u
#define RUNTIME_BLOCK_MEM_PC_BASE 0x02000780u
#define RUNTIME_BLOCK_MEM_STM_R0_VALUE 0x11112222u
#define RUNTIME_BLOCK_MEM_STM_R2_VALUE 0x33334444u
#define RUNTIME_BLOCK_MEM_STM_R5_VALUE 0x55556666u
#define RUNTIME_BLOCK_MEM_LDM_R1_VALUE 0x77778888u
#define RUNTIME_BLOCK_MEM_LDM_R6_VALUE 0x9999aaaau
#define RUNTIME_BLOCK_MEM_LDM_R7_VALUE 0xbbbbccccu
#define RUNTIME_BLOCK_MEM_LDM_PC_R1_VALUE 0x13572468u
#define RUNTIME_BLOCK_MEM_LDM_PC_R6_VALUE 0x24681357u
#define RUNTIME_BLOCK_MEM_PUSH_SP_START (RUNTIME_BLOCK_MEM_BASE + 0x40u)
#define RUNTIME_BLOCK_MEM_PUSH_ADDR \
  (RUNTIME_BLOCK_MEM_PUSH_SP_START - 12u)
#define RUNTIME_BLOCK_MEM_PUSH_R0_VALUE 0x01020304u
#define RUNTIME_BLOCK_MEM_PUSH_R1_VALUE 0x11121314u
#define RUNTIME_BLOCK_MEM_PUSH_LR_VALUE 0x21222324u
#define RUNTIME_BLOCK_MEM_READ32_TAG 0x52333252u
#define RUNTIME_BLOCK_MEM_WRITE32_TAG 0x57333257u
#define RUNTIME_HLE_DIV_START_PC 0x08000ec0u
#define RUNTIME_HLE_DIV_END_PC (RUNTIME_HLE_DIV_START_PC + 4u)
#define RUNTIME_HLE_DIVARM_START_PC 0x08000ee0u
#define RUNTIME_HLE_DIVARM_END_PC (RUNTIME_HLE_DIVARM_START_PC + 4u)
#define RUNTIME_HLE_DIV_CYCLES 73u
#define RUNTIME_HLE_DIVARM_CYCLES 75u
#define RUNTIME_HLE_DIV_R0_VALUE 0xfffffb2eu
#define RUNTIME_HLE_DIV_R1_VALUE 10u
#define RUNTIME_HLE_DIV_QUOTIENT 0xffffff85u
#define RUNTIME_HLE_DIV_REMAINDER 0xfffffffcu
#define RUNTIME_HLE_DIV_ABS_QUOTIENT 123u
#define RUNTIME_HLE_DIVARM_R0_VALUE 0xfffffffdu
#define RUNTIME_HLE_DIVARM_R1_VALUE 10u
#define RUNTIME_HLE_DIVARM_QUOTIENT 0xfffffffdu
#define RUNTIME_HLE_DIVARM_REMAINDER 1u
#define RUNTIME_HLE_DIVARM_ABS_QUOTIENT 3u
#define RUNTIME_PC_SOURCE_START_PC 0x08000f00u
#define RUNTIME_PC_SOURCE_ADD_PC RUNTIME_PC_SOURCE_START_PC
#define RUNTIME_PC_SOURCE_MOV_PC (RUNTIME_PC_SOURCE_START_PC + 4u)
#define RUNTIME_PC_SOURCE_EOR_IMM_PC (RUNTIME_PC_SOURCE_START_PC + 8u)
#define RUNTIME_PC_SOURCE_EOR_REG_RM_PC (RUNTIME_PC_SOURCE_START_PC + 12u)
#define RUNTIME_PC_SOURCE_EOR_REG_RS_PC (RUNTIME_PC_SOURCE_START_PC + 16u)
#define RUNTIME_PC_SOURCE_TST_PC (RUNTIME_PC_SOURCE_START_PC + 20u)
#define RUNTIME_PC_SOURCE_TST_REG_PC (RUNTIME_PC_SOURCE_START_PC + 24u)
#define RUNTIME_PC_SOURCE_END_PC (RUNTIME_PC_SOURCE_START_PC + 28u)
#define RUNTIME_PC_SOURCE_ADD_CYCLES 4u
#define RUNTIME_PC_SOURCE_MOV_CYCLES 5u
#define RUNTIME_PC_SOURCE_EOR_IMM_CYCLES 6u
#define RUNTIME_PC_SOURCE_EOR_REG_RM_CYCLES 7u
#define RUNTIME_PC_SOURCE_EOR_REG_RS_CYCLES 8u
#define RUNTIME_PC_SOURCE_TST_CYCLES 5u
#define RUNTIME_PC_SOURCE_TST_REG_CYCLES 6u
#define RUNTIME_PC_SOURCE_TOTAL_CYCLES \
  (RUNTIME_PC_SOURCE_ADD_CYCLES + RUNTIME_PC_SOURCE_MOV_CYCLES + \
   RUNTIME_PC_SOURCE_EOR_IMM_CYCLES + \
   RUNTIME_PC_SOURCE_EOR_REG_RM_CYCLES + \
   RUNTIME_PC_SOURCE_EOR_REG_RS_CYCLES + RUNTIME_PC_SOURCE_TST_CYCLES + \
   RUNTIME_PC_SOURCE_TST_REG_CYCLES)
#define RUNTIME_PC_SOURCE_ADD_R6_PC_0X20 0xe28f6020u
#define RUNTIME_PC_SOURCE_MOV_R7_PC 0xe1a0700fu
#define RUNTIME_PC_SOURCE_EOR_R8_R0_PC_LSL2 0xe020810fu
#define RUNTIME_PC_SOURCE_EOR_R9_R0_PC_LSL_R2 0xe020921fu
#define RUNTIME_PC_SOURCE_EOR_R10_R0_R1_LSL_PC 0xe020af11u
#define RUNTIME_PC_SOURCE_TST_R0_PC 0xe110000fu
#define RUNTIME_PC_SOURCE_TST_R0_R1_LSL_PC 0xe1100f11u
#define RUNTIME_PC_SOURCE_R0_VALUE 0x01010101u
#define RUNTIME_PC_SOURCE_R1_VALUE 0x00000003u
#define RUNTIME_PC_SOURCE_R2_VALUE 4u
#define RUNTIME_PC_SOURCE_R6_VALUE \
  (RUNTIME_PC_SOURCE_ADD_PC + 8u + 0x20u)
#define RUNTIME_PC_SOURCE_R7_VALUE (RUNTIME_PC_SOURCE_MOV_PC + 8u)
#define RUNTIME_PC_SOURCE_R8_VALUE \
  (RUNTIME_PC_SOURCE_R0_VALUE ^ \
   ((RUNTIME_PC_SOURCE_EOR_IMM_PC + 8u) << 2))
#define RUNTIME_PC_SOURCE_R9_VALUE \
  (RUNTIME_PC_SOURCE_R0_VALUE ^ \
   ((RUNTIME_PC_SOURCE_EOR_REG_RM_PC + 12u) << \
    RUNTIME_PC_SOURCE_R2_VALUE))
#define RUNTIME_PC_SOURCE_R10_VALUE \
  (RUNTIME_PC_SOURCE_R0_VALUE ^ \
   (RUNTIME_PC_SOURCE_R1_VALUE << \
    ((RUNTIME_PC_SOURCE_EOR_REG_RS_PC + 8u) & 0xffu)))
#define RUNTIME_PC_SOURCE_INITIAL_CPSR (0x30000000u | RUNTIME_CPSR_LOW_VALUE)
#define RUNTIME_PC_SOURCE_CPSR_VALUE (0x70000000u | RUNTIME_CPSR_LOW_VALUE)
#define RUNTIME_WRITEBACK_STORE_START_PC 0x08000f80u
#define RUNTIME_WRITEBACK_STORE_END_PC \
  (RUNTIME_WRITEBACK_STORE_START_PC + 4u)
#define RUNTIME_WRITEBACK_LOAD_START_PC 0x08000fa0u
#define RUNTIME_WRITEBACK_LOAD_END_PC \
  (RUNTIME_WRITEBACK_LOAD_START_PC + 4u)
#define RUNTIME_WRITEBACK_STORE_BASE_CYCLES 6u
#define RUNTIME_WRITEBACK_LOAD_BASE_CYCLES 7u
#define RUNTIME_WRITEBACK_STORE_TOTAL_CYCLES \
  (RUNTIME_WRITEBACK_STORE_BASE_CYCLES + 1u)
#define RUNTIME_WRITEBACK_LOAD_TOTAL_CYCLES \
  (RUNTIME_WRITEBACK_LOAD_BASE_CYCLES + 2u)
#define RUNTIME_WRITEBACK_STR_R3_R3_0X10_WB 0xe5a33010u
#define RUNTIME_WRITEBACK_LDRB_R5_R4_POST_NEG_0X20 0xe4545020u
#define RUNTIME_WRITEBACK_BASE_ADDR 0x02000400u
#define RUNTIME_WRITEBACK_STORE_ADDR \
  (RUNTIME_WRITEBACK_BASE_ADDR + 0x10u)
#define RUNTIME_WRITEBACK_POST_LOAD_ADDR RUNTIME_WRITEBACK_BASE_ADDR
#define RUNTIME_WRITEBACK_POST_LOAD_R4 \
  (RUNTIME_WRITEBACK_BASE_ADDR - 0x20u)
#define RUNTIME_WRITEBACK_POST_LOAD_VALUE 0xc7u
#define RUNTIME_REG_OFFSET_LOAD_START_PC 0x08001000u
#define RUNTIME_REG_OFFSET_LDR_PC RUNTIME_REG_OFFSET_LOAD_START_PC
#define RUNTIME_REG_OFFSET_LDRB_PC \
  (RUNTIME_REG_OFFSET_LOAD_START_PC + 4u)
#define RUNTIME_REG_OFFSET_LOAD_END_PC \
  (RUNTIME_REG_OFFSET_LOAD_START_PC + 8u)
#define RUNTIME_REG_OFFSET_LDR_BASE_CYCLES 5u
#define RUNTIME_REG_OFFSET_LDRB_BASE_CYCLES 6u
#define RUNTIME_REG_OFFSET_LOAD_TOTAL_CYCLES \
  ((RUNTIME_REG_OFFSET_LDR_BASE_CYCLES + 2u) + \
   (RUNTIME_REG_OFFSET_LDRB_BASE_CYCLES + 2u))
#define RUNTIME_SHIFTED_REG_OFFSET_START_PC 0x08001020u
#define RUNTIME_SHIFTED_REG_OFFSET_END_PC \
  (RUNTIME_SHIFTED_REG_OFFSET_START_PC + 4u)
#define RUNTIME_SHIFTED_REG_OFFSET_CYCLES 7u
#define RUNTIME_SHIFTED_REG_OFFSET_TOTAL_CYCLES \
  (RUNTIME_SHIFTED_REG_OFFSET_CYCLES + 2u)
#define RUNTIME_REG_OFFSET_RRX_LOAD_START_PC 0x08001040u
#define RUNTIME_REG_OFFSET_RRX_LOAD_END_PC \
  (RUNTIME_REG_OFFSET_RRX_LOAD_START_PC + 4u)
#define RUNTIME_REG_OFFSET_RRX_LOAD_CYCLES 7u
#define RUNTIME_REG_OFFSET_RRX_LOAD_TOTAL_CYCLES \
  (RUNTIME_REG_OFFSET_RRX_LOAD_CYCLES + 2u)
#define RUNTIME_REG_OFFSET_LDR_R8_R3_R2 0xe7938002u
#define RUNTIME_REG_OFFSET_LDRB_R9_R3_NEG_R2 0xe7539002u
#define RUNTIME_SHIFTED_REG_OFFSET_LDRB_R10_R3_R2_LSL2 0xe7d3a102u
#define RUNTIME_REG_OFFSET_LDR_R11_R3_R2_RRX 0xe793b062u
#define RUNTIME_REG_OFFSET_BASE_ADDR 0x02000300u
#define RUNTIME_REG_OFFSET_VALUE 0x34u
#define RUNTIME_REG_OFFSET_RRX_VALUE 0x38u
#define RUNTIME_REG_OFFSET_WORD_ADDR \
  (RUNTIME_REG_OFFSET_BASE_ADDR + RUNTIME_REG_OFFSET_VALUE)
#define RUNTIME_REG_OFFSET_BYTE_ADDR \
  (RUNTIME_REG_OFFSET_BASE_ADDR - RUNTIME_REG_OFFSET_VALUE)
#define RUNTIME_SHIFTED_REG_OFFSET_BYTE_ADDR \
  (RUNTIME_REG_OFFSET_BASE_ADDR + (RUNTIME_REG_OFFSET_VALUE << 2))
#define RUNTIME_REG_OFFSET_RRX_WORD_ADDR 0x8200031cu
#define RUNTIME_REG_OFFSET_WORD_VALUE 0x55667788u
#define RUNTIME_REG_OFFSET_BYTE_VALUE 0xa5u
#define RUNTIME_SHIFTED_REG_OFFSET_BYTE_VALUE 0x5au
#define RUNTIME_REG_OFFSET_RRX_WORD_VALUE 0xaabbccddu
#define RUNTIME_CPSR_C_BIT 0x20000000u
#define RUNTIME_SWP_START_PC 0x08000a00u
#define RUNTIME_SWP_END_PC (RUNTIME_SWP_START_PC + 4u)
#define RUNTIME_SWP_BASE_CYCLES 6u
#define RUNTIME_SWP_TOTAL_CYCLES (RUNTIME_SWP_BASE_CYCLES + 3u)
#define RUNTIME_SWP_R4_R5_R3 0xe1034095u
#define RUNTIME_SWP_ADDR 0x02000200u
#define FRAME_COMPLETE 0x80000000u
#define IDLE_LOOP_DISABLED 0xffffffffu

enum harness_backend {
  BACKEND_INTERP = 0,
  BACKEND_RV32IM = 1,
};

struct harness_state {
  enum harness_backend backend;
  u32 frames;
  u32 cycles;
  u32 blocks;
  u32 instructions;
  u32 loaded_bytes;
  u32 loaded_hash;
  u32 last_frame_hash;
};

struct compare_snapshot {
  u32 frame_hash;
  u32 reg_hash;
  u32 mem_hash;
  u32 scheduler_hash;
  u32 blocks;
  u32 fallbacks;
  u32 native_data_proc;
  u32 native_branch;
  u32 native_load;
  u32 native_store;
  u32 native_psr;
};

static struct harness_state g_state;
static u8 g_frame[FRAME_BYTES];
static u8 g_png_raw[PNG_RAW_SIZE];
static u8 g_zlib[ZLIB_SIZE];
static char g_line[512];

u32 reg[REG_MAX];
u32 spsr[6];
u32 reg_mode[7][7];
u32 idle_loop_target_pc;
u32 rom_cache_watermark;
u32 gamepak_sticky_bit[1024 / 32];

static u8 *g_runtime_code;
static u8 *g_runtime_entry;
static u8 *g_runtime_load_entry;
static u8 *g_runtime_store_entry;
static u8 *g_runtime_branch_entry;
static u8 *g_runtime_branch_target_entry;
static u8 *g_runtime_unsupported_entry;
static u8 *g_runtime_bx_entry;
static u8 *g_runtime_bx_target_entry;
static u8 *g_runtime_patch_branch_entry;
static u8 *g_runtime_patch_branch_target_entry;
static u8 *g_runtime_bl_entry;
static u8 *g_runtime_bl_target_entry;
static u8 *g_runtime_swi_entry;
static u8 *g_runtime_swi_target_entry;
static u8 *g_runtime_cond_entry;
static u8 *g_runtime_pc_write_movs_entry;
static u8 *g_runtime_swp_entry;
static u8 *g_runtime_multiply_entry;
static u8 *g_runtime_multiply_long_entry;
static u8 *g_runtime_multiply_long_flag_umulls_entry;
static u8 *g_runtime_multiply_long_flag_smulls_entry;
static u8 *g_runtime_multiply_long_acc_entry;
static u8 *g_runtime_multiply_long_acc_flag_umlals_entry;
static u8 *g_runtime_multiply_long_acc_flag_smlals_entry;
static u8 *g_runtime_flag_adds_entry;
static u8 *g_runtime_flag_cmp_entry;
static u8 *g_runtime_psr_entry;
static u8 *g_runtime_msr_cpsr_flags_entry;
static u8 *g_runtime_msr_spsr_entry;
static u8 *g_runtime_half_load_entry;
static u8 *g_runtime_half_store_entry;
static u8 *g_runtime_half_reg_load_entry;
static u8 *g_runtime_half_reg_store_entry;
static u8 *g_runtime_block_mem_stm_entry;
static u8 *g_runtime_block_mem_ldm_entry;
static u8 *g_runtime_block_mem_push_entry;
static u8 *g_runtime_block_mem_ldm_pc_entry;
static u8 *g_runtime_block_mem_ldm_pc_s_entry;
static u8 *g_runtime_hle_div_entry;
static u8 *g_runtime_hle_divarm_entry;
static u8 *g_runtime_pc_source_entry;
static u8 *g_runtime_writeback_store_entry;
static u8 *g_runtime_writeback_load_entry;
static u8 *g_runtime_reg_offset_load_entry;
static u8 *g_runtime_shifted_reg_offset_entry;
static u8 *g_runtime_reg_offset_rrx_load_entry;
static u32 g_runtime_code_bytes;
static u32 g_runtime_lookup_calls;
static u32 g_runtime_lookup_pc;
static u32 g_runtime_thumb_lookup_calls;
static u32 g_runtime_execute_calls;
static u32 g_runtime_execute_cycles;
static u32 g_runtime_execute_pc;
static u32 g_runtime_update_calls;
static s32 g_runtime_update_cycles;
static u32 g_runtime_read_calls;
static u32 g_runtime_write_calls;
static u32 g_runtime_read32_calls;
static u32 g_runtime_read32_addr;
static u32 g_runtime_read32_pc;
static u32 g_runtime_read32_value;
static u32 g_runtime_read8_calls;
static u32 g_runtime_read8_addr;
static u32 g_runtime_read8_pc;
static u32 g_runtime_read8_value;
static u32 g_runtime_read16_calls;
static u32 g_runtime_read16_addr;
static u32 g_runtime_read16_pc;
static u32 g_runtime_read16_value;
static u32 g_runtime_read8s_calls;
static u32 g_runtime_read8s_addr;
static u32 g_runtime_read8s_pc;
static u32 g_runtime_read8s_value;
static u32 g_runtime_read16s_calls;
static u32 g_runtime_read16s_addr;
static u32 g_runtime_read16s_pc;
static u32 g_runtime_read16s_value;
static u32 g_runtime_write32_calls;
static u32 g_runtime_write32_addr;
static u32 g_runtime_write32_pc;
static u32 g_runtime_write32_value;
static u32 g_runtime_write16_calls;
static u32 g_runtime_write16_addr;
static u32 g_runtime_write16_pc;
static u32 g_runtime_write16_value;
static u32 g_runtime_block_mem32_hash;
static cpu_alert_type g_runtime_store_alert;
static u32 g_runtime_flush_calls;
static u32 g_runtime_irq_check_calls;
static u32 g_runtime_bios_hook_calls;
static u32 g_runtime_fixture_load_word;
static u32 g_runtime_fixture_load_byte;
static u32 g_runtime_fixture_store_word;
static u32 g_runtime_fixture_swp_old_word;
static u32 g_runtime_fixture_branch_r1;
static u32 g_runtime_fixture_branch_r2;

static void clear_runtime_fixture_entries(void)
{
  g_runtime_entry = (u8 *)0;
  g_runtime_load_entry = (u8 *)0;
  g_runtime_store_entry = (u8 *)0;
  g_runtime_branch_entry = (u8 *)0;
  g_runtime_branch_target_entry = (u8 *)0;
  g_runtime_unsupported_entry = (u8 *)0;
  g_runtime_bx_entry = (u8 *)0;
  g_runtime_bx_target_entry = (u8 *)0;
  g_runtime_patch_branch_entry = (u8 *)0;
  g_runtime_patch_branch_target_entry = (u8 *)0;
  g_runtime_bl_entry = (u8 *)0;
  g_runtime_bl_target_entry = (u8 *)0;
  g_runtime_swi_entry = (u8 *)0;
  g_runtime_swi_target_entry = (u8 *)0;
  g_runtime_cond_entry = (u8 *)0;
  g_runtime_pc_write_movs_entry = (u8 *)0;
  g_runtime_swp_entry = (u8 *)0;
  g_runtime_multiply_entry = (u8 *)0;
  g_runtime_multiply_long_entry = (u8 *)0;
  g_runtime_multiply_long_flag_umulls_entry = (u8 *)0;
  g_runtime_multiply_long_flag_smulls_entry = (u8 *)0;
  g_runtime_multiply_long_acc_entry = (u8 *)0;
  g_runtime_multiply_long_acc_flag_umlals_entry = (u8 *)0;
  g_runtime_multiply_long_acc_flag_smlals_entry = (u8 *)0;
  g_runtime_flag_adds_entry = (u8 *)0;
  g_runtime_flag_cmp_entry = (u8 *)0;
  g_runtime_psr_entry = (u8 *)0;
  g_runtime_msr_cpsr_flags_entry = (u8 *)0;
  g_runtime_msr_spsr_entry = (u8 *)0;
  g_runtime_half_load_entry = (u8 *)0;
  g_runtime_half_store_entry = (u8 *)0;
  g_runtime_half_reg_load_entry = (u8 *)0;
  g_runtime_half_reg_store_entry = (u8 *)0;
  g_runtime_block_mem_stm_entry = (u8 *)0;
  g_runtime_block_mem_ldm_entry = (u8 *)0;
  g_runtime_block_mem_push_entry = (u8 *)0;
  g_runtime_block_mem_ldm_pc_entry = (u8 *)0;
  g_runtime_block_mem_ldm_pc_s_entry = (u8 *)0;
  g_runtime_hle_div_entry = (u8 *)0;
  g_runtime_hle_divarm_entry = (u8 *)0;
  g_runtime_pc_source_entry = (u8 *)0;
  g_runtime_writeback_store_entry = (u8 *)0;
  g_runtime_writeback_load_entry = (u8 *)0;
  g_runtime_reg_offset_load_entry = (u8 *)0;
  g_runtime_shifted_reg_offset_entry = (u8 *)0;
  g_runtime_reg_offset_rrx_load_entry = (u8 *)0;
}

static void render_frame(void);

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

static long syscall4(long number, long arg0, long arg1, long arg2, long arg3)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a3), "r"(a7)
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

static int write_all(int fd, const void *data, usize size)
{
  const u8 *ptr = (const u8 *)data;

  while (size)
  {
    long ret = syscall3(SYS_WRITE, fd, (long)ptr, (long)size);
    if (ret <= 0)
      return -1;
    ptr += (usize)ret;
    size -= (usize)ret;
  }

  return 0;
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
  write_all(STDOUT_FD, text, cstr_len(text));
}

static void put_chr(char ch)
{
  write_all(STDOUT_FD, &ch, 1);
}

static void put_u32_dec(u32 value)
{
  char tmp[10];
  usize pos = 0;

  if (value == 0)
  {
    put_chr('0');
    return;
  }

  while (value)
  {
    tmp[pos++] = (char)('0' + (value % 10));
    value /= 10;
  }

  while (pos)
    put_chr(tmp[--pos]);
}

static char hex_digit(u32 value)
{
  value &= 0xf;
  return (char)(value < 10 ? ('0' + value) : ('a' + value - 10));
}

static void put_u32_hex(u32 value)
{
  s32 shift;
  put_raw("0x");
  for (shift = 28; shift >= 0; shift -= 4)
    put_chr(hex_digit(value >> (u32)shift));
}

static int is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char *skip_space(char *text)
{
  while (*text && is_space(*text))
    text++;
  return text;
}

static char *next_token(char **cursor)
{
  char *start = skip_space(*cursor);
  char *end = start;

  while (*end && !is_space(*end))
    end++;

  if (*end)
    *end++ = 0;

  *cursor = end;
  return start;
}

static int str_eq(const char *a, const char *b)
{
  while (*a && *b && *a == *b)
  {
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static int parse_u32_token(const char *text, u32 *out)
{
  u32 base = 10;
  u32 value = 0;

  if (!text || !*text)
    return 0;

  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
  {
    base = 16;
    text += 2;
  }

  if (!*text)
    return 0;

  while (*text)
  {
    u32 digit;
    char ch = *text++;

    if (ch >= '0' && ch <= '9')
      digit = (u32)(ch - '0');
    else if (ch >= 'a' && ch <= 'f')
      digit = (u32)(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F')
      digit = (u32)(ch - 'A' + 10);
    else
      return 0;

    if (digit >= base)
      return 0;
    value = value * base + digit;
  }

  *out = value;
  return 1;
}

static u32 fnv1a_update(u32 hash, const u8 *data, usize size)
{
  usize i;
  for (i = 0; i < size; i++)
  {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

static u32 fnv1a_update_u32(u32 hash, u32 value)
{
  u8 bytes[4];

  bytes[0] = (u8)value;
  bytes[1] = (u8)(value >> 8);
  bytes[2] = (u8)(value >> 16);
  bytes[3] = (u8)(value >> 24);
  return fnv1a_update(hash, bytes, sizeof(bytes));
}

static void *map_runtime_exec_page(void)
{
  long ret = syscall6(SYS_MMAP, 0, RUNTIME_EXEC_MAP_BYTES,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if ((u32)ret >= 0xfffff000u)
    return (void *)0;
  return (void *)ret;
}

static void reset_runtime_fixture_state(u32 pc)
{
  unsigned i;

  for (i = 0; i < REG_MAX; i++)
    reg[i] = 0;
  for (i = 0; i < 6; i++)
    spsr[i] = 0;
  for (i = 0; i < 7 * 7; i++)
    ((u32 *)reg_mode)[i] = 0;
  for (i = 0; i < (1024 / 32); i++)
    gamepak_sticky_bit[i] = 0xffffffffu;

  reg[REG_PC] = pc;
  reg[REG_CPSR] = 0;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
  idle_loop_target_pc = IDLE_LOOP_DISABLED;
  g_runtime_lookup_calls = 0;
  g_runtime_lookup_pc = 0;
  g_runtime_thumb_lookup_calls = 0;
  g_runtime_execute_calls = 0;
  g_runtime_execute_cycles = 0;
  g_runtime_execute_pc = 0;
  g_runtime_update_calls = 0;
  g_runtime_update_cycles = (s32)RUNTIME_NO_UPDATE_CYCLES;
  g_runtime_read_calls = 0;
  g_runtime_write_calls = 0;
  g_runtime_read32_calls = 0;
  g_runtime_read32_addr = 0;
  g_runtime_read32_pc = 0;
  g_runtime_read32_value = 0;
  g_runtime_read8_calls = 0;
  g_runtime_read8_addr = 0;
  g_runtime_read8_pc = 0;
  g_runtime_read8_value = 0;
  g_runtime_read16_calls = 0;
  g_runtime_read16_addr = 0;
  g_runtime_read16_pc = 0;
  g_runtime_read16_value = 0;
  g_runtime_read8s_calls = 0;
  g_runtime_read8s_addr = 0;
  g_runtime_read8s_pc = 0;
  g_runtime_read8s_value = 0;
  g_runtime_read16s_calls = 0;
  g_runtime_read16s_addr = 0;
  g_runtime_read16s_pc = 0;
  g_runtime_read16s_value = 0;
  g_runtime_write32_calls = 0;
  g_runtime_write32_addr = 0;
  g_runtime_write32_pc = 0;
  g_runtime_write32_value = 0;
  g_runtime_write16_calls = 0;
  g_runtime_write16_addr = 0;
  g_runtime_write16_pc = 0;
  g_runtime_write16_value = 0;
  g_runtime_block_mem32_hash = 2166136261u;
  g_runtime_store_alert = CPU_ALERT_NONE;
  g_runtime_flush_calls = 0;
  g_runtime_irq_check_calls = 0;
}

static int build_runtime_fixture_block(const char **reason)
{
  u8 *translation_ptr = g_runtime_code;
  riscv_jit_block_meta *meta;
  long flush_ret;
  u32 add_code_bytes;
  u32 load_code_bytes;
  u32 store_code_bytes;
  u32 branch_code_bytes;
  u32 branch_target_code_bytes;
  u32 unsupported_code_bytes;
  u32 bx_code_bytes;
  u32 bx_target_code_bytes;
  u32 patch_branch_code_bytes;
  u32 patch_branch_target_code_bytes;
  u32 bl_code_bytes;
  u32 bl_target_code_bytes;
  u32 swi_code_bytes;
  u32 swi_target_code_bytes;
  u32 cond_code_bytes;
  u32 pc_write_movs_code_bytes;
  u32 swp_code_bytes;
  u32 multiply_code_bytes;
  u32 multiply_long_code_bytes;
  u32 multiply_long_flag_umulls_code_bytes;
  u32 multiply_long_flag_smulls_code_bytes;
  u32 multiply_long_acc_code_bytes;
  u32 multiply_long_acc_flag_umlals_code_bytes;
  u32 multiply_long_acc_flag_smlals_code_bytes;
  u32 flag_adds_code_bytes;
  u32 flag_cmp_code_bytes;
  u32 psr_code_bytes;
  u32 msr_cpsr_flags_code_bytes;
  u32 msr_spsr_code_bytes;
  u32 half_load_code_bytes;
  u32 half_store_code_bytes;
  u32 half_reg_load_code_bytes;
  u32 half_reg_store_code_bytes;
  u32 block_mem_stm_code_bytes;
  u32 block_mem_ldm_code_bytes;
  u32 block_mem_push_code_bytes;
  u32 block_mem_ldm_pc_code_bytes;
  u32 block_mem_ldm_pc_s_code_bytes;
  u32 hle_div_code_bytes;
  u32 hle_divarm_code_bytes;
  u32 pc_source_code_bytes;
  u32 writeback_store_code_bytes;
  u32 writeback_load_code_bytes;
  u32 reg_offset_load_code_bytes;
  u32 shifted_reg_offset_code_bytes;
  u32 reg_offset_rrx_load_code_bytes;
  u8 *patch_branch_source;
  u8 *cond_skip_source;

  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       RUNTIME_ADD_R2_R0_R1,
                                       RUNTIME_CYCLES))
  {
    *reason = "runtime_emit_rejected";
    g_runtime_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_START_PC,
                            RUNTIME_END_PC, false);
  add_code_bytes = (u32)(translation_ptr - g_runtime_code);

  translation_ptr = g_runtime_code + RUNTIME_LOAD_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_load_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           RUNTIME_LOAD_LDR_R4_R3_0X24,
                                           RUNTIME_LOAD_WORD_PC,
                                           RUNTIME_LOAD_WORD_BASE_CYCLES))
  {
    *reason = "runtime_load_word_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    return 0;
  }

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           RUNTIME_LOAD_LDRB_R5_R3_0X25,
                                           RUNTIME_LOAD_BYTE_PC,
                                           RUNTIME_LOAD_BYTE_BASE_CYCLES))
  {
    *reason = "runtime_load_byte_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_LOAD_START_PC,
                            RUNTIME_LOAD_END_PC, false);
  load_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_LOAD_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_STORE_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_store_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           RUNTIME_STORE_STR_R6_R3_0X28,
                                           RUNTIME_STORE_START_PC,
                                           RUNTIME_STORE_BASE_CYCLES))
  {
    *reason = "runtime_store_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_STORE_START_PC,
                            RUNTIME_STORE_END_PC, false);
  store_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_STORE_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BRANCH_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_branch_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_b(&translation_ptr, meta,
                               RUNTIME_BRANCH_B_PLUS_12,
                               RUNTIME_BRANCH_START_PC,
                               RUNTIME_BRANCH_CYCLES))
  {
    *reason = "runtime_branch_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_BRANCH_START_PC,
                            RUNTIME_BRANCH_START_PC + 4u, false);
  branch_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_BRANCH_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BRANCH_TARGET_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_branch_target_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       RUNTIME_BRANCH_TARGET_ADD_R3_R2_R1,
                                       RUNTIME_BRANCH_TARGET_CYCLES))
  {
    *reason = "runtime_branch_target_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_BRANCH_TARGET_PC,
                            RUNTIME_BRANCH_TARGET_END_PC, false);
  branch_target_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_BRANCH_TARGET_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_UNSUPPORTED_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_unsupported_entry = ((u8 *)meta) + block_prologue_size;
  riscv_mark_block_unsupported(meta);
  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_UNSUPPORTED_START_PC,
                            RUNTIME_UNSUPPORTED_END_PC, false);
  unsupported_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_UNSUPPORTED_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BX_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_bx_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_bx(&translation_ptr, meta,
                                RUNTIME_BX_R7,
                                RUNTIME_BX_START_PC,
                                RUNTIME_BX_CYCLES))
  {
    *reason = "runtime_bx_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_BX_START_PC,
                            RUNTIME_BX_START_PC + 4u, false);
  bx_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_BX_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BX_TARGET_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_bx_target_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       RUNTIME_BX_TARGET_ADD_R4_R1_R2,
                                       RUNTIME_BX_TARGET_CYCLES))
  {
    *reason = "runtime_bx_target_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_BX_TARGET_PC,
                            RUNTIME_BX_TARGET_END_PC, false);
  bx_target_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_BX_TARGET_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_PATCH_BRANCH_TARGET_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_patch_branch_target_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(
        &translation_ptr, meta,
        RUNTIME_PATCH_BRANCH_TARGET_ADD_R5_R1_R2,
        RUNTIME_PATCH_BRANCH_TARGET_CYCLES))
  {
    *reason = "runtime_patch_branch_target_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_PATCH_BRANCH_TARGET_PC,
                            RUNTIME_PATCH_BRANCH_TARGET_END_PC, false);
  patch_branch_target_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_PATCH_BRANCH_TARGET_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_PATCH_BRANCH_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_patch_branch_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_b_patchable(&translation_ptr, meta,
                                         &patch_branch_source,
                                         RUNTIME_PATCH_BRANCH_B_PLUS_12,
                                         RUNTIME_PATCH_BRANCH_START_PC,
                                         RUNTIME_PATCH_BRANCH_CYCLES))
  {
    *reason = "runtime_patch_branch_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    return 0;
  }

  if (!patch_branch_source)
  {
    *reason = "runtime_patch_branch_no_patch_site";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_PATCH_BRANCH_START_PC,
                            RUNTIME_PATCH_BRANCH_START_PC + 4u, false);
  riscv_patch_unconditional_branch(patch_branch_source,
                                   g_runtime_patch_branch_target_entry);
  patch_branch_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_PATCH_BRANCH_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BL_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_bl_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_bl(&translation_ptr, meta,
                                RUNTIME_BL_PLUS_16,
                                RUNTIME_BL_START_PC,
                                RUNTIME_BL_CYCLES))
  {
    *reason = "runtime_bl_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_BL_START_PC,
                            RUNTIME_BL_START_PC + 4u, false);
  bl_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_BL_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BL_TARGET_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_bl_target_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       RUNTIME_BL_TARGET_ADD_R8_R1_R2,
                                       RUNTIME_BL_TARGET_CYCLES))
  {
    *reason = "runtime_bl_target_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_BL_TARGET_PC,
                            RUNTIME_BL_TARGET_END_PC, false);
  bl_target_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_BL_TARGET_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_SWI_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_swi_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_swi(&translation_ptr, meta,
                                 RUNTIME_SWI_OPCODE_5,
                                 RUNTIME_SWI_START_PC,
                                 RUNTIME_SWI_CYCLES))
  {
    *reason = "runtime_swi_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_SWI_START_PC,
                            RUNTIME_SWI_END_PC, false);
  swi_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_SWI_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_SWI_TARGET_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_swi_target_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       RUNTIME_SWI_TARGET_ADD_R9_R1_R2,
                                       RUNTIME_SWI_TARGET_CYCLES))
  {
    *reason = "runtime_swi_target_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_SWI_TARGET_PC,
                            RUNTIME_SWI_TARGET_END_PC, false);
  swi_target_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_SWI_TARGET_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_COND_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_cond_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_arm_conditional_block_header(
        &translation_ptr, meta, RUNTIME_COND_NE, RUNTIME_COND_CYCLES,
        &cond_skip_source))
  {
    *reason = "runtime_cond_header_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    return 0;
  }

  if (!cond_skip_source)
  {
    *reason = "runtime_cond_header_no_patch_site";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    return 0;
  }

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       RUNTIME_COND_ADD_R10_R1_R2, 0))
  {
    *reason = "runtime_cond_data_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    return 0;
  }

  riscv_patch_unconditional_branch(cond_skip_source, translation_ptr);
  riscv_emit_block_finalize(meta, &translation_ptr, RUNTIME_COND_START_PC,
                            RUNTIME_COND_END_PC, false);
  cond_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_COND_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_PC_WRITE_MOVS_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_pc_write_movs_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       RUNTIME_MOVS_PC_R14,
                                       RUNTIME_PC_WRITE_MOVS_CYCLES))
  {
    *reason = "runtime_pc_write_movs_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_PC_WRITE_MOVS_START_PC,
                            RUNTIME_PC_WRITE_MOVS_END_PC, false);
  pc_write_movs_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_PC_WRITE_MOVS_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_SWP_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_swp_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_swap(&translation_ptr, meta,
                                  RUNTIME_SWP_R4_R5_R3,
                                  RUNTIME_SWP_START_PC,
                                  RUNTIME_SWP_BASE_CYCLES))
  {
    *reason = "runtime_swp_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_SWP_START_PC,
                            RUNTIME_SWP_END_PC, false);
  swp_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_SWP_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_MULTIPLY_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_multiply_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_multiply(&translation_ptr, meta,
                                      RUNTIME_MULTIPLY_MUL_R4_R1_R2,
                                      RUNTIME_MULTIPLY_MUL_CYCLES))
  {
    *reason = "runtime_multiply_mul_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    g_runtime_multiply_entry = (u8 *)0;
    return 0;
  }

  if (!riscv_emit_native_arm_multiply(&translation_ptr, meta,
                                      RUNTIME_MULTIPLY_MLA_R5_R1_R2_R3,
                                      RUNTIME_MULTIPLY_MLA_CYCLES))
  {
    *reason = "runtime_multiply_mla_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    g_runtime_multiply_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_MULTIPLY_START_PC,
                            RUNTIME_MULTIPLY_END_PC, false);
  multiply_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_MULTIPLY_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_MULTIPLY_LONG_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_multiply_long_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_multiply_long(
        &translation_ptr, meta,
        RUNTIME_MULTIPLY_LONG_UMULL_R8_R9_R1_R2,
        RUNTIME_MULTIPLY_LONG_UMULL_CYCLES))
  {
    *reason = "runtime_multiply_long_umull_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_multiply_long(
        &translation_ptr, meta,
        RUNTIME_MULTIPLY_LONG_SMULL_R10_R11_R3_R4,
        RUNTIME_MULTIPLY_LONG_SMULL_CYCLES))
  {
    *reason = "runtime_multiply_long_smull_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_MULTIPLY_LONG_START_PC,
                            RUNTIME_MULTIPLY_LONG_END_PC, false);
  multiply_long_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_MULTIPLY_LONG_BLOCK_OFFSET));

  translation_ptr =
    g_runtime_code + RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_multiply_long_flag_umulls_entry =
    ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_multiply_long(
        &translation_ptr, meta,
        RUNTIME_MULTIPLY_LONG_UMULLS_R8_R9_R1_R2,
        RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_CYCLES))
  {
    *reason = "runtime_multiply_long_umulls_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_START_PC,
                            RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_END_PC,
                            false);
  multiply_long_flag_umulls_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code +
           RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_BLOCK_OFFSET));

  translation_ptr =
    g_runtime_code + RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_multiply_long_flag_smulls_entry =
    ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_multiply_long(
        &translation_ptr, meta,
        RUNTIME_MULTIPLY_LONG_SMULLS_R10_R11_R3_R4,
        RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_CYCLES))
  {
    *reason = "runtime_multiply_long_smulls_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_START_PC,
                            RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_END_PC,
                            false);
  multiply_long_flag_smulls_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code +
           RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_BLOCK_OFFSET));

  translation_ptr =
    g_runtime_code + RUNTIME_MULTIPLY_LONG_ACC_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_multiply_long_acc_entry =
    ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_multiply_long(
        &translation_ptr, meta,
        RUNTIME_MULTIPLY_LONG_UMLAL_R8_R9_R1_R2,
        RUNTIME_MULTIPLY_LONG_UMLAL_CYCLES))
  {
    *reason = "runtime_multiply_long_umlal_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_multiply_long(
        &translation_ptr, meta,
        RUNTIME_MULTIPLY_LONG_SMLAL_R10_R11_R3_R4,
        RUNTIME_MULTIPLY_LONG_SMLAL_CYCLES))
  {
    *reason = "runtime_multiply_long_smlal_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_MULTIPLY_LONG_ACC_START_PC,
                            RUNTIME_MULTIPLY_LONG_ACC_END_PC, false);
  multiply_long_acc_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_MULTIPLY_LONG_ACC_BLOCK_OFFSET));

  translation_ptr =
    g_runtime_code + RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_multiply_long_acc_flag_umlals_entry =
    ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_multiply_long(
        &translation_ptr, meta,
        RUNTIME_MULTIPLY_LONG_UMLALS_R8_R9_R1_R2,
        RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_CYCLES))
  {
    *reason = "runtime_multiply_long_umlals_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(
    meta, &translation_ptr,
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_START_PC,
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_END_PC, false);
  multiply_long_acc_flag_umlals_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code +
           RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_BLOCK_OFFSET));

  translation_ptr =
    g_runtime_code + RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_multiply_long_acc_flag_smlals_entry =
    ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_multiply_long(
        &translation_ptr, meta,
        RUNTIME_MULTIPLY_LONG_SMLALS_R10_R11_R3_R4,
        RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_CYCLES))
  {
    *reason = "runtime_multiply_long_smlals_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(
    meta, &translation_ptr,
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_START_PC,
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_END_PC, false);
  multiply_long_acc_flag_smlals_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code +
           RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_FLAG_ADDS_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_flag_adds_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc(&translation_ptr, meta,
                                       RUNTIME_ADDS_R7_R1_0X1,
                                       RUNTIME_FLAG_ADDS_CYCLES))
  {
    *reason = "runtime_flag_adds_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    g_runtime_multiply_entry = (u8 *)0;
    g_runtime_flag_adds_entry = (u8 *)0;
    g_runtime_flag_cmp_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_FLAG_ADDS_START_PC,
                            RUNTIME_FLAG_ADDS_END_PC, false);
  flag_adds_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_FLAG_ADDS_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_FLAG_CMP_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_flag_cmp_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc_test(&translation_ptr, meta,
                                           RUNTIME_CMP_R0_0X20,
                                           RUNTIME_FLAG_CMP_CYCLES))
  {
    *reason = "runtime_flag_cmp_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    g_runtime_multiply_entry = (u8 *)0;
    g_runtime_flag_adds_entry = (u8 *)0;
    g_runtime_flag_cmp_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_FLAG_CMP_START_PC,
                            RUNTIME_FLAG_CMP_END_PC, false);
  flag_cmp_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_FLAG_CMP_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_PSR_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_psr_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_psr(&translation_ptr, meta,
                                 RUNTIME_MRS_R6_CPSR,
                                 RUNTIME_PSR_MRS_CPSR_CYCLES))
  {
    *reason = "runtime_psr_mrs_cpsr_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    g_runtime_multiply_entry = (u8 *)0;
    g_runtime_flag_adds_entry = (u8 *)0;
    g_runtime_flag_cmp_entry = (u8 *)0;
    g_runtime_psr_entry = (u8 *)0;
    return 0;
  }

  if (!riscv_emit_native_arm_psr(&translation_ptr, meta,
                                 RUNTIME_MRS_R7_SPSR,
                                 RUNTIME_PSR_MRS_SPSR_CYCLES))
  {
    *reason = "runtime_psr_mrs_spsr_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    g_runtime_multiply_entry = (u8 *)0;
    g_runtime_flag_adds_entry = (u8 *)0;
    g_runtime_flag_cmp_entry = (u8 *)0;
    g_runtime_psr_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_PSR_START_PC,
                            RUNTIME_PSR_END_PC, false);
  psr_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_PSR_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_MSR_CPSR_FLAGS_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_msr_cpsr_flags_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_psr_with_pc(&translation_ptr, meta,
                                         RUNTIME_MSR_CPSR_FLAGS_IMM,
                                         RUNTIME_MSR_CPSR_FLAGS_START_PC,
                                         RUNTIME_MSR_CPSR_FLAGS_CYCLES))
  {
    *reason = "runtime_msr_cpsr_flags_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    g_runtime_multiply_entry = (u8 *)0;
    g_runtime_flag_adds_entry = (u8 *)0;
    g_runtime_flag_cmp_entry = (u8 *)0;
    g_runtime_psr_entry = (u8 *)0;
    g_runtime_msr_cpsr_flags_entry = (u8 *)0;
    g_runtime_msr_spsr_entry = (u8 *)0;
    g_runtime_half_load_entry = (u8 *)0;
    g_runtime_half_store_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_MSR_CPSR_FLAGS_START_PC,
                            RUNTIME_MSR_CPSR_FLAGS_END_PC, false);
  msr_cpsr_flags_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_MSR_CPSR_FLAGS_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_MSR_SPSR_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_msr_spsr_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_psr_with_pc(&translation_ptr, meta,
                                         RUNTIME_MSR_SPSR_R1,
                                         RUNTIME_MSR_SPSR_START_PC,
                                         RUNTIME_MSR_SPSR_CYCLES))
  {
    *reason = "runtime_msr_spsr_emit_rejected";
    g_runtime_entry = (u8 *)0;
    g_runtime_load_entry = (u8 *)0;
    g_runtime_store_entry = (u8 *)0;
    g_runtime_branch_entry = (u8 *)0;
    g_runtime_branch_target_entry = (u8 *)0;
    g_runtime_unsupported_entry = (u8 *)0;
    g_runtime_bx_entry = (u8 *)0;
    g_runtime_bx_target_entry = (u8 *)0;
    g_runtime_patch_branch_entry = (u8 *)0;
    g_runtime_patch_branch_target_entry = (u8 *)0;
    g_runtime_bl_entry = (u8 *)0;
    g_runtime_bl_target_entry = (u8 *)0;
    g_runtime_swi_entry = (u8 *)0;
    g_runtime_swi_target_entry = (u8 *)0;
    g_runtime_cond_entry = (u8 *)0;
    g_runtime_pc_write_movs_entry = (u8 *)0;
    g_runtime_swp_entry = (u8 *)0;
    g_runtime_multiply_entry = (u8 *)0;
    g_runtime_flag_adds_entry = (u8 *)0;
    g_runtime_flag_cmp_entry = (u8 *)0;
    g_runtime_psr_entry = (u8 *)0;
    g_runtime_msr_cpsr_flags_entry = (u8 *)0;
    g_runtime_msr_spsr_entry = (u8 *)0;
    g_runtime_half_load_entry = (u8 *)0;
    g_runtime_half_store_entry = (u8 *)0;
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_MSR_SPSR_START_PC,
                            RUNTIME_MSR_SPSR_END_PC, false);
  msr_spsr_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_MSR_SPSR_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_HALF_LOAD_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_half_load_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           RUNTIME_HALF_LDRH_R4_R3_0X24,
                                           RUNTIME_HALF_LDRH_PC,
                                           RUNTIME_HALF_LDRH_BASE_CYCLES))
  {
    *reason = "runtime_half_ldrh_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           RUNTIME_HALF_LDRSB_R5_R3_0X25,
                                           RUNTIME_HALF_LDRSB_PC,
                                           RUNTIME_HALF_LDRSB_BASE_CYCLES))
  {
    *reason = "runtime_half_ldrsb_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           RUNTIME_HALF_LDRSH_R6_R3_0X26,
                                           RUNTIME_HALF_LDRSH_PC,
                                           RUNTIME_HALF_LDRSH_BASE_CYCLES))
  {
    *reason = "runtime_half_ldrsh_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_HALF_LOAD_START_PC,
                            RUNTIME_HALF_LOAD_END_PC, false);
  half_load_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_HALF_LOAD_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_HALF_STORE_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_half_store_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(&translation_ptr, meta,
                                           RUNTIME_HALF_STRH_R7_R3_0X28,
                                           RUNTIME_HALF_STORE_START_PC,
                                           RUNTIME_HALF_STORE_BASE_CYCLES))
  {
    *reason = "runtime_half_strh_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_HALF_STORE_START_PC,
                            RUNTIME_HALF_STORE_END_PC, false);
  half_store_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_HALF_STORE_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_HALF_REG_LOAD_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_half_reg_load_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_HALF_REG_LDRH_R8_R3_R2,
        RUNTIME_HALF_REG_LDRH_PC,
        RUNTIME_HALF_LDRH_BASE_CYCLES))
  {
    *reason = "runtime_half_reg_ldrh_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_HALF_REG_LDRSB_R9_R3_NEG_R2,
        RUNTIME_HALF_REG_LDRSB_PC,
        RUNTIME_HALF_LDRSB_BASE_CYCLES))
  {
    *reason = "runtime_half_reg_ldrsb_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_HALF_REG_LDRSH_R10_R3_R2,
        RUNTIME_HALF_REG_LDRSH_PC,
        RUNTIME_HALF_LDRSH_BASE_CYCLES))
  {
    *reason = "runtime_half_reg_ldrsh_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_HALF_REG_LOAD_START_PC,
                            RUNTIME_HALF_REG_LOAD_END_PC, false);
  half_reg_load_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_HALF_REG_LOAD_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_HALF_REG_STORE_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_half_reg_store_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_HALF_REG_STRH_R9_R3_NEG_R2,
        RUNTIME_HALF_REG_STORE_START_PC,
        RUNTIME_HALF_STORE_BASE_CYCLES))
  {
    *reason = "runtime_half_reg_strh_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_HALF_REG_STORE_START_PC,
                            RUNTIME_HALF_REG_STORE_END_PC, false);
  half_reg_store_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_HALF_REG_STORE_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BLOCK_MEM_STM_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_block_mem_stm_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_block_memory(&translation_ptr, meta,
                                          RUNTIME_STMIA_R3_WB_R0_R2_R5,
                                          RUNTIME_BLOCK_MEM_STM_START_PC,
                                          RUNTIME_BLOCK_MEM_STM_CYCLES))
  {
    *reason = "runtime_block_mem_stm_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_BLOCK_MEM_STM_START_PC,
                            RUNTIME_BLOCK_MEM_STM_END_PC, false);
  block_mem_stm_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_BLOCK_MEM_STM_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BLOCK_MEM_LDM_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_block_mem_ldm_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_block_memory(&translation_ptr, meta,
                                          RUNTIME_LDMIA_R4_WB_R1_R6_R7,
                                          RUNTIME_BLOCK_MEM_LDM_START_PC,
                                          RUNTIME_BLOCK_MEM_LDM_CYCLES))
  {
    *reason = "runtime_block_mem_ldm_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_BLOCK_MEM_LDM_START_PC,
                            RUNTIME_BLOCK_MEM_LDM_END_PC, false);
  block_mem_ldm_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_BLOCK_MEM_LDM_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BLOCK_MEM_PUSH_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_block_mem_push_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_block_memory(
        &translation_ptr, meta,
        RUNTIME_STMDB_R13_WB_R0_R1_R14,
        RUNTIME_BLOCK_MEM_PUSH_START_PC,
        RUNTIME_BLOCK_MEM_PUSH_CYCLES))
  {
    *reason = "runtime_block_mem_push_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_BLOCK_MEM_PUSH_START_PC,
                            RUNTIME_BLOCK_MEM_PUSH_END_PC, false);
  block_mem_push_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_BLOCK_MEM_PUSH_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BLOCK_MEM_LDM_PC_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_block_mem_ldm_pc_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_block_memory(
        &translation_ptr, meta,
        RUNTIME_LDMIA_R4_WB_R1_R6_PC,
        RUNTIME_BLOCK_MEM_LDM_PC_START_PC,
        RUNTIME_BLOCK_MEM_LDM_PC_CYCLES))
  {
    *reason = "runtime_block_mem_ldm_pc_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_BLOCK_MEM_LDM_PC_START_PC,
                            RUNTIME_BLOCK_MEM_LDM_PC_END_PC, false);
  block_mem_ldm_pc_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_BLOCK_MEM_LDM_PC_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_BLOCK_MEM_LDM_PC_S_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_block_mem_ldm_pc_s_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_block_memory(
        &translation_ptr, meta,
        RUNTIME_LDMIA_R4_WB_S_R1_R6_PC,
        RUNTIME_BLOCK_MEM_LDM_PC_S_START_PC,
        RUNTIME_BLOCK_MEM_LDM_PC_CYCLES))
  {
    *reason = "runtime_block_mem_ldm_pc_s_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_BLOCK_MEM_LDM_PC_S_START_PC,
                            RUNTIME_BLOCK_MEM_LDM_PC_S_END_PC, false);
  block_mem_ldm_pc_s_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_BLOCK_MEM_LDM_PC_S_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_HLE_DIV_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_hle_div_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_hle_div(&translation_ptr, meta, false,
                                     RUNTIME_HLE_DIV_CYCLES))
  {
    *reason = "runtime_hle_div_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_HLE_DIV_START_PC,
                            RUNTIME_HLE_DIV_END_PC, false);
  hle_div_code_bytes =
    (u32)(translation_ptr - (g_runtime_code + RUNTIME_HLE_DIV_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_HLE_DIVARM_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_hle_divarm_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_hle_div(&translation_ptr, meta, true,
                                     RUNTIME_HLE_DIVARM_CYCLES))
  {
    *reason = "runtime_hle_divarm_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_HLE_DIVARM_START_PC,
                            RUNTIME_HLE_DIVARM_END_PC, false);
  hle_divarm_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_HLE_DIVARM_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_PC_SOURCE_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_pc_source_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_data_proc_with_pc(
        &translation_ptr, meta,
        RUNTIME_PC_SOURCE_ADD_R6_PC_0X20,
        RUNTIME_PC_SOURCE_ADD_PC,
        RUNTIME_PC_SOURCE_ADD_CYCLES))
  {
    *reason = "runtime_pc_source_add_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_data_proc_with_pc(
        &translation_ptr, meta,
        RUNTIME_PC_SOURCE_MOV_R7_PC,
        RUNTIME_PC_SOURCE_MOV_PC,
        RUNTIME_PC_SOURCE_MOV_CYCLES))
  {
    *reason = "runtime_pc_source_mov_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_data_proc_with_pc(
        &translation_ptr, meta,
        RUNTIME_PC_SOURCE_EOR_R8_R0_PC_LSL2,
        RUNTIME_PC_SOURCE_EOR_IMM_PC,
        RUNTIME_PC_SOURCE_EOR_IMM_CYCLES))
  {
    *reason = "runtime_pc_source_eor_imm_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_data_proc_with_pc(
        &translation_ptr, meta,
        RUNTIME_PC_SOURCE_EOR_R9_R0_PC_LSL_R2,
        RUNTIME_PC_SOURCE_EOR_REG_RM_PC,
        RUNTIME_PC_SOURCE_EOR_REG_RM_CYCLES))
  {
    *reason = "runtime_pc_source_eor_rm_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_data_proc_with_pc(
        &translation_ptr, meta,
        RUNTIME_PC_SOURCE_EOR_R10_R0_R1_LSL_PC,
        RUNTIME_PC_SOURCE_EOR_REG_RS_PC,
        RUNTIME_PC_SOURCE_EOR_REG_RS_CYCLES))
  {
    *reason = "runtime_pc_source_eor_rs_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_data_proc_test_with_pc(
        &translation_ptr, meta,
        RUNTIME_PC_SOURCE_TST_R0_PC,
        RUNTIME_PC_SOURCE_TST_PC,
        RUNTIME_PC_SOURCE_TST_CYCLES))
  {
    *reason = "runtime_pc_source_tst_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_data_proc_test_with_pc(
        &translation_ptr, meta,
        RUNTIME_PC_SOURCE_TST_R0_R1_LSL_PC,
        RUNTIME_PC_SOURCE_TST_REG_PC,
        RUNTIME_PC_SOURCE_TST_REG_CYCLES))
  {
    *reason = "runtime_pc_source_tst_reg_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_PC_SOURCE_START_PC,
                            RUNTIME_PC_SOURCE_END_PC, false);
  pc_source_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_PC_SOURCE_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_WRITEBACK_STORE_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_writeback_store_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_WRITEBACK_STR_R3_R3_0X10_WB,
        RUNTIME_WRITEBACK_STORE_START_PC,
        RUNTIME_WRITEBACK_STORE_BASE_CYCLES))
  {
    *reason = "runtime_writeback_store_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_WRITEBACK_STORE_START_PC,
                            RUNTIME_WRITEBACK_STORE_END_PC, false);
  writeback_store_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_WRITEBACK_STORE_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_WRITEBACK_LOAD_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_writeback_load_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_WRITEBACK_LDRB_R5_R4_POST_NEG_0X20,
        RUNTIME_WRITEBACK_LOAD_START_PC,
        RUNTIME_WRITEBACK_LOAD_BASE_CYCLES))
  {
    *reason = "runtime_writeback_load_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_WRITEBACK_LOAD_START_PC,
                            RUNTIME_WRITEBACK_LOAD_END_PC, false);
  writeback_load_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_WRITEBACK_LOAD_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_REG_OFFSET_LOAD_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_reg_offset_load_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_REG_OFFSET_LDR_R8_R3_R2,
        RUNTIME_REG_OFFSET_LDR_PC,
        RUNTIME_REG_OFFSET_LDR_BASE_CYCLES))
  {
    *reason = "runtime_reg_offset_ldr_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_REG_OFFSET_LDRB_R9_R3_NEG_R2,
        RUNTIME_REG_OFFSET_LDRB_PC,
        RUNTIME_REG_OFFSET_LDRB_BASE_CYCLES))
  {
    *reason = "runtime_reg_offset_ldrb_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_REG_OFFSET_LOAD_START_PC,
                            RUNTIME_REG_OFFSET_LOAD_END_PC, false);
  reg_offset_load_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_REG_OFFSET_LOAD_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_SHIFTED_REG_OFFSET_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_shifted_reg_offset_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_SHIFTED_REG_OFFSET_LDRB_R10_R3_R2_LSL2,
        RUNTIME_SHIFTED_REG_OFFSET_START_PC,
        RUNTIME_SHIFTED_REG_OFFSET_CYCLES))
  {
    *reason = "runtime_shifted_reg_offset_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_SHIFTED_REG_OFFSET_START_PC,
                            RUNTIME_SHIFTED_REG_OFFSET_END_PC, false);
  shifted_reg_offset_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_SHIFTED_REG_OFFSET_BLOCK_OFFSET));

  translation_ptr = g_runtime_code + RUNTIME_REG_OFFSET_RRX_LOAD_BLOCK_OFFSET;
  riscv_emit_block_prologue(&translation_ptr, &meta);
  g_runtime_reg_offset_rrx_load_entry = ((u8 *)meta) + block_prologue_size;

  if (!riscv_emit_native_arm_access_memory(
        &translation_ptr, meta,
        RUNTIME_REG_OFFSET_LDR_R11_R3_R2_RRX,
        RUNTIME_REG_OFFSET_RRX_LOAD_START_PC,
        RUNTIME_REG_OFFSET_RRX_LOAD_CYCLES))
  {
    *reason = "runtime_reg_offset_rrx_emit_rejected";
    clear_runtime_fixture_entries();
    return 0;
  }

  riscv_emit_block_finalize(meta, &translation_ptr,
                            RUNTIME_REG_OFFSET_RRX_LOAD_START_PC,
                            RUNTIME_REG_OFFSET_RRX_LOAD_END_PC, false);
  reg_offset_rrx_load_code_bytes =
    (u32)(translation_ptr -
          (g_runtime_code + RUNTIME_REG_OFFSET_RRX_LOAD_BLOCK_OFFSET));

  g_runtime_code_bytes = add_code_bytes + load_code_bytes + store_code_bytes +
    branch_code_bytes + branch_target_code_bytes + unsupported_code_bytes +
    bx_code_bytes + bx_target_code_bytes + patch_branch_code_bytes +
    patch_branch_target_code_bytes + bl_code_bytes + bl_target_code_bytes +
    swi_code_bytes + swi_target_code_bytes + cond_code_bytes +
    pc_write_movs_code_bytes + swp_code_bytes + multiply_code_bytes +
    multiply_long_code_bytes + multiply_long_flag_umulls_code_bytes +
    multiply_long_flag_smulls_code_bytes + multiply_long_acc_code_bytes +
    multiply_long_acc_flag_umlals_code_bytes +
    multiply_long_acc_flag_smlals_code_bytes +
    flag_adds_code_bytes + flag_cmp_code_bytes +
    psr_code_bytes + msr_cpsr_flags_code_bytes + msr_spsr_code_bytes +
    half_load_code_bytes + half_store_code_bytes +
    half_reg_load_code_bytes + half_reg_store_code_bytes +
    block_mem_stm_code_bytes + block_mem_ldm_code_bytes +
    block_mem_push_code_bytes + block_mem_ldm_pc_code_bytes +
    block_mem_ldm_pc_s_code_bytes +
    hle_div_code_bytes + hle_divarm_code_bytes +
    pc_source_code_bytes + writeback_store_code_bytes +
    writeback_load_code_bytes + reg_offset_load_code_bytes +
    shifted_reg_offset_code_bytes + reg_offset_rrx_load_code_bytes;
  flush_ret = syscall3(SYS_RISCV_FLUSH_ICACHE, (long)g_runtime_code,
                       (long)(g_runtime_code +
                              RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_BLOCK_OFFSET +
                              multiply_long_acc_flag_smlals_code_bytes), 0);
  if (flush_ret != 0)
  {
    *reason = "runtime_icache_flush_failed";
    clear_runtime_fixture_entries();
    return 0;
  }

  return 1;
}

static int ensure_runtime_fixture(const char **reason)
{
  if (g_runtime_entry && g_runtime_load_entry && g_runtime_store_entry &&
      g_runtime_branch_entry && g_runtime_branch_target_entry &&
      g_runtime_unsupported_entry && g_runtime_bx_entry &&
      g_runtime_bx_target_entry && g_runtime_patch_branch_entry &&
      g_runtime_patch_branch_target_entry && g_runtime_bl_entry &&
      g_runtime_bl_target_entry && g_runtime_swi_entry &&
      g_runtime_swi_target_entry && g_runtime_cond_entry &&
      g_runtime_pc_write_movs_entry && g_runtime_swp_entry &&
      g_runtime_multiply_entry && g_runtime_multiply_long_entry &&
      g_runtime_multiply_long_flag_umulls_entry &&
      g_runtime_multiply_long_flag_smulls_entry &&
      g_runtime_multiply_long_acc_entry &&
      g_runtime_multiply_long_acc_flag_umlals_entry &&
      g_runtime_multiply_long_acc_flag_smlals_entry &&
      g_runtime_flag_adds_entry && g_runtime_flag_cmp_entry &&
      g_runtime_psr_entry &&
      g_runtime_msr_cpsr_flags_entry && g_runtime_msr_spsr_entry &&
      g_runtime_half_load_entry && g_runtime_half_store_entry &&
      g_runtime_half_reg_load_entry && g_runtime_half_reg_store_entry &&
      g_runtime_block_mem_stm_entry && g_runtime_block_mem_ldm_entry &&
      g_runtime_block_mem_push_entry &&
      g_runtime_block_mem_ldm_pc_entry &&
      g_runtime_block_mem_ldm_pc_s_entry &&
      g_runtime_hle_div_entry && g_runtime_hle_divarm_entry &&
      g_runtime_pc_source_entry && g_runtime_writeback_store_entry &&
      g_runtime_writeback_load_entry && g_runtime_reg_offset_load_entry &&
      g_runtime_shifted_reg_offset_entry &&
      g_runtime_reg_offset_rrx_load_entry)
    return 1;

  g_runtime_code = (u8 *)map_runtime_exec_page();
  if (!g_runtime_code)
  {
    *reason = "runtime_mmap_failed";
    return 0;
  }

  init_emitter(false);
  return build_runtime_fixture_block(reason);
}

static u32 runtime_initial_r0(const struct harness_state *state)
{
  return state->loaded_hash ^ 0x12345678u;
}

static u32 runtime_initial_r1(const struct harness_state *state)
{
  return state->cycles ^ 0x01020304u;
}

static u32 runtime_multiply_r1_value(const struct harness_state *state)
{
  return (state->loaded_hash & 0x0000ffffu) | 0x00001000u;
}

static u32 runtime_multiply_r2_value(const struct harness_state *state)
{
  return ((state->cycles << 1) & 0x0000ffffu) | 0x00010001u;
}

static u32 runtime_multiply_r3_value(const struct harness_state *state)
{
  return state->loaded_hash ^ state->cycles ^ 0x80000000u;
}

static u32 runtime_load_word_value(const struct harness_state *state)
{
  return state->loaded_hash ^ 0xa1b2c3d4u;
}

static u32 runtime_load_byte_value(const struct harness_state *state)
{
  return (state->cycles ^ state->loaded_hash ^ 0x7eu) & 0xffu;
}

static u32 runtime_store_word_value(const struct harness_state *state)
{
  return state->loaded_hash + 0x13579bdfu;
}

static u32 runtime_swp_old_word_value(const struct harness_state *state)
{
  return state->loaded_hash ^ state->cycles ^ 0x5a6b7c8du;
}

static u32 runtime_branch_r1_value(const struct harness_state *state)
{
  return state->loaded_hash ^ 0x00000120u;
}

static u32 runtime_branch_r2_value(const struct harness_state *state)
{
  return state->cycles ^ 0x00000034u;
}

static u32 runtime_update_reg_hash(u32 hash, const u32 *values)
{
  u32 i;

  for (i = 0; i < 16; i++)
    hash = fnv1a_update_u32(hash, values[i]);
  hash = fnv1a_update_u32(hash, values[REG_CPSR]);
  hash = fnv1a_update_u32(hash, values[CPU_MODE]);
  hash = fnv1a_update_u32(hash, values[CPU_HALT_STATE]);
  hash = fnv1a_update_u32(hash, values[REG_BUS_VALUE]);
  return hash;
}

static u32 runtime_update_supervisor_state_hash(u32 hash,
                                                u32 supervisor_lr,
                                                u32 supervisor_spsr)
{
  hash = fnv1a_update_u32(hash, supervisor_lr);
  hash = fnv1a_update_u32(hash, supervisor_spsr);
  return hash;
}

static u32 runtime_current_sticky_hash(void)
{
  u32 i;
  u32 hash = 2166136261u;

  for (i = 0; i < (1024 / 32); i++)
    hash = fnv1a_update_u32(hash, gamepak_sticky_bit[i]);

  return hash;
}

static u32 runtime_reference_sticky_hash(void)
{
  u32 i;
  u32 hash = 2166136261u;

  for (i = 0; i < (1024 / 32); i++)
    hash = fnv1a_update_u32(hash, 0);

  return hash;
}

static u32 runtime_update_memory_hash(u32 hash,
                                      u32 read32_calls,
                                      u32 read32_addr,
                                      u32 read32_pc,
                                      u32 read32_value,
                                      u32 read8_calls,
                                      u32 read8_addr,
                                      u32 read8_pc,
                                      u32 read8_value,
                                      u32 write32_calls,
                                      u32 write32_addr,
                                      u32 write32_pc,
                                      u32 write32_value,
                                      u32 sticky_hash)
{
  hash = fnv1a_update_u32(hash, read32_calls);
  hash = fnv1a_update_u32(hash, read32_addr);
  hash = fnv1a_update_u32(hash, read32_pc);
  hash = fnv1a_update_u32(hash, read32_value);
  hash = fnv1a_update_u32(hash, read8_calls);
  hash = fnv1a_update_u32(hash, read8_addr);
  hash = fnv1a_update_u32(hash, read8_pc);
  hash = fnv1a_update_u32(hash, read8_value);
  hash = fnv1a_update_u32(hash, write32_calls);
  hash = fnv1a_update_u32(hash, write32_addr);
  hash = fnv1a_update_u32(hash, write32_pc);
  hash = fnv1a_update_u32(hash, write32_value);
  hash = fnv1a_update_u32(hash, sticky_hash);
  return hash;
}

static u32 runtime_update_half_memory_hash(u32 hash,
                                           u32 read16_calls,
                                           u32 read16_addr,
                                           u32 read16_pc,
                                           u32 read16_value,
                                           u32 read8s_calls,
                                           u32 read8s_addr,
                                           u32 read8s_pc,
                                           u32 read8s_value,
                                           u32 read16s_calls,
                                           u32 read16s_addr,
                                           u32 read16s_pc,
                                           u32 read16s_value,
                                           u32 write16_calls,
                                           u32 write16_addr,
                                           u32 write16_pc,
                                           u32 write16_value)
{
  hash = fnv1a_update_u32(hash, read16_calls);
  hash = fnv1a_update_u32(hash, read16_addr);
  hash = fnv1a_update_u32(hash, read16_pc);
  hash = fnv1a_update_u32(hash, read16_value);
  hash = fnv1a_update_u32(hash, read8s_calls);
  hash = fnv1a_update_u32(hash, read8s_addr);
  hash = fnv1a_update_u32(hash, read8s_pc);
  hash = fnv1a_update_u32(hash, read8s_value);
  hash = fnv1a_update_u32(hash, read16s_calls);
  hash = fnv1a_update_u32(hash, read16s_addr);
  hash = fnv1a_update_u32(hash, read16s_pc);
  hash = fnv1a_update_u32(hash, read16s_value);
  hash = fnv1a_update_u32(hash, write16_calls);
  hash = fnv1a_update_u32(hash, write16_addr);
  hash = fnv1a_update_u32(hash, write16_pc);
  hash = fnv1a_update_u32(hash, write16_value);
  return hash;
}

static u32 runtime_update_block_mem32_event_hash(u32 hash,
                                                 u32 tag,
                                                 u32 address,
                                                 u32 pc,
                                                 u32 value)
{
  hash = fnv1a_update_u32(hash, tag);
  hash = fnv1a_update_u32(hash, address);
  hash = fnv1a_update_u32(hash, pc);
  hash = fnv1a_update_u32(hash, value);
  return hash;
}

static u32 runtime_append_block_mem32_hash(u32 hash, u32 block_hash)
{
  return fnv1a_update_u32(hash, block_hash);
}

static u32 runtime_update_scheduler_hash(u32 hash,
                                         u32 lookup_calls,
                                         u32 lookup_pc,
                                         u32 thumb_lookup_calls,
                                         u32 update_calls,
                                         s32 update_cycles,
                                         u32 execute_calls,
                                         u32 execute_cycles,
                                         u32 execute_pc,
                                         u32 flush_calls,
                                         u32 irq_check_calls)
{
  hash = fnv1a_update_u32(hash, lookup_calls);
  hash = fnv1a_update_u32(hash, lookup_pc);
  hash = fnv1a_update_u32(hash, thumb_lookup_calls);
  hash = fnv1a_update_u32(hash, update_calls);
  hash = fnv1a_update_u32(hash, (u32)update_cycles);
  hash = fnv1a_update_u32(hash, execute_calls);
  hash = fnv1a_update_u32(hash, execute_cycles);
  hash = fnv1a_update_u32(hash, execute_pc);
  hash = fnv1a_update_u32(hash, flush_calls);
  hash = fnv1a_update_u32(hash, irq_check_calls);
  return hash;
}

static u32 runtime_update_current_memory_hash(u32 hash)
{
  return runtime_update_memory_hash(hash,
                                    g_runtime_read32_calls,
                                    g_runtime_read32_addr,
                                    g_runtime_read32_pc,
                                    g_runtime_read32_value,
                                    g_runtime_read8_calls,
                                    g_runtime_read8_addr,
                                    g_runtime_read8_pc,
                                    g_runtime_read8_value,
                                    g_runtime_write32_calls,
                                    g_runtime_write32_addr,
                                    g_runtime_write32_pc,
                                    g_runtime_write32_value,
                                    runtime_current_sticky_hash());
}

static u32 runtime_update_current_half_memory_hash(u32 hash)
{
  return runtime_update_half_memory_hash(hash,
                                         g_runtime_read16_calls,
                                         g_runtime_read16_addr,
                                         g_runtime_read16_pc,
                                         g_runtime_read16_value,
                                         g_runtime_read8s_calls,
                                         g_runtime_read8s_addr,
                                         g_runtime_read8s_pc,
                                         g_runtime_read8s_value,
                                         g_runtime_read16s_calls,
                                         g_runtime_read16s_addr,
                                         g_runtime_read16s_pc,
                                         g_runtime_read16s_value,
                                         g_runtime_write16_calls,
                                         g_runtime_write16_addr,
                                         g_runtime_write16_pc,
                                         g_runtime_write16_value);
}

static u32 runtime_update_current_block_mem32_hash(u32 hash)
{
  return runtime_append_block_mem32_hash(hash, g_runtime_block_mem32_hash);
}

static u32 runtime_update_current_scheduler_hash(u32 hash)
{
  return runtime_update_scheduler_hash(hash,
                                       g_runtime_lookup_calls,
                                       g_runtime_lookup_pc,
                                       g_runtime_thumb_lookup_calls,
                                       g_runtime_update_calls,
                                       g_runtime_update_cycles,
                                       g_runtime_execute_calls,
                                       g_runtime_execute_cycles,
                                       g_runtime_execute_pc,
                                       g_runtime_flush_calls,
                                       g_runtime_irq_check_calls);
}

static u32 runtime_fixture_frame_hash(const struct harness_state *state)
{
  struct harness_state saved_state = g_state;
  u32 hash;

  g_state = *state;
  render_frame();
  hash = g_state.last_frame_hash;
  g_state = saved_state;
  return hash;
}

static void run_runtime_reference_workload(const struct harness_state *base,
                                           struct compare_snapshot *snapshot)
{
  u32 values[REG_MAX];
  unsigned i;
  u32 reg_hash = 2166136261u;
  u32 mem_hash = 2166136261u;
  u32 scheduler_hash = 2166136261u;
  u32 multiply_r1 = runtime_multiply_r1_value(base);
  u32 multiply_r2 = runtime_multiply_r2_value(base);
  u32 multiply_r3 = runtime_multiply_r3_value(base);
  u32 multiply_product = multiply_r1 * multiply_r2;
  u32 load_word = runtime_load_word_value(base);
  u32 load_byte = runtime_load_byte_value(base);
  u32 store_word = runtime_store_word_value(base);
  u32 swp_old_word = runtime_swp_old_word_value(base);
  u32 branch_r1 = runtime_branch_r1_value(base);
  u32 branch_r2 = runtime_branch_r2_value(base);
  u32 block_hash;

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[0] = runtime_initial_r0(base);
  values[1] = runtime_initial_r1(base);
  values[2] = values[0] + values[1];
  values[REG_PC] = RUNTIME_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1, RUNTIME_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = multiply_r1;
  values[2] = multiply_r2;
  values[3] = multiply_r3;
  values[4] = multiply_product;
  values[5] = multiply_product + multiply_r3;
  values[REG_PC] = RUNTIME_MULTIPLY_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_MULTIPLY_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = RUNTIME_MULTIPLY_LONG_UMULL_R1_VALUE;
  values[2] = RUNTIME_MULTIPLY_LONG_UMULL_R2_VALUE;
  values[3] = RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE;
  values[4] = RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE;
  values[8] = RUNTIME_MULTIPLY_LONG_UMULL_LO_VALUE;
  values[9] = RUNTIME_MULTIPLY_LONG_UMULL_HI_VALUE;
  values[10] = RUNTIME_MULTIPLY_LONG_SMULL_LO_VALUE;
  values[11] = RUNTIME_MULTIPLY_LONG_SMULL_HI_VALUE;
  values[REG_PC] = RUNTIME_MULTIPLY_LONG_END_PC;
  values[REG_CPSR] = RUNTIME_CPSR_CV_LOW_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_MULTIPLY_LONG_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[2] = RUNTIME_MULTIPLY_LONG_FLAG_R2_VALUE;
  values[REG_PC] = RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_END_PC;
  values[REG_CPSR] = RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_CPSR_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE;
  values[4] = RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE;
  values[10] = RUNTIME_MULTIPLY_LONG_SMULL_LO_VALUE;
  values[11] = RUNTIME_MULTIPLY_LONG_SMULL_HI_VALUE;
  values[REG_PC] = RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_END_PC;
  values[REG_CPSR] = RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_CPSR_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = RUNTIME_MULTIPLY_LONG_UMULL_R1_VALUE;
  values[2] = RUNTIME_MULTIPLY_LONG_UMULL_R2_VALUE;
  values[3] = RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE;
  values[4] = RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE;
  values[8] = RUNTIME_MULTIPLY_LONG_UMLAL_LO_VALUE;
  values[9] = RUNTIME_MULTIPLY_LONG_UMLAL_HI_VALUE;
  values[10] = RUNTIME_MULTIPLY_LONG_SMLAL_LO_VALUE;
  values[11] = RUNTIME_MULTIPLY_LONG_SMLAL_HI_VALUE;
  values[REG_PC] = RUNTIME_MULTIPLY_LONG_ACC_END_PC;
  values[REG_CPSR] = RUNTIME_CPSR_CV_LOW_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_MULTIPLY_LONG_ACC_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = RUNTIME_MULTIPLY_LONG_UMULL_R1_VALUE;
  values[2] = RUNTIME_MULTIPLY_LONG_UMULL_R2_VALUE;
  values[REG_PC] = RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_END_PC;
  values[REG_CPSR] =
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_CPSR_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE;
  values[4] = RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE;
  values[10] = RUNTIME_MULTIPLY_LONG_SMULL_LO_VALUE;
  values[11] = RUNTIME_MULTIPLY_LONG_SMULL_HI_VALUE;
  values[REG_PC] = RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_END_PC;
  values[REG_CPSR] =
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_CPSR_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = RUNTIME_FLAG_ADDS_R1_VALUE;
  values[7] = RUNTIME_FLAG_ADDS_R7_VALUE;
  values[REG_PC] = RUNTIME_FLAG_ADDS_END_PC;
  values[REG_CPSR] = RUNTIME_FLAG_ADDS_CPSR_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_FLAG_ADDS_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[0] = RUNTIME_FLAG_CMP_R0_VALUE;
  values[REG_PC] = RUNTIME_FLAG_CMP_END_PC;
  values[REG_CPSR] = RUNTIME_FLAG_CMP_CPSR_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_FLAG_CMP_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[6] = RUNTIME_PSR_CPSR_VALUE;
  values[7] = RUNTIME_PSR_SPSR_VALUE;
  values[REG_PC] = RUNTIME_PSR_END_PC;
  values[REG_CPSR] = RUNTIME_PSR_CPSR_VALUE;
  values[CPU_MODE] = RUNTIME_PSR_CPU_MODE_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash, 0, RUNTIME_PSR_SPSR_VALUE);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_PSR_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[REG_PC] = RUNTIME_MSR_CPSR_FLAGS_END_PC;
  values[REG_CPSR] = RUNTIME_MSR_CPSR_FLAGS_EXPECTED;
  values[CPU_MODE] = MODE_SUPERVISOR;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1,
    RUNTIME_MSR_CPSR_FLAGS_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = RUNTIME_MSR_SPSR_SOURCE;
  values[REG_PC] = RUNTIME_MSR_SPSR_END_PC;
  values[REG_CPSR] = RUNTIME_PSR_CPSR_VALUE;
  values[CPU_MODE] = MODE_SUPERVISOR;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash, 0, RUNTIME_MSR_SPSR_EXPECTED);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_MSR_SPSR_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_LOAD_BASE_ADDR;
  values[4] = load_word;
  values[5] = load_byte;
  values[REG_PC] = RUNTIME_LOAD_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        1, RUNTIME_LOAD_WORD_ADDR,
                                        RUNTIME_LOAD_WORD_PC, load_word,
                                        1, RUNTIME_LOAD_BYTE_ADDR,
                                        RUNTIME_LOAD_BYTE_PC, load_byte,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1, RUNTIME_LOAD_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_STORE_BASE_ADDR;
  values[6] = store_word;
  values[REG_PC] = RUNTIME_STORE_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        1, RUNTIME_STORE_WORD_ADDR,
                                        RUNTIME_STORE_END_PC, store_word,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1, RUNTIME_STORE_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_STORE_BASE_ADDR;
  values[6] = store_word;
  values[REG_PC] = RUNTIME_STORE_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        1, RUNTIME_STORE_WORD_ADDR,
                                        RUNTIME_STORE_END_PC, store_word,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1, RUNTIME_STORE_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 1, 1);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_HALF_BASE_ADDR;
  values[4] = RUNTIME_HALF_U16_VALUE;
  values[5] = RUNTIME_HALF_S8_VALUE;
  values[6] = RUNTIME_HALF_S16_VALUE;
  values[REG_PC] = RUNTIME_HALF_LOAD_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  mem_hash = runtime_update_half_memory_hash(
    mem_hash,
    1, RUNTIME_HALF_U16_ADDR,
    RUNTIME_HALF_LDRH_PC, RUNTIME_HALF_U16_VALUE,
    1, RUNTIME_HALF_S8_ADDR,
    RUNTIME_HALF_LDRSB_PC, RUNTIME_HALF_S8_VALUE,
    1, RUNTIME_HALF_S16_ADDR,
    RUNTIME_HALF_LDRSH_PC, RUNTIME_HALF_S16_VALUE,
    0, 0, 0, 0);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_HALF_LOAD_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_HALF_BASE_ADDR;
  values[7] = RUNTIME_HALF_STORE_VALUE;
  values[REG_PC] = RUNTIME_HALF_STORE_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  mem_hash = runtime_update_half_memory_hash(
    mem_hash,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    1, RUNTIME_HALF_STORE_ADDR,
    RUNTIME_HALF_STORE_END_PC, RUNTIME_HALF_STORE_U16_VALUE);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_HALF_STORE_START_PC, 0,
    1, 0,
    0, 0, 0,
    1, 1);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[2] = RUNTIME_HALF_REG_OFFSET_VALUE;
  values[3] = RUNTIME_HALF_BASE_ADDR;
  values[8] = RUNTIME_HALF_U16_VALUE;
  values[9] = RUNTIME_HALF_S8_VALUE;
  values[10] = RUNTIME_HALF_S16_VALUE;
  values[REG_PC] = RUNTIME_HALF_REG_LOAD_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  mem_hash = runtime_update_half_memory_hash(
    mem_hash,
    1, RUNTIME_HALF_REG_U16_ADDR,
    RUNTIME_HALF_REG_LDRH_PC, RUNTIME_HALF_U16_VALUE,
    1, RUNTIME_HALF_REG_S8_ADDR,
    RUNTIME_HALF_REG_LDRSB_PC, RUNTIME_HALF_S8_VALUE,
    1, RUNTIME_HALF_REG_S16_ADDR,
    RUNTIME_HALF_REG_LDRSH_PC, RUNTIME_HALF_S16_VALUE,
    0, 0, 0, 0);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_HALF_REG_LOAD_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[2] = RUNTIME_HALF_REG_OFFSET_VALUE;
  values[3] = RUNTIME_HALF_BASE_ADDR;
  values[9] = RUNTIME_HALF_STORE_VALUE;
  values[REG_PC] = RUNTIME_HALF_REG_STORE_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  mem_hash = runtime_update_half_memory_hash(
    mem_hash,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    1, RUNTIME_HALF_REG_STORE_ADDR,
    RUNTIME_HALF_REG_STORE_END_PC, RUNTIME_HALF_STORE_U16_VALUE);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_HALF_REG_STORE_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[0] = RUNTIME_BLOCK_MEM_STM_R0_VALUE;
  values[2] = RUNTIME_BLOCK_MEM_STM_R2_VALUE;
  values[3] = RUNTIME_BLOCK_MEM_BASE +
    (RUNTIME_BLOCK_MEM_XFER_COUNT * 4u);
  values[5] = RUNTIME_BLOCK_MEM_STM_R5_VALUE;
  values[REG_PC] = RUNTIME_BLOCK_MEM_STM_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    0, 0, 0, 0,
    0, 0, 0, 0,
    RUNTIME_BLOCK_MEM_XFER_COUNT,
    RUNTIME_BLOCK_MEM_BASE + 8u,
    RUNTIME_BLOCK_MEM_STM_END_PC,
    RUNTIME_BLOCK_MEM_STM_R5_VALUE,
    runtime_reference_sticky_hash());
  block_hash = 2166136261u;
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_WRITE32_TAG,
    RUNTIME_BLOCK_MEM_BASE, RUNTIME_BLOCK_MEM_STM_END_PC,
    RUNTIME_BLOCK_MEM_STM_R0_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_WRITE32_TAG,
    RUNTIME_BLOCK_MEM_BASE + 4u, RUNTIME_BLOCK_MEM_STM_END_PC,
    RUNTIME_BLOCK_MEM_STM_R2_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_WRITE32_TAG,
    RUNTIME_BLOCK_MEM_BASE + 8u, RUNTIME_BLOCK_MEM_STM_END_PC,
    RUNTIME_BLOCK_MEM_STM_R5_VALUE);
  mem_hash = runtime_append_block_mem32_hash(mem_hash, block_hash);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_BLOCK_MEM_STM_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = RUNTIME_BLOCK_MEM_LDM_R1_VALUE;
  values[4] = RUNTIME_BLOCK_MEM_BASE +
    (RUNTIME_BLOCK_MEM_XFER_COUNT * 4u);
  values[6] = RUNTIME_BLOCK_MEM_LDM_R6_VALUE;
  values[7] = RUNTIME_BLOCK_MEM_LDM_R7_VALUE;
  values[REG_PC] = RUNTIME_BLOCK_MEM_LDM_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    RUNTIME_BLOCK_MEM_XFER_COUNT,
    RUNTIME_BLOCK_MEM_BASE + 8u,
    RUNTIME_BLOCK_MEM_LDM_END_PC,
    RUNTIME_BLOCK_MEM_LDM_R7_VALUE,
    0, 0, 0, 0,
    0, 0, 0, 0,
    runtime_reference_sticky_hash());
  block_hash = 2166136261u;
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_BASE, RUNTIME_BLOCK_MEM_LDM_END_PC,
    RUNTIME_BLOCK_MEM_LDM_R1_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_BASE + 4u, RUNTIME_BLOCK_MEM_LDM_END_PC,
    RUNTIME_BLOCK_MEM_LDM_R6_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_BASE + 8u, RUNTIME_BLOCK_MEM_LDM_END_PC,
    RUNTIME_BLOCK_MEM_LDM_R7_VALUE);
  mem_hash = runtime_append_block_mem32_hash(mem_hash, block_hash);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_BLOCK_MEM_LDM_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[0] = RUNTIME_BLOCK_MEM_PUSH_R0_VALUE;
  values[1] = RUNTIME_BLOCK_MEM_PUSH_R1_VALUE;
  values[REG_SP] = RUNTIME_BLOCK_MEM_PUSH_ADDR;
  values[REG_LR] = RUNTIME_BLOCK_MEM_PUSH_LR_VALUE;
  values[REG_PC] = RUNTIME_BLOCK_MEM_PUSH_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    0, 0, 0, 0,
    0, 0, 0, 0,
    RUNTIME_BLOCK_MEM_XFER_COUNT,
    RUNTIME_BLOCK_MEM_PUSH_ADDR + 8u,
    RUNTIME_BLOCK_MEM_PUSH_END_PC,
    RUNTIME_BLOCK_MEM_PUSH_LR_VALUE,
    runtime_reference_sticky_hash());
  block_hash = 2166136261u;
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_WRITE32_TAG,
    RUNTIME_BLOCK_MEM_PUSH_ADDR, RUNTIME_BLOCK_MEM_PUSH_END_PC,
    RUNTIME_BLOCK_MEM_PUSH_R0_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_WRITE32_TAG,
    RUNTIME_BLOCK_MEM_PUSH_ADDR + 4u, RUNTIME_BLOCK_MEM_PUSH_END_PC,
    RUNTIME_BLOCK_MEM_PUSH_R1_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_WRITE32_TAG,
    RUNTIME_BLOCK_MEM_PUSH_ADDR + 8u, RUNTIME_BLOCK_MEM_PUSH_END_PC,
    RUNTIME_BLOCK_MEM_PUSH_LR_VALUE);
  mem_hash = runtime_append_block_mem32_hash(mem_hash, block_hash);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_BLOCK_MEM_PUSH_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = RUNTIME_BLOCK_MEM_LDM_PC_R1_VALUE;
  values[2] = branch_r2;
  values[3] = branch_r2 + RUNTIME_BLOCK_MEM_LDM_PC_R1_VALUE;
  values[4] = RUNTIME_BLOCK_MEM_PC_BASE +
    (RUNTIME_BLOCK_MEM_XFER_COUNT * 4u);
  values[6] = RUNTIME_BLOCK_MEM_LDM_PC_R6_VALUE;
  values[REG_PC] = RUNTIME_BRANCH_TARGET_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    RUNTIME_BLOCK_MEM_XFER_COUNT,
    RUNTIME_BLOCK_MEM_PC_BASE + 8u,
    RUNTIME_BLOCK_MEM_LDM_PC_END_PC,
    RUNTIME_BRANCH_TARGET_PC,
    0, 0, 0, 0,
    0, 0, 0, 0,
    runtime_reference_sticky_hash());
  block_hash = 2166136261u;
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_PC_BASE, RUNTIME_BLOCK_MEM_LDM_PC_END_PC,
    RUNTIME_BLOCK_MEM_LDM_PC_R1_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_PC_BASE + 4u, RUNTIME_BLOCK_MEM_LDM_PC_END_PC,
    RUNTIME_BLOCK_MEM_LDM_PC_R6_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_PC_BASE + 8u, RUNTIME_BLOCK_MEM_LDM_PC_END_PC,
    RUNTIME_BRANCH_TARGET_PC);
  mem_hash = runtime_append_block_mem32_hash(mem_hash, block_hash);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    2, RUNTIME_BRANCH_TARGET_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = RUNTIME_BLOCK_MEM_LDM_PC_R1_VALUE;
  values[4] = RUNTIME_BLOCK_MEM_PC_BASE +
    (RUNTIME_BLOCK_MEM_XFER_COUNT * 4u);
  values[6] = RUNTIME_BLOCK_MEM_LDM_PC_R6_VALUE;
  values[REG_PC] = RUNTIME_BRANCH_TARGET_PC;
  values[REG_CPSR] = RUNTIME_PC_WRITE_MOVS_SPSR_VALUE;
  values[CPU_MODE] = MODE_SUPERVISOR;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash, 0, RUNTIME_PC_WRITE_MOVS_SPSR_VALUE);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    RUNTIME_BLOCK_MEM_XFER_COUNT,
    RUNTIME_BLOCK_MEM_PC_BASE + 8u,
    RUNTIME_BLOCK_MEM_LDM_PC_S_END_PC,
    RUNTIME_BRANCH_TARGET_PC,
    0, 0, 0, 0,
    0, 0, 0, 0,
    runtime_reference_sticky_hash());
  block_hash = 2166136261u;
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_PC_BASE, RUNTIME_BLOCK_MEM_LDM_PC_S_END_PC,
    RUNTIME_BLOCK_MEM_LDM_PC_R1_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_PC_BASE + 4u, RUNTIME_BLOCK_MEM_LDM_PC_S_END_PC,
    RUNTIME_BLOCK_MEM_LDM_PC_R6_VALUE);
  block_hash = runtime_update_block_mem32_event_hash(
    block_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    RUNTIME_BLOCK_MEM_PC_BASE + 8u, RUNTIME_BLOCK_MEM_LDM_PC_S_END_PC,
    RUNTIME_BRANCH_TARGET_PC);
  mem_hash = runtime_append_block_mem32_hash(mem_hash, block_hash);
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_BLOCK_MEM_LDM_PC_S_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 1);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[0] = RUNTIME_HLE_DIV_QUOTIENT;
  values[1] = RUNTIME_HLE_DIV_REMAINDER;
  values[3] = RUNTIME_HLE_DIV_ABS_QUOTIENT;
  values[REG_PC] = RUNTIME_HLE_DIV_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_HLE_DIV_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[0] = RUNTIME_HLE_DIVARM_QUOTIENT;
  values[1] = RUNTIME_HLE_DIVARM_REMAINDER;
  values[3] = RUNTIME_HLE_DIVARM_ABS_QUOTIENT;
  values[REG_PC] = RUNTIME_HLE_DIVARM_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_HLE_DIVARM_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[0] = RUNTIME_PC_SOURCE_R0_VALUE;
  values[1] = RUNTIME_PC_SOURCE_R1_VALUE;
  values[2] = RUNTIME_PC_SOURCE_R2_VALUE;
  values[6] = RUNTIME_PC_SOURCE_R6_VALUE;
  values[7] = RUNTIME_PC_SOURCE_R7_VALUE;
  values[8] = RUNTIME_PC_SOURCE_R8_VALUE;
  values[9] = RUNTIME_PC_SOURCE_R9_VALUE;
  values[10] = RUNTIME_PC_SOURCE_R10_VALUE;
  values[REG_PC] = RUNTIME_PC_SOURCE_END_PC;
  values[REG_CPSR] = RUNTIME_PC_SOURCE_CPSR_VALUE;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_PC_SOURCE_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_WRITEBACK_STORE_ADDR;
  values[REG_PC] = RUNTIME_WRITEBACK_STORE_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    0, 0, 0, 0,
    0, 0, 0, 0,
    1, RUNTIME_WRITEBACK_STORE_ADDR,
    RUNTIME_WRITEBACK_STORE_END_PC,
    RUNTIME_WRITEBACK_BASE_ADDR,
    runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_WRITEBACK_STORE_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[4] = RUNTIME_WRITEBACK_POST_LOAD_R4;
  values[5] = RUNTIME_WRITEBACK_POST_LOAD_VALUE;
  values[REG_PC] = RUNTIME_WRITEBACK_LOAD_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    0, 0, 0, 0,
    1, RUNTIME_WRITEBACK_POST_LOAD_ADDR,
    RUNTIME_WRITEBACK_LOAD_START_PC,
    RUNTIME_WRITEBACK_POST_LOAD_VALUE,
    0, 0, 0, 0,
    runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_WRITEBACK_LOAD_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[2] = RUNTIME_REG_OFFSET_VALUE;
  values[3] = RUNTIME_REG_OFFSET_BASE_ADDR;
  values[8] = RUNTIME_REG_OFFSET_WORD_VALUE;
  values[9] = RUNTIME_REG_OFFSET_BYTE_VALUE;
  values[REG_PC] = RUNTIME_REG_OFFSET_LOAD_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    1, RUNTIME_REG_OFFSET_WORD_ADDR,
    RUNTIME_REG_OFFSET_LDR_PC,
    RUNTIME_REG_OFFSET_WORD_VALUE,
    1, RUNTIME_REG_OFFSET_BYTE_ADDR,
    RUNTIME_REG_OFFSET_LDRB_PC,
    RUNTIME_REG_OFFSET_BYTE_VALUE,
    0, 0, 0, 0,
    runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_REG_OFFSET_LOAD_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[2] = RUNTIME_REG_OFFSET_VALUE;
  values[3] = RUNTIME_REG_OFFSET_BASE_ADDR;
  values[10] = RUNTIME_SHIFTED_REG_OFFSET_BYTE_VALUE;
  values[REG_PC] = RUNTIME_SHIFTED_REG_OFFSET_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    0, 0, 0, 0,
    1, RUNTIME_SHIFTED_REG_OFFSET_BYTE_ADDR,
    RUNTIME_SHIFTED_REG_OFFSET_START_PC,
    RUNTIME_SHIFTED_REG_OFFSET_BYTE_VALUE,
    0, 0, 0, 0,
    runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_SHIFTED_REG_OFFSET_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[2] = RUNTIME_REG_OFFSET_RRX_VALUE;
  values[3] = RUNTIME_REG_OFFSET_BASE_ADDR;
  values[11] = RUNTIME_REG_OFFSET_RRX_WORD_VALUE;
  values[REG_PC] = RUNTIME_REG_OFFSET_RRX_LOAD_END_PC;
  values[REG_CPSR] = RUNTIME_CPSR_C_BIT;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(
    mem_hash,
    1, RUNTIME_REG_OFFSET_RRX_WORD_ADDR,
    RUNTIME_REG_OFFSET_RRX_LOAD_START_PC,
    RUNTIME_REG_OFFSET_RRX_WORD_VALUE,
    0, 0, 0, 0,
    0, 0, 0, 0,
    runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1, RUNTIME_REG_OFFSET_RRX_LOAD_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[3] = RUNTIME_SWP_ADDR;
  values[4] = swp_old_word;
  values[5] = store_word;
  values[REG_PC] = RUNTIME_SWP_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        1, RUNTIME_SWP_ADDR,
                                        RUNTIME_SWP_START_PC, swp_old_word,
                                        0, 0, 0, 0,
                                        1, RUNTIME_SWP_ADDR,
                                        RUNTIME_SWP_END_PC, store_word,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1, RUNTIME_SWP_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = branch_r1;
  values[2] = branch_r2;
  values[3] = branch_r1 + branch_r2;
  values[REG_PC] = RUNTIME_BRANCH_TARGET_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 2,
                                                 RUNTIME_BRANCH_TARGET_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[REG_PC] = RUNTIME_BRANCH_TARGET_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_BRANCH_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = branch_r1;
  values[2] = branch_r2;
  values[4] = branch_r1 + branch_r2;
  values[7] = RUNTIME_BX_TARGET_PC;
  values[REG_PC] = RUNTIME_BX_TARGET_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 2,
                                                 RUNTIME_BX_TARGET_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = branch_r1;
  values[2] = branch_r2;
  values[5] = branch_r1 + branch_r2;
  values[REG_PC] = RUNTIME_PATCH_BRANCH_TARGET_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_PATCH_BRANCH_START_PC,
                                                 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = branch_r1;
  values[2] = branch_r2;
  values[8] = branch_r1 + branch_r2;
  values[REG_LR] = RUNTIME_BL_LINK_PC;
  values[REG_PC] = RUNTIME_BL_TARGET_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 2,
                                                 RUNTIME_BL_TARGET_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = branch_r1;
  values[2] = branch_r2;
  values[9] = branch_r1 + branch_r2;
  values[REG_LR] = RUNTIME_SWI_LINK_PC;
  values[REG_PC] = RUNTIME_SWI_TARGET_END_PC;
  values[REG_CPSR] = RUNTIME_SWI_CPSR_VALUE;
  values[CPU_MODE] = MODE_SUPERVISOR;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  values[REG_BUS_VALUE] = RUNTIME_SWI_BUS_VALUE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  reg_hash = runtime_update_supervisor_state_hash(reg_hash,
                                                  RUNTIME_SWI_LINK_PC,
                                                  RUNTIME_SWI_INITIAL_CPSR);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 2,
                                                 RUNTIME_SWI_TARGET_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = branch_r1;
  values[2] = branch_r2;
  values[10] = branch_r1 + branch_r2;
  values[REG_PC] = RUNTIME_COND_END_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_COND_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[1] = branch_r1;
  values[2] = branch_r2;
  values[REG_PC] = RUNTIME_COND_END_PC;
  values[REG_CPSR] = RUNTIME_CPSR_Z_BIT;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_COND_START_PC, 0,
                                                 1, 0,
                                                 0, 0, 0,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[REG_PC] = RUNTIME_PC_WRITE_MOVS_TARGET;
  values[REG_CPSR] = RUNTIME_PC_WRITE_MOVS_SPSR_VALUE;
  values[CPU_MODE] = MODE_SUPERVISOR;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash, 0, RUNTIME_PC_WRITE_MOVS_SPSR_VALUE);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(
    scheduler_hash,
    1,
    RUNTIME_PC_WRITE_MOVS_START_PC, 0,
    1, 0,
    0, 0, 0,
    0, 1);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[REG_PC] = RUNTIME_UNSUPPORTED_START_PC;
  values[REG_CPSR] = 0;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 1,
                                                 RUNTIME_UNSUPPORTED_START_PC,
                                                 0,
                                                 0, RUNTIME_NO_UPDATE_CYCLES,
                                                 1,
                                                 RUNTIME_UNSUPPORTED_CYCLES,
                                                 RUNTIME_UNSUPPORTED_START_PC,
                                                 0, 0);

  for (i = 0; i < REG_MAX; i++)
    values[i] = 0;
  values[REG_PC] = RUNTIME_THUMB_FALLBACK_START_PC;
  values[REG_CPSR] = RUNTIME_CPSR_T_BIT;
  values[CPU_HALT_STATE] = CPU_ACTIVE;
  reg_hash = runtime_update_reg_hash(reg_hash, values);
  mem_hash = runtime_update_memory_hash(mem_hash,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        runtime_reference_sticky_hash());
  scheduler_hash = runtime_update_scheduler_hash(scheduler_hash,
                                                 0, 0,
                                                 1,
                                                 0, RUNTIME_NO_UPDATE_CYCLES,
                                                 1,
                                                 RUNTIME_THUMB_FALLBACK_CYCLES,
                                                 RUNTIME_THUMB_FALLBACK_START_PC,
                                                 0, 0);

  snapshot->frame_hash = runtime_fixture_frame_hash(base);
  snapshot->reg_hash = reg_hash;
  snapshot->mem_hash = mem_hash;
  snapshot->scheduler_hash = scheduler_hash;
  snapshot->blocks = 49;
  snapshot->fallbacks = 2;
  snapshot->native_data_proc = 29;
  snapshot->native_branch = 5;
  snapshot->native_load = 16;
  snapshot->native_store = 7;
  snapshot->native_psr = 4;
}

static void run_runtime_rv32im_workload(const struct harness_state *base,
                                        struct compare_snapshot *snapshot)
{
  riscv_runtime_stats before;
  riscv_runtime_stats after;
  u32 reg_hash = 2166136261u;
  u32 mem_hash = 2166136261u;
  u32 scheduler_hash = 2166136261u;

  g_runtime_fixture_load_word = runtime_load_word_value(base);
  g_runtime_fixture_load_byte = runtime_load_byte_value(base);
  g_runtime_fixture_store_word = runtime_store_word_value(base);
  g_runtime_fixture_swp_old_word = runtime_swp_old_word_value(base);
  g_runtime_fixture_branch_r1 = runtime_branch_r1_value(base);
  g_runtime_fixture_branch_r2 = runtime_branch_r2_value(base);

  riscv_get_runtime_stats(&before);

  reset_runtime_fixture_state(RUNTIME_START_PC);
  reg[0] = runtime_initial_r0(base);
  reg[1] = runtime_initial_r1(base);
  execute_arm_translate_internal(RUNTIME_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_MULTIPLY_START_PC);
  reg[1] = runtime_multiply_r1_value(base);
  reg[2] = runtime_multiply_r2_value(base);
  reg[3] = runtime_multiply_r3_value(base);
  execute_arm_translate_internal(RUNTIME_MULTIPLY_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_MULTIPLY_LONG_START_PC);
  reg[1] = RUNTIME_MULTIPLY_LONG_UMULL_R1_VALUE;
  reg[2] = RUNTIME_MULTIPLY_LONG_UMULL_R2_VALUE;
  reg[3] = RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE;
  reg[4] = RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE;
  reg[REG_CPSR] = RUNTIME_CPSR_CV_LOW_VALUE;
  execute_arm_translate_internal(RUNTIME_MULTIPLY_LONG_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_START_PC);
  reg[1] = RUNTIME_MULTIPLY_LONG_FLAG_ZERO_R1_VALUE;
  reg[2] = RUNTIME_MULTIPLY_LONG_FLAG_R2_VALUE;
  reg[REG_CPSR] = RUNTIME_CPSR_CV_LOW_VALUE;
  execute_arm_translate_internal(RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_START_PC);
  reg[3] = RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE;
  reg[4] = RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE;
  reg[REG_CPSR] = RUNTIME_CPSR_CV_LOW_VALUE;
  execute_arm_translate_internal(RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_MULTIPLY_LONG_ACC_START_PC);
  reg[1] = RUNTIME_MULTIPLY_LONG_UMULL_R1_VALUE;
  reg[2] = RUNTIME_MULTIPLY_LONG_UMULL_R2_VALUE;
  reg[3] = RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE;
  reg[4] = RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE;
  reg[8] = RUNTIME_MULTIPLY_LONG_UMLAL_OLD_LO_VALUE;
  reg[9] = RUNTIME_MULTIPLY_LONG_UMLAL_OLD_HI_VALUE;
  reg[10] = RUNTIME_MULTIPLY_LONG_SMLAL_OLD_LO_VALUE;
  reg[11] = RUNTIME_MULTIPLY_LONG_SMLAL_OLD_HI_VALUE;
  reg[REG_CPSR] = RUNTIME_CPSR_CV_LOW_VALUE;
  execute_arm_translate_internal(RUNTIME_MULTIPLY_LONG_ACC_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_START_PC);
  reg[1] = RUNTIME_MULTIPLY_LONG_UMULL_R1_VALUE;
  reg[2] = RUNTIME_MULTIPLY_LONG_UMULL_R2_VALUE;
  reg[8] = RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_OLD_LO_VALUE;
  reg[9] = RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_OLD_HI_VALUE;
  reg[REG_CPSR] = RUNTIME_CPSR_CV_LOW_VALUE;
  execute_arm_translate_internal(
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_START_PC);
  reg[3] = RUNTIME_MULTIPLY_LONG_SMULL_R3_VALUE;
  reg[4] = RUNTIME_MULTIPLY_LONG_SMULL_R4_VALUE;
  reg[10] = RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_OLD_LO_VALUE;
  reg[11] = RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_OLD_HI_VALUE;
  reg[REG_CPSR] = RUNTIME_CPSR_CV_LOW_VALUE;
  execute_arm_translate_internal(
    RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_FLAG_ADDS_START_PC);
  reg[1] = RUNTIME_FLAG_ADDS_R1_VALUE;
  reg[REG_CPSR] = RUNTIME_CPSR_LOW_VALUE;
  execute_arm_translate_internal(RUNTIME_FLAG_ADDS_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_FLAG_CMP_START_PC);
  reg[0] = RUNTIME_FLAG_CMP_R0_VALUE;
  reg[REG_CPSR] = RUNTIME_CPSR_LOW_VALUE;
  execute_arm_translate_internal(RUNTIME_FLAG_CMP_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_PSR_START_PC);
  reg[REG_CPSR] = RUNTIME_PSR_CPSR_VALUE;
  reg[CPU_MODE] = RUNTIME_PSR_CPU_MODE_VALUE;
  spsr[RUNTIME_PSR_CPU_MODE_VALUE & 0xfu] = RUNTIME_PSR_SPSR_VALUE;
  execute_arm_translate_internal(RUNTIME_PSR_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash,
    reg_mode[RUNTIME_PSR_CPU_MODE_VALUE & 0xfu][6],
    spsr[RUNTIME_PSR_CPU_MODE_VALUE & 0xfu]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_MSR_CPSR_FLAGS_START_PC);
  reg[REG_CPSR] = RUNTIME_MSR_CPSR_FLAGS_INITIAL;
  reg[CPU_MODE] = MODE_SUPERVISOR;
  execute_arm_translate_internal(RUNTIME_MSR_CPSR_FLAGS_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_MSR_SPSR_START_PC);
  reg[1] = RUNTIME_MSR_SPSR_SOURCE;
  reg[REG_CPSR] = RUNTIME_PSR_CPSR_VALUE;
  reg[CPU_MODE] = MODE_SUPERVISOR;
  spsr[MODE_SUPERVISOR & 0xfu] = RUNTIME_MSR_SPSR_OLD_VALUE;
  execute_arm_translate_internal(RUNTIME_MSR_SPSR_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash,
    reg_mode[MODE_SUPERVISOR & 0xfu][6],
    spsr[MODE_SUPERVISOR & 0xfu]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_LOAD_START_PC);
  reg[3] = RUNTIME_LOAD_BASE_ADDR;
  execute_arm_translate_internal(RUNTIME_LOAD_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_STORE_START_PC);
  reg[3] = RUNTIME_STORE_BASE_ADDR;
  reg[6] = g_runtime_fixture_store_word;
  execute_arm_translate_internal(RUNTIME_STORE_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_STORE_START_PC);
  reg[3] = RUNTIME_STORE_BASE_ADDR;
  reg[6] = g_runtime_fixture_store_word;
  g_runtime_store_alert = CPU_ALERT_SMC | CPU_ALERT_IRQ;
  execute_arm_translate_internal(RUNTIME_STORE_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_HALF_LOAD_START_PC);
  reg[3] = RUNTIME_HALF_BASE_ADDR;
  execute_arm_translate_internal(RUNTIME_HALF_LOAD_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_half_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_HALF_STORE_START_PC);
  reg[3] = RUNTIME_HALF_BASE_ADDR;
  reg[7] = RUNTIME_HALF_STORE_VALUE;
  g_runtime_store_alert = CPU_ALERT_SMC | CPU_ALERT_IRQ;
  execute_arm_translate_internal(RUNTIME_HALF_STORE_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_half_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_HALF_REG_LOAD_START_PC);
  reg[2] = RUNTIME_HALF_REG_OFFSET_VALUE;
  reg[3] = RUNTIME_HALF_BASE_ADDR;
  execute_arm_translate_internal(RUNTIME_HALF_LOAD_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_half_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_HALF_REG_STORE_START_PC);
  reg[2] = RUNTIME_HALF_REG_OFFSET_VALUE;
  reg[3] = RUNTIME_HALF_BASE_ADDR;
  reg[9] = RUNTIME_HALF_STORE_VALUE;
  execute_arm_translate_internal(RUNTIME_HALF_STORE_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_half_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BLOCK_MEM_STM_START_PC);
  reg[0] = RUNTIME_BLOCK_MEM_STM_R0_VALUE;
  reg[2] = RUNTIME_BLOCK_MEM_STM_R2_VALUE;
  reg[3] = RUNTIME_BLOCK_MEM_BASE;
  reg[5] = RUNTIME_BLOCK_MEM_STM_R5_VALUE;
  execute_arm_translate_internal(RUNTIME_BLOCK_MEM_STM_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_block_mem32_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BLOCK_MEM_LDM_START_PC);
  reg[4] = RUNTIME_BLOCK_MEM_BASE;
  execute_arm_translate_internal(RUNTIME_BLOCK_MEM_LDM_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_block_mem32_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BLOCK_MEM_PUSH_START_PC);
  reg[0] = RUNTIME_BLOCK_MEM_PUSH_R0_VALUE;
  reg[1] = RUNTIME_BLOCK_MEM_PUSH_R1_VALUE;
  reg[REG_SP] = RUNTIME_BLOCK_MEM_PUSH_SP_START;
  reg[REG_LR] = RUNTIME_BLOCK_MEM_PUSH_LR_VALUE;
  execute_arm_translate_internal(RUNTIME_BLOCK_MEM_PUSH_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_block_mem32_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BLOCK_MEM_LDM_PC_START_PC);
  reg[2] = g_runtime_fixture_branch_r2;
  reg[4] = RUNTIME_BLOCK_MEM_PC_BASE;
  execute_arm_translate_internal(RUNTIME_BLOCK_MEM_LDM_PC_CHAIN_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_block_mem32_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BLOCK_MEM_LDM_PC_S_START_PC);
  reg[REG_CPSR] = MODE_SUPERVISOR;
  reg[CPU_MODE] = MODE_SUPERVISOR;
  reg[4] = RUNTIME_BLOCK_MEM_PC_BASE;
  spsr[MODE_SUPERVISOR & 0xfu] = RUNTIME_PC_WRITE_MOVS_SPSR_VALUE;
  execute_arm_translate_internal(RUNTIME_BLOCK_MEM_LDM_PC_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash,
    reg_mode[MODE_SUPERVISOR & 0xfu][6],
    spsr[MODE_SUPERVISOR & 0xfu]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  mem_hash = runtime_update_current_block_mem32_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_HLE_DIV_START_PC);
  reg[0] = RUNTIME_HLE_DIV_R0_VALUE;
  reg[1] = RUNTIME_HLE_DIV_R1_VALUE;
  execute_arm_translate_internal(RUNTIME_HLE_DIV_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_HLE_DIVARM_START_PC);
  reg[0] = RUNTIME_HLE_DIVARM_R0_VALUE;
  reg[1] = RUNTIME_HLE_DIVARM_R1_VALUE;
  execute_arm_translate_internal(RUNTIME_HLE_DIVARM_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_PC_SOURCE_START_PC);
  reg[0] = RUNTIME_PC_SOURCE_R0_VALUE;
  reg[1] = RUNTIME_PC_SOURCE_R1_VALUE;
  reg[2] = RUNTIME_PC_SOURCE_R2_VALUE;
  reg[REG_CPSR] = RUNTIME_PC_SOURCE_INITIAL_CPSR;
  execute_arm_translate_internal(RUNTIME_PC_SOURCE_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_WRITEBACK_STORE_START_PC);
  reg[3] = RUNTIME_WRITEBACK_BASE_ADDR;
  execute_arm_translate_internal(RUNTIME_WRITEBACK_STORE_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_WRITEBACK_LOAD_START_PC);
  reg[4] = RUNTIME_WRITEBACK_BASE_ADDR;
  execute_arm_translate_internal(RUNTIME_WRITEBACK_LOAD_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_REG_OFFSET_LOAD_START_PC);
  reg[2] = RUNTIME_REG_OFFSET_VALUE;
  reg[3] = RUNTIME_REG_OFFSET_BASE_ADDR;
  execute_arm_translate_internal(RUNTIME_REG_OFFSET_LOAD_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_SHIFTED_REG_OFFSET_START_PC);
  reg[2] = RUNTIME_REG_OFFSET_VALUE;
  reg[3] = RUNTIME_REG_OFFSET_BASE_ADDR;
  execute_arm_translate_internal(RUNTIME_SHIFTED_REG_OFFSET_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_REG_OFFSET_RRX_LOAD_START_PC);
  reg[2] = RUNTIME_REG_OFFSET_RRX_VALUE;
  reg[3] = RUNTIME_REG_OFFSET_BASE_ADDR;
  reg[REG_CPSR] = RUNTIME_CPSR_C_BIT;
  execute_arm_translate_internal(RUNTIME_REG_OFFSET_RRX_LOAD_TOTAL_CYCLES,
                                 &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_SWP_START_PC);
  reg[3] = RUNTIME_SWP_ADDR;
  reg[5] = g_runtime_fixture_store_word;
  execute_arm_translate_internal(RUNTIME_SWP_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BRANCH_START_PC);
  reg[1] = g_runtime_fixture_branch_r1;
  reg[2] = g_runtime_fixture_branch_r2;
  execute_arm_translate_internal(RUNTIME_BRANCH_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BRANCH_START_PC);
  idle_loop_target_pc = RUNTIME_BRANCH_TARGET_PC;
  execute_arm_translate_internal(RUNTIME_BRANCH_IDLE_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BX_START_PC);
  reg[1] = g_runtime_fixture_branch_r1;
  reg[2] = g_runtime_fixture_branch_r2;
  reg[7] = RUNTIME_BX_TARGET_PC;
  execute_arm_translate_internal(RUNTIME_BX_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_PATCH_BRANCH_START_PC);
  reg[1] = g_runtime_fixture_branch_r1;
  reg[2] = g_runtime_fixture_branch_r2;
  execute_arm_translate_internal(RUNTIME_PATCH_BRANCH_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_BL_START_PC);
  reg[1] = g_runtime_fixture_branch_r1;
  reg[2] = g_runtime_fixture_branch_r2;
  execute_arm_translate_internal(RUNTIME_BL_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_SWI_START_PC);
  reg[REG_CPSR] = RUNTIME_SWI_INITIAL_CPSR;
  reg[CPU_MODE] = 0x1fu;
  reg[REG_LR] = 0x12345678u;
  reg_mode[MODE_SUPERVISOR & 0xfu][6] = 0xa5a5a5a5u;
  spsr[MODE_SUPERVISOR & 0xfu] = 0x11111111u;
  reg[1] = g_runtime_fixture_branch_r1;
  reg[2] = g_runtime_fixture_branch_r2;
  execute_arm_translate_internal(RUNTIME_SWI_TOTAL_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash,
    reg_mode[MODE_SUPERVISOR & 0xfu][6],
    spsr[MODE_SUPERVISOR & 0xfu]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_COND_START_PC);
  reg[1] = g_runtime_fixture_branch_r1;
  reg[2] = g_runtime_fixture_branch_r2;
  reg[REG_CPSR] = 0;
  execute_arm_translate_internal(RUNTIME_COND_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_COND_START_PC);
  reg[1] = g_runtime_fixture_branch_r1;
  reg[2] = g_runtime_fixture_branch_r2;
  reg[REG_CPSR] = RUNTIME_CPSR_Z_BIT;
  execute_arm_translate_internal(RUNTIME_COND_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_PC_WRITE_MOVS_START_PC);
  reg[REG_LR] = RUNTIME_PC_WRITE_MOVS_TARGET;
  reg[CPU_MODE] = MODE_SUPERVISOR;
  spsr[MODE_SUPERVISOR & 0xfu] = RUNTIME_PC_WRITE_MOVS_SPSR_VALUE;
  execute_arm_translate_internal(RUNTIME_PC_WRITE_MOVS_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  reg_hash = runtime_update_supervisor_state_hash(
    reg_hash,
    reg_mode[MODE_SUPERVISOR & 0xfu][6],
    spsr[MODE_SUPERVISOR & 0xfu]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_UNSUPPORTED_START_PC);
  execute_arm_translate_internal(RUNTIME_UNSUPPORTED_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  reset_runtime_fixture_state(RUNTIME_THUMB_FALLBACK_START_PC);
  reg[REG_CPSR] = RUNTIME_CPSR_T_BIT;
  execute_arm_translate_internal(RUNTIME_THUMB_FALLBACK_CYCLES, &reg[0]);
  reg_hash = runtime_update_reg_hash(reg_hash, &reg[0]);
  mem_hash = runtime_update_current_memory_hash(mem_hash);
  scheduler_hash = runtime_update_current_scheduler_hash(scheduler_hash);

  riscv_get_runtime_stats(&after);
  snapshot->frame_hash = runtime_fixture_frame_hash(base);
  snapshot->reg_hash = reg_hash;
  snapshot->mem_hash = mem_hash;
  snapshot->scheduler_hash = scheduler_hash;
  snapshot->blocks = after.blocks_executed - before.blocks_executed;
  snapshot->fallbacks = after.interpreter_fallbacks - before.interpreter_fallbacks;
  snapshot->native_data_proc = after.native_data_proc_insns;
  snapshot->native_branch = after.native_branch_insns;
  snapshot->native_load = after.native_load_insns;
  snapshot->native_store = after.native_store_insns;
  snapshot->native_psr = after.native_psr_insns;
}

void execute_arm(u32 cycles)
{
  g_runtime_execute_calls++;
  g_runtime_execute_cycles = cycles;
  g_runtime_execute_pc = reg[REG_PC];
}

u32 function_cc read_memory8(u32 address)
{
  u32 value = 0;

  (void)address;
  g_runtime_read_calls++;
  g_runtime_read8_calls++;
  g_runtime_read8_addr = address;
  g_runtime_read8_pc = reg[REG_PC];
  if (address == RUNTIME_LOAD_BYTE_ADDR)
    value = g_runtime_fixture_load_byte;
  else if (address == RUNTIME_REG_OFFSET_BYTE_ADDR)
    value = RUNTIME_REG_OFFSET_BYTE_VALUE;
  else if (address == RUNTIME_SHIFTED_REG_OFFSET_BYTE_ADDR)
    value = RUNTIME_SHIFTED_REG_OFFSET_BYTE_VALUE;
  else if (address == RUNTIME_WRITEBACK_POST_LOAD_ADDR)
    value = RUNTIME_WRITEBACK_POST_LOAD_VALUE;
  g_runtime_read8_value = value;
  return value;
}

u32 function_cc read_memory8s(u32 address)
{
  u32 value = 0;

  g_runtime_read_calls++;
  g_runtime_read8s_calls++;
  g_runtime_read8s_addr = address;
  g_runtime_read8s_pc = reg[REG_PC];
  if (address == RUNTIME_HALF_S8_ADDR)
    value = RUNTIME_HALF_S8_VALUE;
  else if (address == RUNTIME_HALF_REG_S8_ADDR)
    value = RUNTIME_HALF_S8_VALUE;
  g_runtime_read8s_value = value;
  return value;
}

u32 function_cc read_memory16(u32 address)
{
  u32 value = 0;

  g_runtime_read_calls++;
  g_runtime_read16_calls++;
  g_runtime_read16_addr = address;
  g_runtime_read16_pc = reg[REG_PC];
  if (address == RUNTIME_HALF_U16_ADDR)
    value = RUNTIME_HALF_U16_VALUE;
  g_runtime_read16_value = value;
  return value;
}

u32 function_cc read_memory16s(u32 address)
{
  u32 value = 0;

  g_runtime_read_calls++;
  g_runtime_read16s_calls++;
  g_runtime_read16s_addr = address;
  g_runtime_read16s_pc = reg[REG_PC];
  if (address == RUNTIME_HALF_S16_ADDR)
    value = RUNTIME_HALF_S16_VALUE;
  else if (address == RUNTIME_HALF_REG_S16_ADDR)
    value = RUNTIME_HALF_S16_VALUE;
  g_runtime_read16s_value = value;
  return value;
}

u32 function_cc read_memory32(u32 address)
{
  u32 value = 0;

  g_runtime_read_calls++;
  g_runtime_read32_calls++;
  g_runtime_read32_addr = address;
  g_runtime_read32_pc = reg[REG_PC];
  if (address == RUNTIME_LOAD_WORD_ADDR)
    value = g_runtime_fixture_load_word;
  else if (address == RUNTIME_REG_OFFSET_WORD_ADDR)
    value = RUNTIME_REG_OFFSET_WORD_VALUE;
  else if (address == RUNTIME_REG_OFFSET_RRX_WORD_ADDR)
    value = RUNTIME_REG_OFFSET_RRX_WORD_VALUE;
  else if (address == RUNTIME_SWP_ADDR)
    value = g_runtime_fixture_swp_old_word;
  else if (address == RUNTIME_BLOCK_MEM_BASE)
    value = RUNTIME_BLOCK_MEM_LDM_R1_VALUE;
  else if (address == RUNTIME_BLOCK_MEM_BASE + 4u)
    value = RUNTIME_BLOCK_MEM_LDM_R6_VALUE;
  else if (address == RUNTIME_BLOCK_MEM_BASE + 8u)
    value = RUNTIME_BLOCK_MEM_LDM_R7_VALUE;
  else if (address == RUNTIME_BLOCK_MEM_PC_BASE)
    value = RUNTIME_BLOCK_MEM_LDM_PC_R1_VALUE;
  else if (address == RUNTIME_BLOCK_MEM_PC_BASE + 4u)
    value = RUNTIME_BLOCK_MEM_LDM_PC_R6_VALUE;
  else if (address == RUNTIME_BLOCK_MEM_PC_BASE + 8u)
    value = RUNTIME_BRANCH_TARGET_PC;
  g_runtime_read32_value = value;
  g_runtime_block_mem32_hash = runtime_update_block_mem32_event_hash(
    g_runtime_block_mem32_hash, RUNTIME_BLOCK_MEM_READ32_TAG,
    address, reg[REG_PC], value);
  return value;
}

cpu_alert_type function_cc write_memory8(u32 address, u8 value)
{
  (void)address;
  (void)value;
  g_runtime_write_calls++;
  return CPU_ALERT_NONE;
}

cpu_alert_type function_cc write_memory16(u32 address, u16 value)
{
  g_runtime_write_calls++;
  g_runtime_write16_calls++;
  g_runtime_write16_addr = address;
  g_runtime_write16_pc = reg[REG_PC];
  g_runtime_write16_value = value;
  return g_runtime_store_alert;
}

cpu_alert_type function_cc write_memory32(u32 address, u32 value)
{
  g_runtime_write_calls++;
  g_runtime_write32_calls++;
  g_runtime_write32_addr = address;
  g_runtime_write32_pc = reg[REG_PC];
  g_runtime_write32_value = value;
  g_runtime_block_mem32_hash = runtime_update_block_mem32_event_hash(
    g_runtime_block_mem32_hash, RUNTIME_BLOCK_MEM_WRITE32_TAG,
    address, reg[REG_PC], value);
  return g_runtime_store_alert;
}

u32 check_and_raise_interrupts(void)
{
  g_runtime_irq_check_calls++;
  return 0;
}

void flush_translation_cache_ram(void)
{
  g_runtime_flush_calls++;
}

void set_cpu_mode(u32 new_mode)
{
  reg[REG_LR] = reg_mode[new_mode & 0xfu][6];
  reg[CPU_MODE] = new_mode;
}

u32 function_cc update_gba(int remaining_cycles)
{
  g_runtime_update_calls++;
  g_runtime_update_cycles = remaining_cycles;
  return FRAME_COMPLETE;
}

u8 function_cc *block_lookup_address_arm(u32 pc)
{
  g_runtime_lookup_calls++;
  g_runtime_lookup_pc = pc;
  if (g_runtime_entry && pc == RUNTIME_START_PC)
    return g_runtime_entry;
  if (g_runtime_multiply_entry && pc == RUNTIME_MULTIPLY_START_PC)
    return g_runtime_multiply_entry;
  if (g_runtime_multiply_long_entry &&
      pc == RUNTIME_MULTIPLY_LONG_START_PC)
    return g_runtime_multiply_long_entry;
  if (g_runtime_multiply_long_flag_umulls_entry &&
      pc == RUNTIME_MULTIPLY_LONG_FLAG_UMULLS_START_PC)
    return g_runtime_multiply_long_flag_umulls_entry;
  if (g_runtime_multiply_long_flag_smulls_entry &&
      pc == RUNTIME_MULTIPLY_LONG_FLAG_SMULLS_START_PC)
    return g_runtime_multiply_long_flag_smulls_entry;
  if (g_runtime_multiply_long_acc_entry &&
      pc == RUNTIME_MULTIPLY_LONG_ACC_START_PC)
    return g_runtime_multiply_long_acc_entry;
  if (g_runtime_multiply_long_acc_flag_umlals_entry &&
      pc == RUNTIME_MULTIPLY_LONG_ACC_FLAG_UMLALS_START_PC)
    return g_runtime_multiply_long_acc_flag_umlals_entry;
  if (g_runtime_multiply_long_acc_flag_smlals_entry &&
      pc == RUNTIME_MULTIPLY_LONG_ACC_FLAG_SMLALS_START_PC)
    return g_runtime_multiply_long_acc_flag_smlals_entry;
  if (g_runtime_flag_adds_entry && pc == RUNTIME_FLAG_ADDS_START_PC)
    return g_runtime_flag_adds_entry;
  if (g_runtime_flag_cmp_entry && pc == RUNTIME_FLAG_CMP_START_PC)
    return g_runtime_flag_cmp_entry;
  if (g_runtime_psr_entry && pc == RUNTIME_PSR_START_PC)
    return g_runtime_psr_entry;
  if (g_runtime_msr_cpsr_flags_entry &&
      pc == RUNTIME_MSR_CPSR_FLAGS_START_PC)
    return g_runtime_msr_cpsr_flags_entry;
  if (g_runtime_msr_spsr_entry && pc == RUNTIME_MSR_SPSR_START_PC)
    return g_runtime_msr_spsr_entry;
  if (g_runtime_load_entry && pc == RUNTIME_LOAD_START_PC)
    return g_runtime_load_entry;
  if (g_runtime_store_entry && pc == RUNTIME_STORE_START_PC)
    return g_runtime_store_entry;
  if (g_runtime_half_load_entry && pc == RUNTIME_HALF_LOAD_START_PC)
    return g_runtime_half_load_entry;
  if (g_runtime_half_store_entry && pc == RUNTIME_HALF_STORE_START_PC)
    return g_runtime_half_store_entry;
  if (g_runtime_half_reg_load_entry &&
      pc == RUNTIME_HALF_REG_LOAD_START_PC)
    return g_runtime_half_reg_load_entry;
  if (g_runtime_half_reg_store_entry &&
      pc == RUNTIME_HALF_REG_STORE_START_PC)
    return g_runtime_half_reg_store_entry;
  if (g_runtime_block_mem_stm_entry &&
      pc == RUNTIME_BLOCK_MEM_STM_START_PC)
    return g_runtime_block_mem_stm_entry;
  if (g_runtime_block_mem_ldm_entry &&
      pc == RUNTIME_BLOCK_MEM_LDM_START_PC)
    return g_runtime_block_mem_ldm_entry;
  if (g_runtime_block_mem_push_entry &&
      pc == RUNTIME_BLOCK_MEM_PUSH_START_PC)
    return g_runtime_block_mem_push_entry;
  if (g_runtime_block_mem_ldm_pc_entry &&
      pc == RUNTIME_BLOCK_MEM_LDM_PC_START_PC)
    return g_runtime_block_mem_ldm_pc_entry;
  if (g_runtime_block_mem_ldm_pc_s_entry &&
      pc == RUNTIME_BLOCK_MEM_LDM_PC_S_START_PC)
    return g_runtime_block_mem_ldm_pc_s_entry;
  if (g_runtime_hle_div_entry && pc == RUNTIME_HLE_DIV_START_PC)
    return g_runtime_hle_div_entry;
  if (g_runtime_hle_divarm_entry && pc == RUNTIME_HLE_DIVARM_START_PC)
    return g_runtime_hle_divarm_entry;
  if (g_runtime_pc_source_entry && pc == RUNTIME_PC_SOURCE_START_PC)
    return g_runtime_pc_source_entry;
  if (g_runtime_writeback_store_entry &&
      pc == RUNTIME_WRITEBACK_STORE_START_PC)
    return g_runtime_writeback_store_entry;
  if (g_runtime_writeback_load_entry &&
      pc == RUNTIME_WRITEBACK_LOAD_START_PC)
    return g_runtime_writeback_load_entry;
  if (g_runtime_reg_offset_load_entry &&
      pc == RUNTIME_REG_OFFSET_LOAD_START_PC)
    return g_runtime_reg_offset_load_entry;
  if (g_runtime_shifted_reg_offset_entry &&
      pc == RUNTIME_SHIFTED_REG_OFFSET_START_PC)
    return g_runtime_shifted_reg_offset_entry;
  if (g_runtime_reg_offset_rrx_load_entry &&
      pc == RUNTIME_REG_OFFSET_RRX_LOAD_START_PC)
    return g_runtime_reg_offset_rrx_load_entry;
  if (g_runtime_branch_entry && pc == RUNTIME_BRANCH_START_PC)
    return g_runtime_branch_entry;
  if (g_runtime_branch_target_entry && pc == RUNTIME_BRANCH_TARGET_PC)
    return g_runtime_branch_target_entry;
  if (g_runtime_unsupported_entry && pc == RUNTIME_UNSUPPORTED_START_PC)
    return g_runtime_unsupported_entry;
  if (g_runtime_bx_entry && pc == RUNTIME_BX_START_PC)
    return g_runtime_bx_entry;
  if (g_runtime_bx_target_entry && pc == RUNTIME_BX_TARGET_PC)
    return g_runtime_bx_target_entry;
  if (g_runtime_patch_branch_entry && pc == RUNTIME_PATCH_BRANCH_START_PC)
    return g_runtime_patch_branch_entry;
  if (g_runtime_patch_branch_target_entry &&
      pc == RUNTIME_PATCH_BRANCH_TARGET_PC)
    return g_runtime_patch_branch_target_entry;
  if (g_runtime_bl_entry && pc == RUNTIME_BL_START_PC)
    return g_runtime_bl_entry;
  if (g_runtime_bl_target_entry && pc == RUNTIME_BL_TARGET_PC)
    return g_runtime_bl_target_entry;
  if (g_runtime_swi_entry && pc == RUNTIME_SWI_START_PC)
    return g_runtime_swi_entry;
  if (g_runtime_swi_target_entry && pc == RUNTIME_SWI_TARGET_PC)
    return g_runtime_swi_target_entry;
  if (g_runtime_cond_entry && pc == RUNTIME_COND_START_PC)
    return g_runtime_cond_entry;
  if (g_runtime_pc_write_movs_entry &&
      pc == RUNTIME_PC_WRITE_MOVS_START_PC)
    return g_runtime_pc_write_movs_entry;
  if (g_runtime_swp_entry && pc == RUNTIME_SWP_START_PC)
    return g_runtime_swp_entry;

  return (u8 *)0;
}

u8 function_cc *block_lookup_address_thumb(u32 pc)
{
  (void)pc;
  g_runtime_thumb_lookup_calls++;
  return (u8 *)0;
}

void init_bios_hooks(void)
{
  g_runtime_bios_hook_calls++;
}

static const char *backend_name(void)
{
  return g_state.backend == BACKEND_RV32IM ? "rv32im" : "interp";
}

static void reset_state(void)
{
  g_state.backend = BACKEND_INTERP;
  g_state.frames = 0;
  g_state.cycles = 0;
  g_state.blocks = 0;
  g_state.instructions = 0;
  g_state.loaded_bytes = 0;
  g_state.loaded_hash = 2166136261u;
  g_state.last_frame_hash = 0;
}

static void render_frame(void)
{
  u32 x;
  u32 y;
  u32 seed = g_state.loaded_hash ^ (g_state.frames * 0x45d9f3bu);
  usize pos = 0;

  for (y = 0; y < FRAME_H; y++)
  {
    for (x = 0; x < FRAME_W; x++)
    {
      u32 r = (x + (seed >> 3) + g_state.frames) & 31u;
      u32 g = (y + (seed >> 9)) & 63u;
      u32 b = (x + y + (seed >> 17)) & 31u;
      u16 pix = (u16)((r << 11) | (g << 5) | b);
      g_frame[pos++] = (u8)(pix & 0xff);
      g_frame[pos++] = (u8)(pix >> 8);
    }
  }

  g_state.last_frame_hash = fnv1a_update(2166136261u, g_frame, FRAME_BYTES);
}

static void print_summary(const char *command, const char *reason)
{
  put_raw("result=PASS command=");
  put_raw(command);
  put_raw(" backend=");
  put_raw(backend_name());
  put_raw(" frames=");
  put_u32_dec(g_state.frames);
  put_raw(" cycles=");
  put_u32_dec(g_state.cycles);
  put_raw(" blocks=");
  put_u32_dec(g_state.blocks);
  put_raw(" instructions=");
  put_u32_dec(g_state.instructions);
  put_raw(" frame_hash=");
  put_u32_hex(g_state.last_frame_hash);
  put_raw(" harness_mode=");
  put_raw(HARNESS_MODE);
  put_raw(" reason=");
  put_raw(reason);
  put_chr('\n');
}

static void print_fail(const char *command, const char *reason)
{
  put_raw("result=FAIL command=");
  put_raw(command);
  put_raw(" backend=");
  put_raw(backend_name());
  put_raw(" harness_mode=");
  put_raw(HARNESS_MODE);
  put_raw(" reason=");
  put_raw(reason);
  put_chr('\n');
}

static void command_help(void)
{
  put_raw("commands=load reset backend run cont stepi stepb regs mem counters tracepc framehash compare png quit\n");
}

static void command_backend(char *arg)
{
  if (str_eq(arg, "interp"))
  {
    g_state.backend = BACKEND_INTERP;
    put_raw("ok backend=interp\n");
  }
  else if (str_eq(arg, "rv32im"))
  {
    g_state.backend = BACKEND_RV32IM;
    put_raw("ok backend=rv32im\n");
  }
  else
  {
    print_fail("backend", "unknown_backend");
  }
}

static void command_load(char *path)
{
  u8 buf[256];
  long fd;
  u32 hash = 2166136261u;
  u32 total = 0;

  if (!path || !*path)
  {
    print_fail("load", "missing_path");
    return;
  }

  fd = syscall4(SYS_OPENAT, AT_FDCWD, (long)path, O_RDONLY, 0);
  if (fd < 0)
  {
    print_fail("load", "open_error");
    return;
  }

  for (;;)
  {
    long got = syscall3(SYS_READ, fd, (long)buf, sizeof(buf));
    if (got < 0)
    {
      syscall1(SYS_CLOSE, fd);
      print_fail("load", "read_error");
      return;
    }
    if (got == 0)
      break;
    hash = fnv1a_update(hash, buf, (usize)got);
    total += (u32)got;
  }

  syscall1(SYS_CLOSE, fd);
  g_state.loaded_hash = hash;
  g_state.loaded_bytes = total;

  put_raw("ok command=load bytes=");
  put_u32_dec(total);
  put_raw(" hash=");
  put_u32_hex(hash);
  put_chr('\n');
}

static void command_reset(void)
{
  enum harness_backend backend = g_state.backend;
  u32 loaded_bytes = g_state.loaded_bytes;
  u32 loaded_hash = g_state.loaded_hash;

  reset_state();
  g_state.backend = backend;
  g_state.loaded_bytes = loaded_bytes;
  g_state.loaded_hash = loaded_hash;
  render_frame();
  put_raw("ok command=reset backend=");
  put_raw(backend_name());
  put_raw(" loaded_bytes=");
  put_u32_dec(g_state.loaded_bytes);
  put_raw(" loaded_hash=");
  put_u32_hex(g_state.loaded_hash);
  put_chr('\n');
}

static u32 optional_count(char *arg, u32 fallback)
{
  u32 value;
  if (!arg || !*arg)
    return fallback;
  if (!parse_u32_token(arg, &value))
    return fallback;
  return value;
}

static void command_run(char *arg)
{
  u32 frames = optional_count(arg, 1);
  g_state.frames += frames;
  g_state.cycles += frames * 280896u;
  g_state.blocks += frames * (g_state.backend == BACKEND_RV32IM ? 128u : 0u);
  g_state.instructions += frames * 1024u;
  render_frame();
  print_summary("run", "synthetic_frame_workload");
}

static void command_cont(char *arg)
{
  u32 cycles = optional_count(arg, 960);
  g_state.cycles += cycles;
  g_state.blocks += g_state.backend == BACKEND_RV32IM ? 1u : 0u;
  render_frame();
  print_summary("cont", "synthetic_scheduler_boundary");
}

static u32 synthetic_reg_value(const struct harness_state *state, u32 index)
{
  return state->loaded_hash ^ (index * 0x11111111u) ^ state->cycles;
}

static u8 synthetic_mem_value(const struct harness_state *state, u32 addr)
{
  return (u8)((addr + state->loaded_hash + state->cycles) & 0xff);
}

static u32 synthetic_trace_pc(const struct harness_state *state, u32 index)
{
  u32 seed = state->loaded_hash ^ state->cycles ^
    (state->blocks << 8) ^ (state->instructions << 1);

  return 0x08000000u + ((seed + index * 4u) & 0x000003fcu);
}

static void command_stepi(char *arg)
{
  u32 count = optional_count(arg, 1);
  g_state.instructions += count;
  g_state.cycles += count;
  render_frame();
  print_summary("stepi", "synthetic_instruction_step");
}

static void command_stepb(char *arg)
{
  u32 count = optional_count(arg, 1);
  g_state.blocks += count;
  g_state.cycles += count * 4u;
  render_frame();
  print_summary("stepb", "synthetic_block_step");
}

static void command_regs(void)
{
  u32 i;
  put_raw("regs");
  for (i = 0; i < 16; i++)
  {
    put_raw(" r");
    put_u32_dec(i);
    put_chr('=');
    put_u32_hex(synthetic_reg_value(&g_state, i));
  }
  put_raw(" harness_mode=");
  put_raw(HARNESS_MODE);
  put_chr('\n');
}

static void command_mem(char *addr_arg, char *len_arg)
{
  u32 addr;
  u32 len;
  u32 i;

  if (!parse_u32_token(addr_arg, &addr) || !parse_u32_token(len_arg, &len))
  {
    print_fail("mem", "bad_args");
    return;
  }

  if (len > 64)
    len = 64;

  put_raw("mem addr=");
  put_u32_hex(addr);
  put_raw(" len=");
  put_u32_dec(len);
  put_raw(" data=");
  for (i = 0; i < len; i++)
  {
    u8 value = synthetic_mem_value(&g_state, addr + i);
    put_chr(hex_digit(value >> 4));
    put_chr(hex_digit(value));
  }
  put_raw(" harness_mode=");
  put_raw(HARNESS_MODE);
  put_chr('\n');
}

static void command_counters(void)
{
  render_frame();
  put_raw("result=PASS command=counters backend=");
  put_raw(backend_name());
  put_raw(" frames=");
  put_u32_dec(g_state.frames);
  put_raw(" cycles=");
  put_u32_dec(g_state.cycles);
  put_raw(" blocks=");
  put_u32_dec(g_state.blocks);
  put_raw(" instructions=");
  put_u32_dec(g_state.instructions);
  put_raw(" loaded_bytes=");
  put_u32_dec(g_state.loaded_bytes);
  put_raw(" loaded_hash=");
  put_u32_hex(g_state.loaded_hash);
  put_raw(" frame_hash=");
  put_u32_hex(g_state.last_frame_hash);
  put_raw(" harness_mode=");
  put_raw(HARNESS_MODE);
  put_raw(" reason=synthetic_state_counters\n");
}

static void command_tracepc(char *arg)
{
  u32 count = optional_count(arg, 8);
  u32 i;
  u32 hash = 2166136261u;

  if (count > 16)
    count = 16;

  put_raw("tracepc backend=");
  put_raw(backend_name());
  put_raw(" count=");
  put_u32_dec(count);
  put_raw(" pcs=");
  for (i = 0; i < count; i++)
  {
    u32 pc = synthetic_trace_pc(&g_state, i);
    if (i)
      put_chr(',');
    put_u32_hex(pc);
    hash = fnv1a_update_u32(hash, pc);
  }
  put_raw(" hash=");
  put_u32_hex(hash);
  put_raw(" harness_mode=");
  put_raw(HARNESS_MODE);
  put_raw(" reason=synthetic_pc_trace\n");
}

static u32 crc32_update(u32 crc, const u8 *data, usize len)
{
  usize i;
  crc = ~crc;
  for (i = 0; i < len; i++)
  {
    u32 value = (crc ^ data[i]) & 0xffu;
    u32 bit;
    for (bit = 0; bit < 8; bit++)
      value = (value & 1u) ? (0xedb88320u ^ (value >> 1)) : (value >> 1);
    crc = (crc >> 8) ^ value;
  }
  return ~crc;
}

static u32 adler32(const u8 *data, usize len)
{
  const u32 mod = 65521u;
  u32 a = 1;
  u32 b = 0;
  usize i;

  for (i = 0; i < len; i++)
  {
    a += data[i];
    if (a >= mod)
      a -= mod;
    b += a;
    if (b >= mod)
      b %= mod;
  }

  return (b << 16) | a;
}

static void store_be32(u8 *dst, u32 value)
{
  dst[0] = (u8)(value >> 24);
  dst[1] = (u8)(value >> 16);
  dst[2] = (u8)(value >> 8);
  dst[3] = (u8)value;
}

static int write_png_chunk(int fd, const char type[4], const u8 *payload, u32 len)
{
  u8 header[8];
  u8 crc_bytes[4];
  u32 crc = 0;

  store_be32(header, len);
  header[4] = (u8)type[0];
  header[5] = (u8)type[1];
  header[6] = (u8)type[2];
  header[7] = (u8)type[3];

  crc = crc32_update(crc, (const u8 *)type, 4);
  crc = crc32_update(crc, payload, len);
  store_be32(crc_bytes, crc);

  if (write_all(fd, header, sizeof(header)) < 0)
    return -1;
  if (len && write_all(fd, payload, len) < 0)
    return -1;
  if (write_all(fd, crc_bytes, sizeof(crc_bytes)) < 0)
    return -1;
  return 0;
}

static void build_png_raw(void)
{
  u32 x;
  u32 y;
  usize src = 0;
  usize dst = 0;

  for (y = 0; y < FRAME_H; y++)
  {
    g_png_raw[dst++] = 0;
    for (x = 0; x < FRAME_W; x++)
    {
      u16 pix = (u16)(g_frame[src] | ((u16)g_frame[src + 1] << 8));
      u32 r = (pix >> 11) & 31u;
      u32 g = (pix >> 5) & 63u;
      u32 b = pix & 31u;
      src += 2;
      g_png_raw[dst++] = (u8)((r << 3) | (r >> 2));
      g_png_raw[dst++] = (u8)((g << 2) | (g >> 4));
      g_png_raw[dst++] = (u8)((b << 3) | (b >> 2));
    }
  }
}

static u32 build_zlib_stored(void)
{
  u32 src = 0;
  u32 dst = 0;
  u32 remaining = PNG_RAW_SIZE;
  u32 adler = adler32(g_png_raw, PNG_RAW_SIZE);

  g_zlib[dst++] = 0x78;
  g_zlib[dst++] = 0x01;

  while (remaining)
  {
    u32 block = remaining > ZLIB_BLOCK_MAX ? ZLIB_BLOCK_MAX : remaining;
    u32 final = (remaining == block);
    u32 nlen = (~block) & 0xffffu;
    u32 i;

    g_zlib[dst++] = (u8)final;
    g_zlib[dst++] = (u8)block;
    g_zlib[dst++] = (u8)(block >> 8);
    g_zlib[dst++] = (u8)nlen;
    g_zlib[dst++] = (u8)(nlen >> 8);

    for (i = 0; i < block; i++)
      g_zlib[dst++] = g_png_raw[src++];

    remaining -= block;
  }

  store_be32(&g_zlib[dst], adler);
  dst += 4;
  return dst;
}

static int write_png_file(const char *path)
{
  static const u8 png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  u8 ihdr[13];
  u32 zlen;
  long fd;

  fd = syscall4(SYS_OPENAT, AT_FDCWD, (long)path,
                O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return -1;

  build_png_raw();
  zlen = build_zlib_stored();

  store_be32(&ihdr[0], FRAME_W);
  store_be32(&ihdr[4], FRAME_H);
  ihdr[8] = 8;
  ihdr[9] = 2;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;

  if (write_all((int)fd, png_sig, sizeof(png_sig)) < 0 ||
      write_png_chunk((int)fd, "IHDR", ihdr, sizeof(ihdr)) < 0 ||
      write_png_chunk((int)fd, "IDAT", g_zlib, zlen) < 0 ||
      write_png_chunk((int)fd, "IEND", (const u8 *)"", 0) < 0)
  {
    syscall1(SYS_CLOSE, fd);
    return -1;
  }

  syscall1(SYS_CLOSE, fd);
  return 0;
}

static void command_framehash(void)
{
  render_frame();
  put_raw("framehash width=");
  put_u32_dec(FRAME_W);
  put_raw(" height=");
  put_u32_dec(FRAME_H);
  put_raw(" bytes=");
  put_u32_dec(FRAME_BYTES);
  put_raw(" hash=");
  put_u32_hex(g_state.last_frame_hash);
  put_raw(" harness_mode=");
  put_raw(HARNESS_MODE);
  put_chr('\n');
}

static void command_compare(void)
{
  const char *runtime_reason = "runtime_unknown";
  struct harness_state saved_state = g_state;
  struct compare_snapshot interp;
  struct compare_snapshot rv32im;

  if (!ensure_runtime_fixture(&runtime_reason))
  {
    put_raw("result=FAIL command=compare workload=arm_add_multiply_multiplylong_longmulflags_longmulacc_longmulaccflags_flags_psr_msr_load_store_regoff_halfreg_blockmem_blockpush_blockpc_blockspsr_hle_pcsrc_writeback_swp_alert_branch_patch_bl_bx_swi_cond_spsr_idle_thumb_fallback");
    put_raw(" harness_mode=");
    put_raw(RUNTIME_FIXTURE_MODE);
    put_raw(" frame_mode=synthetic mem_mode=runtime_stickybits reason=");
    put_raw(runtime_reason);
    put_chr('\n');
    return;
  }

  run_runtime_reference_workload(&saved_state, &interp);
  run_runtime_rv32im_workload(&saved_state, &rv32im);
  g_state = saved_state;
  render_frame();

  if (interp.frame_hash != rv32im.frame_hash ||
      interp.reg_hash != rv32im.reg_hash ||
      interp.mem_hash != rv32im.mem_hash ||
      interp.scheduler_hash != rv32im.scheduler_hash ||
      rv32im.blocks != interp.blocks ||
      rv32im.fallbacks != interp.fallbacks ||
      rv32im.native_data_proc != interp.native_data_proc ||
      rv32im.native_branch != interp.native_branch ||
      rv32im.native_load != interp.native_load ||
      rv32im.native_store != interp.native_store ||
      rv32im.native_psr != interp.native_psr)
  {
    put_raw("result=FAIL command=compare workload=arm_add_multiply_multiplylong_longmulflags_longmulacc_longmulaccflags_flags_psr_msr_load_store_regoff_halfreg_blockmem_blockpush_blockpc_blockspsr_hle_pcsrc_writeback_swp_alert_branch_patch_bl_bx_swi_cond_spsr_idle_thumb_fallback interp_frame_hash=");
    put_u32_hex(interp.frame_hash);
    put_raw(" rv32im_frame_hash=");
    put_u32_hex(rv32im.frame_hash);
    put_raw(" interp_reg_hash=");
    put_u32_hex(interp.reg_hash);
    put_raw(" rv32im_reg_hash=");
    put_u32_hex(rv32im.reg_hash);
    put_raw(" interp_mem_hash=");
    put_u32_hex(interp.mem_hash);
    put_raw(" rv32im_mem_hash=");
    put_u32_hex(rv32im.mem_hash);
    put_raw(" interp_scheduler_hash=");
    put_u32_hex(interp.scheduler_hash);
    put_raw(" rv32im_scheduler_hash=");
    put_u32_hex(rv32im.scheduler_hash);
    put_raw(" rv32im_blocks=");
    put_u32_dec(rv32im.blocks);
    put_raw(" rv32im_fallbacks=");
    put_u32_dec(rv32im.fallbacks);
    put_raw(" rv32im_native_data_proc=");
    put_u32_dec(rv32im.native_data_proc);
    put_raw(" rv32im_native_branch=");
    put_u32_dec(rv32im.native_branch);
    put_raw(" rv32im_native_load=");
    put_u32_dec(rv32im.native_load);
    put_raw(" rv32im_native_store=");
    put_u32_dec(rv32im.native_store);
    put_raw(" rv32im_native_psr=");
    put_u32_dec(rv32im.native_psr);
    put_raw(" code_bytes=");
    put_u32_dec(g_runtime_code_bytes);
    put_raw(" harness_mode=");
    put_raw(RUNTIME_FIXTURE_MODE);
    put_raw(" frame_mode=synthetic mem_mode=runtime_stickybits");
    put_raw(" reason=runtime_state_or_frame_mismatch\n");
    return;
  }

  put_raw("result=PASS command=compare workload=arm_add_multiply_multiplylong_longmulflags_longmulacc_longmulaccflags_flags_psr_msr_load_store_regoff_halfreg_blockmem_blockpush_blockpc_blockspsr_hle_pcsrc_writeback_swp_alert_branch_patch_bl_bx_swi_cond_spsr_idle_thumb_fallback interp_frame_hash=");
  put_u32_hex(interp.frame_hash);
  put_raw(" rv32im_frame_hash=");
  put_u32_hex(rv32im.frame_hash);
  put_raw(" interp_reg_hash=");
  put_u32_hex(interp.reg_hash);
  put_raw(" rv32im_reg_hash=");
  put_u32_hex(rv32im.reg_hash);
  put_raw(" interp_mem_hash=");
  put_u32_hex(interp.mem_hash);
  put_raw(" rv32im_mem_hash=");
  put_u32_hex(rv32im.mem_hash);
  put_raw(" interp_scheduler_hash=");
  put_u32_hex(interp.scheduler_hash);
  put_raw(" rv32im_scheduler_hash=");
  put_u32_hex(rv32im.scheduler_hash);
  put_raw(" rv32im_blocks=");
  put_u32_dec(rv32im.blocks);
  put_raw(" rv32im_fallbacks=");
  put_u32_dec(rv32im.fallbacks);
  put_raw(" rv32im_native_data_proc=");
  put_u32_dec(rv32im.native_data_proc);
  put_raw(" rv32im_native_branch=");
  put_u32_dec(rv32im.native_branch);
  put_raw(" rv32im_native_load=");
  put_u32_dec(rv32im.native_load);
  put_raw(" rv32im_native_store=");
  put_u32_dec(rv32im.native_store);
  put_raw(" rv32im_native_psr=");
  put_u32_dec(rv32im.native_psr);
  put_raw(" code_bytes=");
  put_u32_dec(g_runtime_code_bytes);
  put_raw(" harness_mode=");
  put_raw(RUNTIME_FIXTURE_MODE);
  put_raw(" frame_mode=synthetic mem_mode=runtime_stickybits");
  put_raw(" reason=runtime_state_synthetic_frame_equal\n");
}

static void command_png(char *path)
{
  if (!path || !*path)
  {
    print_fail("png", "missing_path");
    return;
  }

  render_frame();
  if (write_png_file(path) < 0)
  {
    print_fail("png", "write_error");
    return;
  }

  put_raw("result=PASS command=png backend=");
  put_raw(backend_name());
  put_raw(" width=");
  put_u32_dec(FRAME_W);
  put_raw(" height=");
  put_u32_dec(FRAME_H);
  put_raw(" frame_hash=");
  put_u32_hex(g_state.last_frame_hash);
  put_raw(" harness_mode=");
  put_raw(HARNESS_MODE);
  put_raw(" path=");
  put_raw(path);
  put_chr('\n');
}

static int read_line(char *line, usize cap)
{
  usize len = 0;

  while (len + 1 < cap)
  {
    char ch;
    long got = syscall3(SYS_READ, STDIN_FD, (long)&ch, 1);
    if (got < 0)
      return -1;
    if (got == 0)
    {
      if (len == 0)
        return 0;
      break;
    }
    if (ch == '\n')
      break;
    line[len++] = ch;
  }

  line[len] = 0;
  return 1;
}

static void process_line(char *line)
{
  char *cursor = line;
  char *cmd = next_token(&cursor);

  if (!cmd || !*cmd)
    return;

  if (str_eq(cmd, "help"))
  {
    command_help();
  }
  else if (str_eq(cmd, "load"))
  {
    command_load(next_token(&cursor));
  }
  else if (str_eq(cmd, "reset"))
  {
    command_reset();
  }
  else if (str_eq(cmd, "backend"))
  {
    command_backend(next_token(&cursor));
  }
  else if (str_eq(cmd, "run"))
  {
    command_run(next_token(&cursor));
  }
  else if (str_eq(cmd, "cont"))
  {
    command_cont(next_token(&cursor));
  }
  else if (str_eq(cmd, "stepi"))
  {
    command_stepi(next_token(&cursor));
  }
  else if (str_eq(cmd, "stepb"))
  {
    command_stepb(next_token(&cursor));
  }
  else if (str_eq(cmd, "regs"))
  {
    command_regs();
  }
  else if (str_eq(cmd, "mem"))
  {
    char *addr = next_token(&cursor);
    char *len = next_token(&cursor);
    command_mem(addr, len);
  }
  else if (str_eq(cmd, "counters"))
  {
    command_counters();
  }
  else if (str_eq(cmd, "tracepc"))
  {
    command_tracepc(next_token(&cursor));
  }
  else if (str_eq(cmd, "framehash"))
  {
    command_framehash();
  }
  else if (str_eq(cmd, "compare"))
  {
    command_compare();
  }
  else if (str_eq(cmd, "png"))
  {
    command_png(next_token(&cursor));
  }
  else if (str_eq(cmd, "quit"))
  {
    render_frame();
    print_summary("quit", "requested");
    sys_exit(0);
  }
  else
  {
    print_fail(cmd, "unknown_command");
  }
}

void _start(void)
{
  reset_state();
  render_frame();
  put_raw("rv32im-harness ready version=0 backend=interp\n");

  for (;;)
  {
    int ret = read_line(g_line, sizeof(g_line));
    if (ret < 0)
      sys_exit(2);
    if (ret == 0)
      sys_exit(0);
    process_line(g_line);
  }
}
