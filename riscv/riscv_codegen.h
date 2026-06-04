/* gpSP RV32I/M code emitter helpers.
 *
 * The first RISC-V backend milestone emits plain 32-bit RV32I/M instructions
 * only. Do not emit compressed instructions here.
 */

#ifndef RISCV_CODEGEN_H
#define RISCV_CODEGEN_H

typedef enum
{
  riscv_reg_zero = 0,
  riscv_reg_ra   = 1,
  riscv_reg_sp   = 2,
  riscv_reg_gp   = 3,
  riscv_reg_tp   = 4,
  riscv_reg_t0   = 5,
  riscv_reg_t1   = 6,
  riscv_reg_t2   = 7,
  riscv_reg_s0   = 8,
  riscv_reg_fp   = 8,
  riscv_reg_s1   = 9,
  riscv_reg_a0   = 10,
  riscv_reg_a1   = 11,
  riscv_reg_a2   = 12,
  riscv_reg_a3   = 13,
  riscv_reg_a4   = 14,
  riscv_reg_a5   = 15,
  riscv_reg_a6   = 16,
  riscv_reg_a7   = 17,
  riscv_reg_s2   = 18,
  riscv_reg_s3   = 19,
  riscv_reg_s4   = 20,
  riscv_reg_s5   = 21,
  riscv_reg_s6   = 22,
  riscv_reg_s7   = 23,
  riscv_reg_s8   = 24,
  riscv_reg_s9   = 25,
  riscv_reg_s10  = 26,
  riscv_reg_s11  = 27,
  riscv_reg_t3   = 28,
  riscv_reg_t4   = 29,
  riscv_reg_t5   = 30,
  riscv_reg_t6   = 31
} riscv_reg_number;

typedef enum
{
  riscv_opcode_load   = 0x03,
  riscv_opcode_op_imm = 0x13,
  riscv_opcode_auipc  = 0x17,
  riscv_opcode_store  = 0x23,
  riscv_opcode_op     = 0x33,
  riscv_opcode_lui    = 0x37,
  riscv_opcode_branch = 0x63,
  riscv_opcode_jalr   = 0x67,
  riscv_opcode_jal    = 0x6f
} riscv_opcode;

#define riscv_emit_raw_u32(value)                                             \
  do                                                                          \
  {                                                                           \
    *((u32 *)translation_ptr) = (u32)(value);                                 \
    translation_ptr += 4;                                                     \
  } while (0)                                                                 \

#define riscv_emit_r(funct7, funct3, rd, rs1, rs2)                            \
  riscv_emit_raw_u32((((u32)(funct7) & 0x7f) << 25) |                        \
                     (((u32)(rs2) & 0x1f) << 20) |                           \
                     (((u32)(rs1) & 0x1f) << 15) |                           \
                     (((u32)(funct3) & 0x07) << 12) |                        \
                     (((u32)(rd) & 0x1f) << 7) |                             \
                     riscv_opcode_op)                                        \

#define riscv_emit_i(opcode, funct3, rd, rs1, imm)                            \
  riscv_emit_raw_u32((((u32)(imm) & 0xfff) << 20) |                          \
                     (((u32)(rs1) & 0x1f) << 15) |                           \
                     (((u32)(funct3) & 0x07) << 12) |                        \
                     (((u32)(rd) & 0x1f) << 7) |                             \
                     (opcode))                                               \

#define riscv_emit_s(funct3, rs1, rs2, imm)                                   \
  riscv_emit_raw_u32(((((u32)(imm) >> 5) & 0x7f) << 25) |                    \
                     (((u32)(rs2) & 0x1f) << 20) |                           \
                     (((u32)(rs1) & 0x1f) << 15) |                           \
                     (((u32)(funct3) & 0x07) << 12) |                        \
                     (((u32)(imm) & 0x1f) << 7) |                            \
                     riscv_opcode_store)                                     \

