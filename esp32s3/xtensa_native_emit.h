#ifndef XTENSA_NATIVE_EMIT_H
#define XTENSA_NATIVE_EMIT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp32s3/xtensa_codegen.h"

enum
{
  XTENSA_LITERAL_HELPER = 0,
  XTENSA_LITERAL_REG_BASE = 4,
  XTENSA_LITERAL_CYCLES = 8
};

enum
{
  XTENSA_A_HELPER = 2,
  XTENSA_A_REG_BASE = 2,
  XTENSA_A_CYCLES_PTR = 3,
  XTENSA_A_DST = 4,
  XTENSA_A_RHS = 5,
  XTENSA_A_TMP = 6
};

enum
{
  XTENSA_ARM_REG_PC = 15
};

static inline uint32_t xtensa_native_ror32(uint32_t value, uint32_t shift)
{
  shift &= 31;
  if (shift == 0)
    return value;
  return (value >> shift) | (value << (32 - shift));
}

static inline bool xtensa_native_literal_pool_has(uint8_t *literal_base,
                                                  uint8_t *literal_cursor,
                                                  uint32_t words)
{
  return literal_cursor + (words * sizeof(uint32_t)) <=
         literal_base + XTENSA_BLOCK_LITERAL_BYTES;
}

static inline const uint8_t *xtensa_native_alloc_literal_u32(
  uint8_t **literal_cursor, uint32_t value)
{
  uint8_t *slot = *literal_cursor;
  xtensa_store_u32(slot, value);
  *literal_cursor += sizeof(uint32_t);
  return slot;
}

static inline void xtensa_native_emit_load_literal_u32(
  uint8_t **translation_ptr, uint32_t dst, uint8_t **literal_cursor,
  uint32_t value)
{
  const uint8_t *slot =
    xtensa_native_alloc_literal_u32(literal_cursor, value);
  xtensa_emit_l32r_literal(translation_ptr, dst, slot);
}

static inline void xtensa_native_emit_arm_cycle_update(
  uint8_t **translation_ptr, uint8_t *literal_base, uint8_t **literal_cursor,
  uint32_t cycles)
{
  if (cycles == 0)
    return;

  xtensa_emit_l32r_literal(translation_ptr, XTENSA_A_CYCLES_PTR,
                           literal_base + XTENSA_LITERAL_CYCLES);
  xtensa_emit_l32i(translation_ptr, XTENSA_A_TMP, XTENSA_A_CYCLES_PTR, 0);

  if (cycles <= 128)
  {
    xtensa_emit_addi(translation_ptr, XTENSA_A_TMP, XTENSA_A_TMP,
                     -(int32_t)cycles);
  }
  else
  {
    xtensa_native_emit_load_literal_u32(translation_ptr, XTENSA_A_RHS,
                                        literal_cursor, cycles);
    xtensa_emit_sub(translation_ptr, XTENSA_A_TMP, XTENSA_A_TMP,
                    XTENSA_A_RHS);
  }

  xtensa_emit_s32i(translation_ptr, XTENSA_A_TMP, XTENSA_A_CYCLES_PTR, 0);
}

