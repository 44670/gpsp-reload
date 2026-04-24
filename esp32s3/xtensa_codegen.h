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

#endif
