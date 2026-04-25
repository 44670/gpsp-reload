#ifndef XTENSA_CODEGEN_H
#define XTENSA_CODEGEN_H

#include <stdint.h>
#include <string.h>

#include "esp32s3/xtensa_state.h"

#define XTENSA_BLOCK_FIXED_LITERAL_BYTES 16
#define XTENSA_BLOCK_LITERAL_BYTES 256

enum
{
  XTENSA_LITERAL_HELPER = 0,
  XTENSA_LITERAL_STATE = 4,
  XTENSA_LITERAL_RESERVED0 = 8,
  XTENSA_LITERAL_RESERVED1 = 12
};

enum
{
  XTENSA_NATIVE_A_STATE = 2,
  XTENSA_NATIVE_A_PC = 3
};

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

static inline void xtensa_emit_l32i(uint8_t **ptr, uint32_t dst,
                                    uint32_t base, uint32_t offset)
{
  xtensa_emit_u8(ptr, (uint8_t)((dst << 4) | 0x02));
  xtensa_emit_u8(ptr, (uint8_t)(0x20 | base));
  xtensa_emit_u8(ptr, (uint8_t)(offset >> 2));
}

static inline void xtensa_emit_s32i(uint8_t **ptr, uint32_t src,
                                    uint32_t base, uint32_t offset)
{
  xtensa_emit_u8(ptr, (uint8_t)((src << 4) | 0x02));
  xtensa_emit_u8(ptr, (uint8_t)(0x60 | base));
  xtensa_emit_u8(ptr, (uint8_t)(offset >> 2));
}

static inline void xtensa_emit_add_n(uint8_t **ptr, uint32_t dst,
                                     uint32_t src_a, uint32_t src_b)
{
  xtensa_emit_u16(ptr, (uint16_t)((dst << 12) | (src_a << 8) |
                                  (src_b << 4) | 0x0A));
}

static inline void xtensa_emit_sub(uint8_t **ptr, uint32_t dst,
                                   uint32_t src_a, uint32_t src_b)
{
  xtensa_emit_u8(ptr, (uint8_t)(src_b << 4));
  xtensa_emit_u8(ptr, (uint8_t)((dst << 4) | src_a));
  xtensa_emit_u8(ptr, 0xC0);
}

static inline void xtensa_emit_and(uint8_t **ptr, uint32_t dst,
                                   uint32_t src_a, uint32_t src_b)
{
  xtensa_emit_u8(ptr, (uint8_t)(src_b << 4));
  xtensa_emit_u8(ptr, (uint8_t)((dst << 4) | src_a));
  xtensa_emit_u8(ptr, 0x10);
}

static inline void xtensa_emit_or(uint8_t **ptr, uint32_t dst,
                                  uint32_t src_a, uint32_t src_b)
{
  xtensa_emit_u8(ptr, (uint8_t)(src_b << 4));
  xtensa_emit_u8(ptr, (uint8_t)((dst << 4) | src_a));
  xtensa_emit_u8(ptr, 0x20);
}

static inline void xtensa_emit_xor(uint8_t **ptr, uint32_t dst,
                                   uint32_t src_a, uint32_t src_b)
{
  xtensa_emit_u8(ptr, (uint8_t)(src_b << 4));
  xtensa_emit_u8(ptr, (uint8_t)((dst << 4) | src_a));
  xtensa_emit_u8(ptr, 0x30);
}

static inline void xtensa_emit_extui(uint8_t **ptr, uint32_t dst,
                                     uint32_t src, uint32_t pos,
                                     uint32_t width)
{
  xtensa_emit_u8(ptr, (uint8_t)(src << 4));
  xtensa_emit_u8(ptr, (uint8_t)((dst << 4) | (pos & 0x0F)));
  xtensa_emit_u8(ptr, (uint8_t)((((width - 1) & 0x0F) << 4) |
                                0x04 | ((pos >> 4) & 0x01)));
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

static inline void xtensa_emit_mov_n(uint8_t **ptr, uint32_t dst,
                                     uint32_t src)
{
  xtensa_emit_u16(ptr, (uint16_t)((src << 8) | (dst << 4) | 0x0D));
}

static inline void xtensa_emit_callx8(uint8_t **ptr, uint32_t reg_num)
{
  xtensa_emit_u8(ptr, 0xE0);
  xtensa_emit_u8(ptr, (uint8_t)reg_num);
  xtensa_emit_u8(ptr, 0x00);
}

static inline void xtensa_emit_callx8_a2(uint8_t **ptr)
{
  xtensa_emit_callx8(ptr, 2);
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

static inline void xtensa_emit_native_block_prologue(uint8_t **ptr,
                                                    const uint8_t *literal_base)
{
  xtensa_emit_entry_sp_32(ptr);
  /* a2 is the fixed CPU/JIT state base for the whole translated block. */
  xtensa_emit_l32r_literal(ptr, XTENSA_NATIVE_A_STATE,
                           literal_base + XTENSA_LITERAL_STATE);
  xtensa_emit_l32i(ptr, XTENSA_NATIVE_A_PC, XTENSA_NATIVE_A_STATE, OFF_PC);
}

static inline void xtensa_emit_native_arm_instruction(uint8_t **ptr,
                                                     const uint8_t *literal_base,
                                                     uint32_t insn_index)
{
  xtensa_emit_l32r_literal(ptr, 4, literal_base + XTENSA_LITERAL_HELPER);
  xtensa_emit_s32i(ptr, XTENSA_NATIVE_A_PC, XTENSA_NATIVE_A_STATE, OFF_PC);
  xtensa_emit_mov_n(ptr, 10, XTENSA_NATIVE_A_STATE);
  xtensa_emit_movi_u15(ptr, 11, insn_index);
  xtensa_emit_callx8(ptr, 4);
  xtensa_emit_beqz_a10_skip_retw(ptr);
  xtensa_emit_retw_n(ptr);
  xtensa_emit_l32i(ptr, XTENSA_NATIVE_A_PC, XTENSA_NATIVE_A_STATE, OFF_PC);
}

#endif
