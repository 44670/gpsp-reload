#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp32s3/xtensa_codegen.h"
#include "esp32s3/xtensa_hle.h"
#include "esp32s3/xtensa_native_emit.h"

static unsigned failures;

static uint32_t load_u32_le(const uint8_t *ptr)
{
  uint32_t value;
  memcpy(&value, ptr, sizeof(value));
  return value;
}

static void print_bytes(const uint8_t *bytes, size_t len)
{
  size_t i;

  for (i = 0; i < len; i++)
    printf("%s%02x", i == 0 ? "" : " ", bytes[i]);
}

static void expect_true(const char *name, bool actual)
{
  if (!actual)
  {
    printf("FAIL %s: expected true\n", name);
    failures++;
  }
}

static void expect_false(const char *name, bool actual)
{
  if (actual)
  {
    printf("FAIL %s: expected false\n", name);
    failures++;
  }
}

static void expect_size(const char *name, size_t actual, size_t expected)
{
  if (actual != expected)
  {
    printf("FAIL %s: expected %zu got %zu\n", name, expected, actual);
    failures++;
  }
}

static void expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
  if (actual != expected)
  {
    printf("FAIL %s: expected 0x%08x got 0x%08x\n",
           name, expected, actual);
    failures++;
  }
}

static void expect_bytes(const char *name, const uint8_t *actual,
                         size_t actual_len, const uint8_t *expected,
                         size_t expected_len)
{
  if (actual_len == expected_len &&
      memcmp(actual, expected, expected_len) == 0)
    return;

  printf("FAIL %s:\n  expected: ", name);
  print_bytes(expected, expected_len);
  printf("\n  actual:   ");
  print_bytes(actual, actual_len);
  printf("\n");
  failures++;
}

static bool contains_bytes(const uint8_t *haystack, size_t haystack_len,
                           const uint8_t *needle, size_t needle_len)
{
  size_t i;

  if (needle_len > haystack_len)
    return false;

  for (i = 0; i <= haystack_len - needle_len; i++)
  {
    if (memcmp(haystack + i, needle, needle_len) == 0)
      return true;
  }

  return false;
}

static void test_low_level_encoders(void)
{
  uint8_t buf[16];
  uint8_t *ptr;
  static const uint8_t l32i_a4_a5_64[] = {0x42, 0x25, 0x10};
  static const uint8_t s32i_a4_a5_64[] = {0x42, 0x65, 0x10};
  static const uint8_t add_n_a4_a4_a5[] = {0x5A, 0x44};
  static const uint8_t sub_a4_a4_a5[] = {0x50, 0x44, 0xC0};
  static const uint8_t and_a4_a4_a5[] = {0x50, 0x44, 0x10};
  static const uint8_t or_a4_a4_a5[] = {0x50, 0x44, 0x20};
  static const uint8_t xor_a4_a4_a5[] = {0x50, 0x44, 0x30};
  static const uint8_t extui_a6_a6_29_1[] = {0x60, 0x6D, 0x05};
  static const uint8_t mov_n_a10_a2[] = {0xAD, 0x02};
  static const uint8_t callx8_a4[] = {0xE0, 0x04, 0x00};

  ptr = buf;
  xtensa_emit_l32i(&ptr, 4, 5, 64);
  expect_bytes("l32i a4,a5,64", buf, (size_t)(ptr - buf),
               l32i_a4_a5_64, sizeof(l32i_a4_a5_64));

  ptr = buf;
  xtensa_emit_s32i(&ptr, 4, 5, 64);
  expect_bytes("s32i a4,a5,64", buf, (size_t)(ptr - buf),
               s32i_a4_a5_64, sizeof(s32i_a4_a5_64));

  ptr = buf;
  xtensa_emit_add_n(&ptr, 4, 4, 5);
  expect_bytes("add.n a4,a4,a5", buf, (size_t)(ptr - buf),
               add_n_a4_a4_a5, sizeof(add_n_a4_a4_a5));

  ptr = buf;
  xtensa_emit_sub(&ptr, 4, 4, 5);
  expect_bytes("sub a4,a4,a5", buf, (size_t)(ptr - buf),
               sub_a4_a4_a5, sizeof(sub_a4_a4_a5));

  ptr = buf;
  xtensa_emit_and(&ptr, 4, 4, 5);
  expect_bytes("and a4,a4,a5", buf, (size_t)(ptr - buf),
               and_a4_a4_a5, sizeof(and_a4_a4_a5));

  ptr = buf;
  xtensa_emit_or(&ptr, 4, 4, 5);
  expect_bytes("or a4,a4,a5", buf, (size_t)(ptr - buf),
               or_a4_a4_a5, sizeof(or_a4_a4_a5));

  ptr = buf;
  xtensa_emit_xor(&ptr, 4, 4, 5);
  expect_bytes("xor a4,a4,a5", buf, (size_t)(ptr - buf),
               xor_a4_a4_a5, sizeof(xor_a4_a4_a5));

  ptr = buf;
  xtensa_emit_extui(&ptr, 6, 6, 29, 1);
  expect_bytes("extui a6,a6,29,1", buf, (size_t)(ptr - buf),
               extui_a6_a6_29_1, sizeof(extui_a6_a6_29_1));

  ptr = buf;
  xtensa_emit_mov_n(&ptr, 10, 2);
  expect_bytes("mov.n a10,a2", buf, (size_t)(ptr - buf),
               mov_n_a10_a2, sizeof(mov_n_a10_a2));

  ptr = buf;
  xtensa_emit_callx8(&ptr, 4);
  expect_bytes("callx8 a4", buf, (size_t)(ptr - buf),
               callx8_a4, sizeof(callx8_a4));
}