#define riscv_emit_b(funct3, rs1, rs2, offset)                                \
  riscv_emit_raw_u32(((((u32)(offset) >> 12) & 0x01) << 31) |                \
                     ((((u32)(offset) >> 5) & 0x3f) << 25) |                 \
                     (((u32)(rs2) & 0x1f) << 20) |                           \
                     (((u32)(rs1) & 0x1f) << 15) |                           \
                     (((u32)(funct3) & 0x07) << 12) |                        \
                     ((((u32)(offset) >> 1) & 0x0f) << 8) |                  \
                     ((((u32)(offset) >> 11) & 0x01) << 7) |                 \
                     riscv_opcode_branch)                                    \

#define riscv_emit_u(opcode, rd, imm20)                                       \
  riscv_emit_raw_u32((((u32)(imm20) & 0xfffff) << 12) |                      \
                     (((u32)(rd) & 0x1f) << 7) |                             \
                     (opcode))                                               \

#define riscv_emit_j(rd, offset)                                              \
  riscv_emit_raw_u32(((((u32)(offset) >> 20) & 0x01) << 31) |                \
                     ((((u32)(offset) >> 1) & 0x3ff) << 21) |                \
                     ((((u32)(offset) >> 11) & 0x01) << 20) |                \
                     ((((u32)(offset) >> 12) & 0xff) << 12) |                \
                     (((u32)(rd) & 0x1f) << 7) |                             \
                     riscv_opcode_jal)                                       \

#define riscv_emit_nop()                                                      \
  riscv_emit_addi(riscv_reg_zero, riscv_reg_zero, 0)                          \

#define riscv_emit_add(rd, rs1, rs2)  riscv_emit_r(0x00, 0x0, rd, rs1, rs2)
#define riscv_emit_sub(rd, rs1, rs2)  riscv_emit_r(0x20, 0x0, rd, rs1, rs2)
#define riscv_emit_sll(rd, rs1, rs2)  riscv_emit_r(0x00, 0x1, rd, rs1, rs2)
#define riscv_emit_slt(rd, rs1, rs2)  riscv_emit_r(0x00, 0x2, rd, rs1, rs2)
#define riscv_emit_sltu(rd, rs1, rs2) riscv_emit_r(0x00, 0x3, rd, rs1, rs2)
#define riscv_emit_xor(rd, rs1, rs2)  riscv_emit_r(0x00, 0x4, rd, rs1, rs2)
#define riscv_emit_srl(rd, rs1, rs2)  riscv_emit_r(0x00, 0x5, rd, rs1, rs2)
#define riscv_emit_sra(rd, rs1, rs2)  riscv_emit_r(0x20, 0x5, rd, rs1, rs2)
#define riscv_emit_or(rd, rs1, rs2)   riscv_emit_r(0x00, 0x6, rd, rs1, rs2)
#define riscv_emit_and(rd, rs1, rs2)  riscv_emit_r(0x00, 0x7, rd, rs1, rs2)

#define riscv_emit_mul(rd, rs1, rs2)    riscv_emit_r(0x01, 0x0, rd, rs1, rs2)
#define riscv_emit_mulh(rd, rs1, rs2)   riscv_emit_r(0x01, 0x1, rd, rs1, rs2)
#define riscv_emit_mulhsu(rd, rs1, rs2) riscv_emit_r(0x01, 0x2, rd, rs1, rs2)
#define riscv_emit_mulhu(rd, rs1, rs2)  riscv_emit_r(0x01, 0x3, rd, rs1, rs2)
#define riscv_emit_div(rd, rs1, rs2)    riscv_emit_r(0x01, 0x4, rd, rs1, rs2)
#define riscv_emit_divu(rd, rs1, rs2)   riscv_emit_r(0x01, 0x5, rd, rs1, rs2)
#define riscv_emit_rem(rd, rs1, rs2)    riscv_emit_r(0x01, 0x6, rd, rs1, rs2)
#define riscv_emit_remu(rd, rs1, rs2)   riscv_emit_r(0x01, 0x7, rd, rs1, rs2)