static inline bool xtensa_emit_native_arm_data_proc_body(
  uint8_t **translation_ptr, uint8_t *literal_base, uint8_t **literal_cursor,
  uint32_t opcode, uint32_t pc, uint32_t cycles)
{
  uint32_t op = (opcode >> 21) & 0x0F;
  uint32_t rd = (opcode >> 12) & 0x0F;
  uint32_t rn = (opcode >> 16) & 0x0F;
  uint32_t rm = opcode & 0x0F;
  bool unary = (op == 0x0D) || (op == 0x0F);
  bool immediate = (opcode & (1u << 25)) != 0;
  uint32_t rhs_imm = 0;
  uint32_t literal_words = 1;

  if ((opcode >> 28) != 0x0E || (opcode & (1u << 20)) ||
      rd == XTENSA_ARM_REG_PC)
    return false;

  switch (op)
  {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F:
      break;

    default:
      return false;
  }

  if (!unary && rn == XTENSA_ARM_REG_PC)
    return false;

  if (immediate)
  {
    rhs_imm =
      xtensa_native_ror32(opcode & 0xFF, ((opcode >> 8) & 0x0F) * 2);
    literal_words++;
  }
  else
  {
    if ((opcode & 0x00000FF0) != 0 || rm == XTENSA_ARM_REG_PC)
      return false;
  }

  if (cycles > 128)
    literal_words++;

  if (!xtensa_native_literal_pool_has(literal_base, *literal_cursor,
                                      literal_words))
    return false;

  /* First native tier keeps ARM state memory-backed until stubs map regs. */
  xtensa_emit_l32r_literal(translation_ptr, XTENSA_A_REG_BASE,
                           literal_base + XTENSA_LITERAL_REG_BASE);

  if (!unary)
    xtensa_emit_l32i(translation_ptr, XTENSA_A_DST, XTENSA_A_REG_BASE,
                     rn * sizeof(uint32_t));

  if (immediate)
  {
    xtensa_native_emit_load_literal_u32(translation_ptr,
                                        unary ? XTENSA_A_DST : XTENSA_A_RHS,
                                        literal_cursor, rhs_imm);
  }
  else
  {
    xtensa_emit_l32i(translation_ptr, unary ? XTENSA_A_DST : XTENSA_A_RHS,
                     XTENSA_A_REG_BASE, rm * sizeof(uint32_t));
  }

  switch (op)
  {
    case 0x00:
      xtensa_emit_and(translation_ptr, XTENSA_A_DST, XTENSA_A_DST,
                      XTENSA_A_RHS);
      break;

    case 0x01:
      xtensa_emit_xor(translation_ptr, XTENSA_A_DST, XTENSA_A_DST,
                      XTENSA_A_RHS);
      break;

    case 0x02:
      xtensa_emit_sub(translation_ptr, XTENSA_A_DST, XTENSA_A_DST,
                      XTENSA_A_RHS);
      break;

    case 0x03:
      xtensa_emit_sub(translation_ptr, XTENSA_A_DST, XTENSA_A_RHS,
                      XTENSA_A_DST);
      break;

    case 0x04:
      xtensa_emit_add_n(translation_ptr, XTENSA_A_DST, XTENSA_A_DST,
                        XTENSA_A_RHS);
      break;

    case 0x0C:
      xtensa_emit_or(translation_ptr, XTENSA_A_DST, XTENSA_A_DST,
                     XTENSA_A_RHS);
      break;

    case 0x0E:
      xtensa_emit_movi(translation_ptr, XTENSA_A_TMP, (uint32_t)-1);
      xtensa_emit_xor(translation_ptr, XTENSA_A_RHS, XTENSA_A_RHS,
                      XTENSA_A_TMP);
      xtensa_emit_and(translation_ptr, XTENSA_A_DST, XTENSA_A_DST,
                      XTENSA_A_RHS);
      break;

    case 0x0F:
      xtensa_emit_movi(translation_ptr, XTENSA_A_TMP, (uint32_t)-1);
      xtensa_emit_xor(translation_ptr, XTENSA_A_DST, XTENSA_A_DST,
                      XTENSA_A_TMP);
      break;

    default:
      break;
  }

  xtensa_emit_s32i(translation_ptr, XTENSA_A_DST, XTENSA_A_REG_BASE,
                   rd * sizeof(uint32_t));
  xtensa_native_emit_load_literal_u32(translation_ptr, XTENSA_A_TMP,
                                      literal_cursor, pc + 4);
  xtensa_emit_s32i(translation_ptr, XTENSA_A_TMP, XTENSA_A_REG_BASE,
                   XTENSA_ARM_REG_PC * sizeof(uint32_t));
  xtensa_native_emit_arm_cycle_update(translation_ptr, literal_base,
                                      literal_cursor, cycles);
  return true;
}

#endif