static void test_state_layout(void)
{
  expect_size("state size", sizeof(xtensa_jit_state), OFF_STATE_SIZE);
  expect_size("state r0 offset", offsetof(xtensa_jit_state, r), OFF_R0);
  expect_size("state spsr offset", offsetof(xtensa_jit_state, spsr),
              OFF_SPSR);
  expect_size("state reg_mode offset", offsetof(xtensa_jit_state, reg_mode),
              OFF_REG_MODE);
  expect_size("state cycles offset", offsetof(xtensa_jit_state, jit_cycles),
              OFF_JIT_CYCLES);
  expect_u32("OFF_PC aliases OFF_R15", OFF_PC, OFF_R15);
}

static void test_hle_division(void)
{
  xtensa_hle_div_result result;

  result = xtensa_hle_divide((uint32_t)(int32_t)-1234, 10);
  expect_u32("hle Div quotient", result.quotient, (uint32_t)(int32_t)-123);
  expect_u32("hle Div remainder", result.remainder, (uint32_t)(int32_t)-4);
  expect_u32("hle Div abs quotient", result.abs_quotient, 123);

  result = xtensa_hle_divide(10, (uint32_t)(int32_t)-3);
  expect_u32("hle Div negative denominator quotient", result.quotient,
             (uint32_t)(int32_t)-3);
  expect_u32("hle Div negative denominator remainder", result.remainder, 1);
  expect_u32("hle Div negative denominator abs", result.abs_quotient, 3);

  result = xtensa_hle_divide(0x80000000U, 0xFFFFFFFFU);
  expect_u32("hle Div overflow quotient", result.quotient, 0x80000000U);
  expect_u32("hle Div overflow remainder", result.remainder, 0);
  expect_u32("hle Div overflow abs", result.abs_quotient, 0x80000000U);
}