#define riscv_emit_addi(rd, rs1, imm)  riscv_emit_i(riscv_opcode_op_imm, 0x0, rd, rs1, imm)
#define riscv_emit_slti(rd, rs1, imm)  riscv_emit_i(riscv_opcode_op_imm, 0x2, rd, rs1, imm)
#define riscv_emit_sltiu(rd, rs1, imm) riscv_emit_i(riscv_opcode_op_imm, 0x3, rd, rs1, imm)
#define riscv_emit_xori(rd, rs1, imm)  riscv_emit_i(riscv_opcode_op_imm, 0x4, rd, rs1, imm)
#define riscv_emit_ori(rd, rs1, imm)   riscv_emit_i(riscv_opcode_op_imm, 0x6, rd, rs1, imm)
#define riscv_emit_andi(rd, rs1, imm)  riscv_emit_i(riscv_opcode_op_imm, 0x7, rd, rs1, imm)

#define riscv_emit_slli(rd, rs1, shamt)                                      \
  riscv_emit_i(riscv_opcode_op_imm, 0x1, rd, rs1, ((u32)(shamt) & 0x1f))
#define riscv_emit_srli(rd, rs1, shamt)                                      \
  riscv_emit_i(riscv_opcode_op_imm, 0x5, rd, rs1, ((u32)(shamt) & 0x1f))
#define riscv_emit_srai(rd, rs1, shamt)                                      \
  riscv_emit_i(riscv_opcode_op_imm, 0x5, rd, rs1, 0x400 | ((u32)(shamt) & 0x1f))

#define riscv_emit_lb(rd, rs1, imm)  riscv_emit_i(riscv_opcode_load, 0x0, rd, rs1, imm)
#define riscv_emit_lh(rd, rs1, imm)  riscv_emit_i(riscv_opcode_load, 0x1, rd, rs1, imm)
#define riscv_emit_lw(rd, rs1, imm)  riscv_emit_i(riscv_opcode_load, 0x2, rd, rs1, imm)
#define riscv_emit_lbu(rd, rs1, imm) riscv_emit_i(riscv_opcode_load, 0x4, rd, rs1, imm)
#define riscv_emit_lhu(rd, rs1, imm) riscv_emit_i(riscv_opcode_load, 0x5, rd, rs1, imm)

#define riscv_emit_sb(rs2, rs1, imm) riscv_emit_s(0x0, rs1, rs2, imm)
#define riscv_emit_sh(rs2, rs1, imm) riscv_emit_s(0x1, rs1, rs2, imm)
#define riscv_emit_sw(rs2, rs1, imm) riscv_emit_s(0x2, rs1, rs2, imm)

#define riscv_emit_beq(rs1, rs2, offset)  riscv_emit_b(0x0, rs1, rs2, offset)
#define riscv_emit_bne(rs1, rs2, offset)  riscv_emit_b(0x1, rs1, rs2, offset)
#define riscv_emit_blt(rs1, rs2, offset)  riscv_emit_b(0x4, rs1, rs2, offset)
#define riscv_emit_bge(rs1, rs2, offset)  riscv_emit_b(0x5, rs1, rs2, offset)
#define riscv_emit_bltu(rs1, rs2, offset) riscv_emit_b(0x6, rs1, rs2, offset)
#define riscv_emit_bgeu(rs1, rs2, offset) riscv_emit_b(0x7, rs1, rs2, offset)

#define riscv_emit_jal(rd, offset)      riscv_emit_j(rd, offset)
#define riscv_emit_jalr(rd, rs1, imm)   riscv_emit_i(riscv_opcode_jalr, 0x0, rd, rs1, imm)
#define riscv_emit_lui(rd, imm20)       riscv_emit_u(riscv_opcode_lui, rd, imm20)
#define riscv_emit_auipc(rd, imm20)     riscv_emit_u(riscv_opcode_auipc, rd, imm20)

#endif
