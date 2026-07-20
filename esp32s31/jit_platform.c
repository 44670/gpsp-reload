#include "esp32s31/jit_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_cache.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_memory_utils.h"

#include "common.h"
#include "cpu.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S31)
#error "ESP32-S31 JIT platform support requires CONFIG_IDF_TARGET_ESP32S31"
#endif

#if !CONFIG_SPIRAM_XIP_FROM_PSRAM
#error "ESP32-S31 PSRAM JIT requires CONFIG_SPIRAM_XIP_FROM_PSRAM=y"
#endif

#ifndef CONFIG_CACHE_L1_ICACHE_LINE_SIZE
#error "ESP32-S31 JIT cache sync requires the L1 I-cache line size"
#endif

static const char *TAG = "gpsp-jit";

_Static_assert((CONFIG_CACHE_L1_ICACHE_LINE_SIZE &
                (CONFIG_CACHE_L1_ICACHE_LINE_SIZE - 1)) == 0,
               "I-cache line size must be a power of two");

bool esp32s31_jit_cache_sync(void *start_ptr, void *end_ptr)
{
  uintptr_t start = (uintptr_t)start_ptr;
  uintptr_t end = (uintptr_t)end_ptr;
  const uintptr_t line_size = CONFIG_CACHE_L1_ICACHE_LINE_SIZE;

  if (end <= start)
    return true;
  if (!esp_ptr_external_ram(start_ptr) ||
      !esp_ptr_external_ram((const void *)(end - 1u)))
  {
    ESP_LOGE(TAG, "JIT sync range is not entirely in PSRAM: %p..%p",
             start_ptr, end_ptr);
    return false;
  }

  esp_err_t error = esp_cache_msync(
      start_ptr, end - start,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M |
          ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
          ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "JIT data writeback failed: %s",
             esp_err_to_name(error));
    return false;
  }

  const uintptr_t aligned_start = start & ~(line_size - 1u);
  const uintptr_t aligned_end =
      (end + line_size - 1u) & ~(line_size - 1u);
  error = esp_cache_msync(
      (void *)aligned_start, aligned_end - aligned_start,
      ESP_CACHE_MSYNC_FLAG_DIR_M2C |
          ESP_CACHE_MSYNC_FLAG_TYPE_INST);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "JIT instruction invalidate failed: %s",
             esp_err_to_name(error));
    return false;
  }

  __asm__ __volatile__("fence.i" ::: "memory");
  return true;
}

bool esp32s31_jit_cache_selftest(esp32s31_jit_selftest_result_t *result)
{
  typedef uint32_t (*selftest_fn_t)(void);
  uint32_t saved[2];
  uint32_t *code = (uint32_t *)rom_translation_cache;

  if (result == NULL)
    return false;

  memset(result, 0, sizeof(*result));
  result->rom_cache_address = (uintptr_t)rom_translation_cache;
  result->ram_cache_address = (uintptr_t)ram_translation_cache;
  result->rom_cache_external = esp_ptr_external_ram(rom_translation_cache);
  result->ram_cache_external = esp_ptr_external_ram(ram_translation_cache);
  if (!result->rom_cache_external || !result->ram_cache_external ||
      (((uintptr_t)code & 3u) != 0u))
    return false;

  saved[0] = code[0];
  saved[1] = code[1];

  /* addi a0, zero, 42; jalr zero, 0(ra) */
  code[0] = UINT32_C(0x02a00513);
  code[1] = UINT32_C(0x00008067);
  if (!esp32s31_jit_cache_sync(code, code + 2))
    return false;

  result->return_value = ((selftest_fn_t)(uintptr_t)code)();

  /* A first execution only proves that PSRAM is executable.  Reuse the same
   * I-cache line to prove that runtime block replacement is actually visible
   * to instruction fetch as required by translation-cache flush/reuse. */
  code[0] = UINT32_C(0x02b00513); /* addi a0, zero, 43 */
  if (!esp32s31_jit_cache_sync(code, code + 2))
    return false;
  result->patched_return_value = ((selftest_fn_t)(uintptr_t)code)();

  code[0] = saved[0];
  code[1] = saved[1];
  if (!esp32s31_jit_cache_sync(code, code + 2))
    return false;

  return result->return_value == 42u && result->patched_return_value == 43u;
}
