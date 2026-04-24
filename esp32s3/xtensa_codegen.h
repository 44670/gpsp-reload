#ifndef XTENSA_CODEGEN_H
#define XTENSA_CODEGEN_H

#include <stdint.h>
#include <string.h>

#define XTENSA_BLOCK_LITERAL_BYTES 8

typedef struct xtensa_jit_block_meta
{
  uint32_t start_pc;
  uint32_t end_pc;
  uint32_t thumb;
} xtensa_jit_block_meta;

static inline uint8_t *xtensa_align_ptr(uint8_t *ptr)
{
  uintptr_t value = (uintptr_t)ptr;
  value = (value + (uintptr_t)3U) & ~(uintptr_t)3U;
  return (uint8_t *)value;
}

static inline void xtensa_store_u16(void *dst, uint16_t value)
{
  memcpy(dst, &value, sizeof(value));
}

static inline void xtensa_store_u32(void *dst, uint32_t value)
{
  memcpy(dst, &value, sizeof(value));
}

static inline void xtensa_emit_u8(uint8_t **ptr, uint8_t value)
{
  *(*ptr)++ = value;
}

static inline void xtensa_emit_u16(uint8_t **ptr, uint16_t value)
{
  xtensa_store_u16(*ptr, value);
  *ptr += sizeof(value);
}

static inline void xtensa_emit_l32r_imm_minus3(uint8_t **ptr, uint32_t reg_num)
{
  xtensa_emit_u8(ptr, (uint8_t)((reg_num << 4) | 0x01));
  xtensa_emit_u16(ptr, 0xFFFD);
}

static inline void xtensa_emit_l32r_literal(uint8_t **ptr, uint32_t reg_num,
                                            const void *literal)
{
  uintptr_t insn_addr = (uintptr_t)*ptr;
  uintptr_t base = (insn_addr + 3U) & ~(uintptr_t)3U;
  intptr_t delta = (intptr_t)(uintptr_t)literal - (intptr_t)base;

  xtensa_emit_u8(ptr, (uint8_t)((reg_num << 4) | 0x01));
  xtensa_emit_u16(ptr, (uint16_t)(delta >> 2));
}

static inline void xtensa_emit_addmi(uint8_t **ptr, uint32_t dst,
                                     uint32_t src, uint32_t imm_hi)
{
  xtensa_emit_u8(ptr, (uint8_t)((dst << 4) | 0x02));
  xtensa_emit_u8(ptr, (uint8_t)(0xD0 | src));
  xtensa_emit_u8(ptr, (uint8_t)imm_hi);
}

static inline void xtensa_emit_addi(uint8_t **ptr, uint32_t dst,
                                    uint32_t src, int32_t imm)
{
  xtensa_emit_u8(ptr, (uint8_t)((dst << 4) | 0x02));
  xtensa_emit_u8(ptr, (uint8_t)(0xC0 | src));
  xtensa_emit_u8(ptr, (uint8_t)imm);
}

static inline void xtensa_emit_movi(uint8_t **ptr, uint32_t reg_num,
                                    uint32_t imm)
{
  xtensa_emit_u8(ptr, (uint8_t)((reg_num << 4) | 0x02));
  xtensa_emit_u8(ptr, (uint8_t)(0xA0 | ((imm >> 8) & 0x0F)));
  xtensa_emit_u8(ptr, (uint8_t)(imm & 0xFF));
}

static inline void xtensa_emit_movi_u15(uint8_t **ptr, uint32_t reg_num,
                                        uint32_t imm)
{
  uint32_t low = imm & 0xFF;
  uint32_t high = imm >> 8;

  if (imm <= 0x7FF)
  {
    xtensa_emit_movi(ptr, reg_num, imm);
    return;
  }

  xtensa_emit_movi(ptr, reg_num, low);
  while (high != 0)
  {
    uint32_t chunk = (high > 0x7F) ? 0x7F : high;
    xtensa_emit_addmi(ptr, reg_num, reg_num, chunk);
    high -= chunk;
  }
}

static inline void xtensa_emit_entry_sp_32(uint8_t **ptr)
{
  xtensa_emit_u8(ptr, 0x36);
  xtensa_emit_u8(ptr, 0x41);
  xtensa_emit_u8(ptr, 0x00);
}

static inline void xtensa_emit_mov_n_a10_a3(uint8_t **ptr)
{
  xtensa_emit_u16(ptr, 0x03AD);
}

static inline void xtensa_emit_callx8_a2(uint8_t **ptr)
{
  xtensa_emit_u8(ptr, 0xE0);
  xtensa_emit_u8(ptr, 0x02);
  xtensa_emit_u8(ptr, 0x00);
}

static inline void xtensa_emit_beqz_a10_skip_retw(uint8_t **ptr)
{
  xtensa_emit_u8(ptr, 0x16);
  xtensa_emit_u8(ptr, 0x1A);
  xtensa_emit_u8(ptr, 0x00);
}

static inline void xtensa_emit_mov_n_a2_a10(uint8_t **ptr)
{
  xtensa_emit_u16(ptr, 0x0A2D);
}

static inline void xtensa_emit_retw_n(uint8_t **ptr)
{
  xtensa_emit_u16(ptr, 0xF01D);
}

static inline void xtensa_emit_block_stub(uint8_t **ptr)
{
  xtensa_emit_entry_sp_32(ptr);
  xtensa_emit_l32r_imm_minus3(ptr, 2);
  xtensa_emit_l32r_imm_minus3(ptr, 3);
  xtensa_emit_mov_n_a10_a3(ptr);
  xtensa_emit_callx8_a2(ptr);
  xtensa_emit_mov_n_a2_a10(ptr);
  xtensa_emit_retw_n(ptr);
}

static inline void xtensa_emit_native_block_prologue(uint8_t **ptr)
{
  xtensa_emit_entry_sp_32(ptr);
}

static inline void xtensa_emit_native_arm_instruction(uint8_t **ptr,
                                                     const uint8_t *literal_base,
                                                     uint32_t insn_index)
{
  xtensa_emit_l32r_literal(ptr, 2, literal_base);
  xtensa_emit_movi_u15(ptr, 10, insn_index);
  xtensa_emit_callx8_a2(ptr);
  xtensa_emit_beqz_a10_skip_retw(ptr);
  xtensa_emit_retw_n(ptr);
}

#endif
