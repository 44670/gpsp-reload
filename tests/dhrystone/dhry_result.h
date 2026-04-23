#ifndef TESTS_DHRYSTONE_DHRY_RESULT_H
#define TESTS_DHRYSTONE_DHRY_RESULT_H

#include <stdint.h>

#define DHRY_RESULT_MAGIC 0x44524859u
#define DHRY_RESULT_ADDR  0x02000000u

typedef struct
{
  uint32_t magic;
  uint32_t status;
  uint32_t iterations;
  int32_t int_glob;
  int32_t bool_glob;
  uint32_t ch_1_glob;
  uint32_t ch_2_glob;
  int32_t arr_1_8;
  int32_t arr_2_8_7;
  int32_t ptr_int_comp;
  int32_t next_ptr_int_comp;
} dhry_result_t;

#define DHRY_STATUS_PASS 0u
#define DHRY_STATUS_FAIL 1u

#endif
