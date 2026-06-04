#define u32 uint32_t
#define u8  uint8_t

#include <stdint.h>
#include <stdio.h>
#include "riscv_codegen.h"

int main(void)
{
  u32 buffer[1024];
  u8 *translation_ptr = (u8 *)&buffer[0];
  const int imm[] = {-2048, -1, 0, 1, 2047};
  const unsigned shamt[] = {0, 1, 30, 31};
  unsigned i;

  riscv_emit_nop();

  riscv_emit_add(riscv_reg_a0, riscv_reg_a1, riscv_reg_a2);
  riscv_emit_sub(riscv_reg_s0, riscv_reg_s1, riscv_reg_s2);
  riscv_emit_xor(riscv_reg_t0, riscv_reg_t1, riscv_reg_t2);
  riscv_emit_or(riscv_reg_t3, riscv_reg_t4, riscv_reg_t5);
  riscv_emit_and(riscv_reg_s3, riscv_reg_s4, riscv_reg_s5);
  riscv_emit_sll(riscv_reg_a3, riscv_reg_a4, riscv_reg_a5);
  riscv_emit_srl(riscv_reg_a6, riscv_reg_a7, riscv_reg_s6);
  riscv_emit_sra(riscv_reg_s7, riscv_reg_s8, riscv_reg_s9);
  riscv_emit_slt(riscv_reg_s10, riscv_reg_s11, riscv_reg_t3);
  riscv_emit_sltu(riscv_reg_t4, riscv_reg_t5, riscv_reg_t6);

  riscv_emit_mul(riscv_reg_a0, riscv_reg_a1, riscv_reg_a2);
  riscv_emit_mulh(riscv_reg_a3, riscv_reg_a4, riscv_reg_a5);
  riscv_emit_mulhsu(riscv_reg_a6, riscv_reg_a7, riscv_reg_s0);
  riscv_emit_mulhu(riscv_reg_s1, riscv_reg_s2, riscv_reg_s3);
  riscv_emit_div(riscv_reg_s4, riscv_reg_s5, riscv_reg_s6);
  riscv_emit_divu(riscv_reg_s7, riscv_reg_s8, riscv_reg_s9);
  riscv_emit_rem(riscv_reg_s10, riscv_reg_s11, riscv_reg_t3);
  riscv_emit_remu(riscv_reg_t4, riscv_reg_t5, riscv_reg_t6);

  for (i = 0; i < sizeof(imm) / sizeof(imm[0]); i++)
  {
    riscv_emit_addi(riscv_reg_a0, riscv_reg_s0, imm[i]);
    riscv_emit_slti(riscv_reg_a1, riscv_reg_s1, imm[i]);
    riscv_emit_sltiu(riscv_reg_a2, riscv_reg_s2, imm[i]);
    riscv_emit_xori(riscv_reg_a3, riscv_reg_s3, imm[i]);
    riscv_emit_ori(riscv_reg_a4, riscv_reg_s4, imm[i]);
    riscv_emit_andi(riscv_reg_a5, riscv_reg_s5, imm[i]);
  }

  for (i = 0; i < sizeof(shamt) / sizeof(shamt[0]); i++)
  {
    riscv_emit_slli(riscv_reg_a0, riscv_reg_a1, shamt[i]);
    riscv_emit_srli(riscv_reg_a2, riscv_reg_a3, shamt[i]);
    riscv_emit_srai(riscv_reg_a4, riscv_reg_a5, shamt[i]);
  }

  riscv_emit_lui(riscv_reg_a0, 0x12345);
  riscv_emit_lui(riscv_reg_s0, 0x00000);
  riscv_emit_auipc(riscv_reg_a1, 0x54321);
  riscv_emit_auipc(riscv_reg_s1, 0xfffff);

  for (i = 0; i < sizeof(imm) / sizeof(imm[0]); i++)
  {
    riscv_emit_lb(riscv_reg_a0, riscv_reg_a1, imm[i]);
    riscv_emit_lbu(riscv_reg_a2, riscv_reg_a3, imm[i]);
    riscv_emit_lh(riscv_reg_a4, riscv_reg_a5, imm[i]);
    riscv_emit_lhu(riscv_reg_a6, riscv_reg_a7, imm[i]);
    riscv_emit_lw(riscv_reg_s0, riscv_reg_s1, imm[i]);
  }

  for (i = 0; i < sizeof(imm) / sizeof(imm[0]); i++)
  {
    riscv_emit_sb(riscv_reg_a0, riscv_reg_a1, imm[i]);
    riscv_emit_sh(riscv_reg_a2, riscv_reg_a3, imm[i]);
    riscv_emit_sw(riscv_reg_a4, riscv_reg_a5, imm[i]);
  }

  riscv_emit_jalr(riscv_reg_ra, riscv_reg_sp, 0);
  riscv_emit_jalr(riscv_reg_t0, riscv_reg_t1, -16);

  riscv_emit_nop();
  riscv_emit_beq(riscv_reg_a0, riscv_reg_a1, 4);
  riscv_emit_bne(riscv_reg_a2, riscv_reg_a3, 8);
  riscv_emit_nop();
  riscv_emit_nop();
  riscv_emit_blt(riscv_reg_a4, riscv_reg_a5, -4);
  riscv_emit_bge(riscv_reg_a6, riscv_reg_a7, 8);
  riscv_emit_nop();
  riscv_emit_bltu(riscv_reg_s0, riscv_reg_s1, 4);
  riscv_emit_bgeu(riscv_reg_s2, riscv_reg_s3, 8);
  riscv_emit_nop();
  riscv_emit_jal(riscv_reg_a0, 8);
  riscv_emit_nop();
  riscv_emit_nop();
  riscv_emit_jal(riscv_reg_a1, -4);

  fwrite(buffer, 1, (size_t)(translation_ptr - (u8 *)buffer), stdout);
  return 0;
}
