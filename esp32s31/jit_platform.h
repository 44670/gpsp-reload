#ifndef GPSP_ESP32S31_JIT_PLATFORM_H
#define GPSP_ESP32S31_JIT_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP32S31_JIT_CACHE_ALIGNMENT 0x1000u

typedef struct esp32s31_jit_selftest_result
{
  uintptr_t rom_cache_address;
  uintptr_t ram_cache_address;
  uint32_t return_value;
  uint32_t patched_return_value;
  bool rom_cache_external;
  bool ram_cache_external;
} esp32s31_jit_selftest_result_t;

bool esp32s31_jit_cache_sync(void *start, void *end);
bool esp32s31_jit_cache_selftest(esp32s31_jit_selftest_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