static void test_helper_stub_layout(void)
{
  uint32_t image_words[160];
  uint8_t *literal_base = (uint8_t *)image_words;
  uint8_t *code = literal_base + XTENSA_BLOCK_LITERAL_BYTES;
  uint8_t *ptr = code;
  static const uint8_t expected[] =
  {
    0x36, 0x41, 0x00,
    0x21, 0xC0, 0xFF,
    0x41, 0xBE, 0xFF,
    0xAD, 0x02,
    0xB2, 0xA0, 0x07,
    0xE0, 0x04, 0x00,
    0x16, 0x1A, 0x00,
    0x1D, 0xF0,
    0x1D, 0xF0
  };

  memset(image_words, 0, sizeof(image_words));
  xtensa_emit_native_block_prologue(&ptr, literal_base);
  xtensa_emit_native_arm_instruction(&ptr, literal_base, 7);
  xtensa_emit_retw_n(&ptr);

  expect_size("fixed literal bytes", XTENSA_BLOCK_FIXED_LITERAL_BYTES, 16);
  expect_size("block literal bytes", XTENSA_BLOCK_LITERAL_BYTES, 256);
  expect_bytes("helper stub after literal area", code, (size_t)(ptr - code),
               expected, sizeof(expected));
}

static void test_native_arm_add_register(void)
{
  uint32_t image_words[160];
  uint8_t *literal_base = (uint8_t *)image_words;
  uint8_t *literal_cursor = literal_base + XTENSA_BLOCK_FIXED_LITERAL_BYTES;
  uint8_t *code = literal_base + XTENSA_BLOCK_LITERAL_BYTES;
  uint8_t *ptr = code;
  bool emitted;
  static const uint8_t callx8_a4[] = {0xE0, 0x04, 0x00};
  static const uint8_t expected[] =
  {
    0x42, 0x22, 0x04,
    0x52, 0x22, 0x05,
    0x5A, 0x44,
    0x42, 0x62, 0x04,
    0x61, 0xC1, 0xFF,
    0x62, 0x62, 0x0F,
    0x62, 0x22, 0x57,
    0x62, 0xC6, 0xFD,
    0x62, 0x62, 0x57
  };

  memset(image_words, 0, sizeof(image_words));
  emitted = xtensa_emit_native_arm_data_proc_body(&ptr, literal_base,
                                                  &literal_cursor,
                                                  0xE0844005, 0x08000100,
                                                  3);

  expect_true("native ADD r4,r4,r5 accepted", emitted);
  expect_bytes("native ADD r4,r4,r5 bytes", code, (size_t)(ptr - code),
               expected, sizeof(expected));
  expect_size("native ADD literal cursor",
              (size_t)(literal_cursor - literal_base), 20);
  expect_u32("native ADD pc literal", load_u32_le(literal_base + 16),
             0x08000104);
  expect_false("native ADD does not call helper",
               contains_bytes(code, (size_t)(ptr - code),
                              callx8_a4, sizeof(callx8_a4)));
}

static void test_native_arm_immediate_literal(void)
{
  uint32_t image_words[160];
  uint8_t *literal_base = (uint8_t *)image_words;
  uint8_t *literal_cursor = literal_base + XTENSA_BLOCK_FIXED_LITERAL_BYTES;
  uint8_t *code = literal_base + XTENSA_BLOCK_LITERAL_BYTES;
  uint8_t *ptr = code;
  bool emitted;

  memset(image_words, 0, sizeof(image_words));
  emitted = xtensa_emit_native_arm_data_proc_body(&ptr, literal_base,
                                                  &literal_cursor,
                                                  0xE3A01102, 0x08000200,
                                                  0);

  expect_true("native MOV r1,#0x80000000 accepted", emitted);
  expect_size("native MOV immediate literal cursor",
              (size_t)(literal_cursor - literal_base), 24);
  expect_u32("native MOV immediate literal", load_u32_le(literal_base + 16),
             0x80000000);
  expect_u32("native MOV pc literal", load_u32_le(literal_base + 20),
             0x08000204);
  expect_size("native MOV no cycle update length", (size_t)(ptr - code), 12);
}

