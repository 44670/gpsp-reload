#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp32s3/xtensa_codegen.h"

enum
{
  IMAGE_BASE = 0x3F400000U,
  IMAGE_SIZE = 64U,
  ARM_HELPER = 0x40001000U,
  THUMB_HELPER = 0x40002000U,
  TEST_INSN_INDEX = 7U
};

static int emit_block_image(uint8_t *image, size_t image_size, int thumb_mode,
                            size_t *total_size, size_t *code_offset,
                            size_t *literal_offset)
{
  uint8_t *literal_base;
  uint8_t *translation_ptr;

  memset(image, 0, image_size);

  literal_base = xtensa_align_ptr(image);
  translation_ptr = literal_base + XTENSA_BLOCK_LITERAL_BYTES;
  xtensa_emit_native_block_prologue(&translation_ptr);
  xtensa_emit_native_arm_instruction(&translation_ptr, literal_base,
                                     TEST_INSN_INDEX);
  xtensa_emit_retw_n(&translation_ptr);

  xtensa_store_u32(literal_base + 0, thumb_mode ? THUMB_HELPER : ARM_HELPER);
  xtensa_store_u32(literal_base + 4, 0);

  *total_size = (size_t)(translation_ptr - image);
  *code_offset = (size_t)((literal_base + XTENSA_BLOCK_LITERAL_BYTES) - image);
  *literal_offset = (size_t)(literal_base - image);
  return 0;
}

int main(int argc, char **argv)
{
  FILE *fp;
  uint8_t image[IMAGE_SIZE];
  size_t total_size;
  size_t code_offset;
  size_t literal_offset;
  int thumb_mode;

  if (argc != 3)
  {
    fprintf(stderr, "usage: %s <arm|thumb> <output.bin>\n", argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "arm") == 0)
    thumb_mode = 0;
  else if (strcmp(argv[1], "thumb") == 0)
    thumb_mode = 1;
  else
  {
    fprintf(stderr, "unknown block kind: %s\n", argv[1]);
    return 1;
  }

  if (emit_block_image(image, sizeof(image), thumb_mode,
                       &total_size, &code_offset, &literal_offset) != 0)
  {
    fprintf(stderr, "failed to emit test block\n");
    return 1;
  }

  fp = fopen(argv[2], "wb");
  if (!fp)
  {
    perror("fopen");
    return 1;
  }

  if (fwrite(image, 1, total_size, fp) != total_size)
  {
    perror("fwrite");
    fclose(fp);
    return 1;
  }

  fclose(fp);

  printf("emitted %s block: total=%zu code_offset=0x%zx literal_offset=0x%zx helper=0x%08x insn_index=%u\n",
         argv[1], total_size, code_offset, literal_offset,
         thumb_mode ? THUMB_HELPER : ARM_HELPER,
         (unsigned)TEST_INSN_INDEX);

  return 0;
}
