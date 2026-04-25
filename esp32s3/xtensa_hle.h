#ifndef XTENSA_HLE_H
#define XTENSA_HLE_H

#include <stdint.h>

typedef struct xtensa_hle_div_result
{
  uint32_t quotient;
  uint32_t remainder;
  uint32_t abs_quotient;
} xtensa_hle_div_result;

static inline xtensa_hle_div_result xtensa_hle_divide(uint32_t numerator,
                                                      uint32_t denominator)
{
  xtensa_hle_div_result result;
  int64_t signed_numerator = (int32_t)numerator;
  int64_t signed_denominator = (int32_t)denominator;
  int64_t quotient;
  int64_t remainder;

  /* Real BIOS loops forever here; avoid C division undefined behavior. */
  if (signed_denominator == 0)
  {
    result.quotient = 0;
    result.remainder = numerator;
    result.abs_quotient = 0;
    return result;
  }

  quotient = signed_numerator / signed_denominator;
  remainder = signed_numerator % signed_denominator;

  result.quotient = (uint32_t)(int32_t)quotient;
  result.remainder = (uint32_t)(int32_t)remainder;
  result.abs_quotient = (uint32_t)((quotient < 0) ? -quotient : quotient);
  return result;
}

#endif