static void test_native_arm_carry_ops(void)
{
  uint32_t image_words[160];
  uint8_t *literal_base = (uint8_t *)image_words;
  uint8_t *literal_cursor = literal_base + XTENSA_BLOCK_FIXED_LITERAL_BYTES;
  uint8_t *code = literal_base + XTENSA_BLOCK_LITERAL_BYTES;
  uint8_t *ptr = code;
  bool emitted;
  static const uint8_t callx8_a4[] = {0xE0, 0x04, 0x00};
  static const uint8_t extui_cpsr_c[] = {0x60, 0x6D, 0x05};

  memset(image_words, 0, sizeof(image_words));
  emitted = xtensa_emit_native_arm_data_proc_body(&ptr, literal_base,
                                                  &literal_cursor,
                                                  0xE0A22003, 0x08000400,
                                                  0);

  expect_true("native ADC r2,r2,r3 accepted", emitted);
  expect_true("native ADC extracts CPSR C",
              contains_bytes(code, (size_t)(ptr - code),
                             extui_cpsr_c, sizeof(extui_cpsr_c)));
  expect_false("native ADC does not call helper",
               contains_bytes(code, (size_t)(ptr - code),
                              callx8_a4, sizeof(callx8_a4)));
  expect_size("native ADC literal cursor",
              (size_t)(literal_cursor - literal_base), 20);
  expect_u32("native ADC pc literal", load_u32_le(literal_base + 16),
             0x08000404);

  literal_cursor = literal_base + XTENSA_BLOCK_FIXED_LITERAL_BYTES;
  ptr = code;
  memset(image_words, 0, sizeof(image_words));
  emitted = xtensa_emit_native_arm_data_proc_body(&ptr, literal_base,
                                                  &literal_cursor,
                                                  0xE2C44001, 0x08000404,
                                                  0);

  expect_true("native SBC r4,r4,#1 accepted", emitted);
  expect_true("native SBC extracts CPSR C",
              contains_bytes(code, (size_t)(ptr - code),
                             extui_cpsr_c, sizeof(extui_cpsr_c)));
  expect_size("native SBC literal cursor",
              (size_t)(literal_cursor - literal_base), 24);
  expect_u32("native SBC immediate literal", load_u32_le(literal_base + 16),
             1);
  expect_u32("native SBC pc literal", load_u32_le(literal_base + 20),
             0x08000408);
}

static void expect_rejected_opcode(const char *name, uint32_t opcode)
{
  uint32_t image_words[160];
  uint8_t *literal_base = (uint8_t *)image_words;
  uint8_t *literal_cursor = literal_base + XTENSA_BLOCK_FIXED_LITERAL_BYTES;
  uint8_t *code = literal_base + XTENSA_BLOCK_LITERAL_BYTES;
  uint8_t *ptr = code;

  memset(image_words, 0, sizeof(image_words));
  expect_false(name,
               xtensa_emit_native_arm_data_proc_body(&ptr, literal_base,
                                                     &literal_cursor,
                                                     opcode, 0x08000300,
                                                     1));
  expect_size(name, (size_t)(ptr - code), 0);
  expect_size(name, (size_t)(literal_cursor - literal_base),
              XTENSA_BLOCK_FIXED_LITERAL_BYTES);
}

static void test_native_arm_rejects_unsupported(void)
{
  expect_rejected_opcode("reject non-AL condition", 0x10844005);
  expect_rejected_opcode("reject flag-setting ADD", 0xE0944005);
  expect_rejected_opcode("reject rd=pc", 0xE084F005);
  expect_rejected_opcode("reject shifted rm", 0xE0844085);
  expect_rejected_opcode("reject rm=pc", 0xE084400F);
}

int main(void)
{
  test_low_level_encoders();
  test_state_layout();
  test_hle_division();
  test_helper_stub_layout();
  test_native_arm_add_register();
  test_native_arm_immediate_literal();
  test_native_arm_carry_ops();
  test_native_arm_rejects_unsupported();

  if (failures != 0)
  {
    printf("%u test failure(s)\n", failures);
    return 1;
  }

  printf("xtensa_codegen_tests: PASS\n");
  return 0;
}
