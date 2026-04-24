#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp32s3/xtensa_native_emit.h"

enum
{
  IMAGE_SIZE = 768U,
  ARM_HELPER = 0x40001000U,
  REG_BASE = 0x3FC90000U,
  CYCLES_PTR = 0x3FC91000U
};

typedef struct arm_sample
{
  const char *text;
  uint32_t opcode;
  uint32_t pc;
  uint32_t cycles;
} arm_sample;

static const arm_sample samples[] =
{
  {"add r4, r4, r5",       0xE0844005U, 0x08000100U, 1},
  {"mov r1, #0x80000000",  0xE3A01102U, 0x08000104U, 1},
  {"sub r6, r6, r7",       0xE0466007U, 0x08000108U, 2},
  {"orr r8, r8, r9",       0xE1888009U, 0x0800010CU, 1}
};

static int write_file(const char *path, const uint8_t *data, size_t size)
{
  FILE *fp = fopen(path, "wb");

  if (!fp)
  {
    perror("fopen");
    return -1;
  }

  if (fwrite(data, 1, size, fp) != size)
  {
    perror("fwrite");
    fclose(fp);
    return -1;
  }

  fclose(fp);
  return 0;
}

static int emit_native_arm_block(const char *path)
{
  uint32_t image_words[IMAGE_SIZE / sizeof(uint32_t)];
  uint8_t *image = (uint8_t *)image_words;
  uint8_t *literal_base;
  uint8_t *literal_cursor;
  uint8_t *translation_ptr;
  size_t i;

  memset(image_words, 0, sizeof(image_words));

  literal_base = xtensa_align_ptr(image);
  literal_cursor = literal_base + XTENSA_BLOCK_FIXED_LITERAL_BYTES;
  translation_ptr = literal_base + XTENSA_BLOCK_LITERAL_BYTES;

  xtensa_store_u32(literal_base + XTENSA_LITERAL_HELPER, ARM_HELPER);
  xtensa_store_u32(literal_base + XTENSA_LITERAL_REG_BASE, REG_BASE);
  xtensa_store_u32(literal_base + XTENSA_LITERAL_CYCLES, CYCLES_PTR);
  xtensa_store_u32(literal_base + 12, 0);

  xtensa_emit_native_block_prologue(&translation_ptr);

  printf("literal_base=0x%zx code_start=0x%zx\n",
         (size_t)(literal_base - image),
         (size_t)((literal_base + XTENSA_BLOCK_LITERAL_BYTES) - image));

  for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
  {
    size_t code_offset = (size_t)(translation_ptr - image);
    size_t literal_before = (size_t)(literal_cursor - image);

    if (!xtensa_emit_native_arm_data_proc_body(&translation_ptr, literal_base,
                                               &literal_cursor,
                                               samples[i].opcode,
                                               samples[i].pc,
                                               samples[i].cycles))
    {
      fprintf(stderr, "unsupported sample: %s\n", samples[i].text);
      return -1;
    }

    printf("0x%04zx ARM 0x%08x %-22s -> %zu Xtensa bytes, literals 0x%zx..0x%zx\n",
           code_offset, samples[i].opcode, samples[i].text,
           (size_t)(translation_ptr - image) - code_offset,
           literal_before, (size_t)(literal_cursor - image));
  }

  xtensa_emit_retw_n(&translation_ptr);

  printf("retw.n at 0x%04zx total=%zu\n",
         (size_t)(translation_ptr - image) - 2,
         (size_t)(translation_ptr - image));

  return write_file(path, image, (size_t)(translation_ptr - image));
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <output.bin>\n", argv[0]);
    return 1;
  }

  return emit_native_arm_block(argv[1]) == 0 ? 0 : 1;
}
