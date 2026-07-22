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
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
#include "esp32s31/psram_fault_trace.h"
#endif

#if !defined(CONFIG_IDF_TARGET_ESP32S31)
#error "ESP32-S31 JIT platform support requires CONFIG_IDF_TARGET_ESP32S31"
#endif

#if !CONFIG_SPIRAM
#error "ESP32-S31 PSRAM JIT requires CONFIG_SPIRAM=y"
#endif

#if !defined(CONFIG_SPIRAM_SPEED) || CONFIG_SPIRAM_SPEED != 250
#error "ESP32-S31 PSRAM JIT release requires 250 MHz PSRAM"
#endif

#if !defined(CONFIG_SPIRAM_XIP_FROM_PSRAM) || \
    !CONFIG_SPIRAM_XIP_FROM_PSRAM || \
    !defined(CONFIG_SPIRAM_FETCH_INSTRUCTIONS) || \
    !CONFIG_SPIRAM_FETCH_INSTRUCTIONS || \
    !defined(CONFIG_SPIRAM_RODATA) || !CONFIG_SPIRAM_RODATA
#error "ESP32-S31 dynarec release requires application .text/.rodata XIP from PSRAM"
#endif

#ifndef CONFIG_CACHE_L1_ICACHE_LINE_SIZE
#error "ESP32-S31 JIT cache sync requires the L1 I-cache line size"
#endif

static const char *TAG = "gpsp-jit";
static const uint8_t s_app_rodata_probe[] = "gpsp-app-rodata-probe";

_Static_assert((CONFIG_CACHE_L1_ICACHE_LINE_SIZE &
                (CONFIG_CACHE_L1_ICACHE_LINE_SIZE - 1)) == 0,
               "I-cache line size must be a power of two");

static bool execute_cache_probe(uint32_t *code, uint32_t *return_value,
                                uint32_t *patched_return_value)
{
  typedef uint32_t (*selftest_fn_t)(void);
  const uint32_t saved0 = code[0];
  const uint32_t saved1 = code[1];

  /* addi a0, zero, 42; jalr zero, 0(ra) */
  code[0] = UINT32_C(0x02a00513);
  code[1] = UINT32_C(0x00008067);
  if (!esp32s31_jit_cache_sync(code, code + 2))
    return false;

  *return_value = ((selftest_fn_t)(uintptr_t)code)();

  /* Reuse the same I-cache line to prove that a later block replacement is
   * visible to instruction fetch, not merely that initial PSRAM XIP works. */
  code[0] = UINT32_C(0x02b00513); /* addi a0, zero, 43 */
  if (!esp32s31_jit_cache_sync(code, code + 2))
    return false;
  *patched_return_value = ((selftest_fn_t)(uintptr_t)code)();

  code[0] = saved0;
  code[1] = saved1;
  if (!esp32s31_jit_cache_sync(code, code + 2))
    return false;

  return *return_value == 42u && *patched_return_value == 43u;
}

bool esp32s31_jit_cache_sync(void *start_ptr, void *end_ptr)
{
  uintptr_t start = (uintptr_t)start_ptr;
  uintptr_t end = (uintptr_t)end_ptr;
  const uintptr_t line_size = CONFIG_CACHE_L1_ICACHE_LINE_SIZE;

#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
  esp32s31_psram_fault_trace_note_cache_sync(
      ESP32S31_PSRAM_FAULT_SYNC_BEGIN, start, end, ESP_OK);
#endif

  if (end <= start)
  {
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
    esp32s31_psram_fault_trace_note_cache_sync(
        ESP32S31_PSRAM_FAULT_SYNC_DONE, start, end, ESP_OK);
#endif
    return true;
  }
  if (!esp_ptr_external_ram(start_ptr) ||
      !esp_ptr_external_ram((const void *)(end - 1u)))
  {
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
    esp32s31_psram_fault_trace_note_cache_sync(
        ESP32S31_PSRAM_FAULT_SYNC_BAD_RANGE, start, end,
        ESP_ERR_INVALID_ARG);
#endif
    ESP_LOGE(TAG, "JIT sync range is not entirely in PSRAM: %p..%p",
             start_ptr, end_ptr);
    return false;
  }

#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
  esp32s31_psram_fault_trace_note_cache_sync(
      ESP32S31_PSRAM_FAULT_SYNC_DATA_WRITEBACK, start, end, ESP_OK);
#endif
  esp_err_t error = esp_cache_msync(
      start_ptr, end - start,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M |
          ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
          ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (error != ESP_OK)
  {
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
    esp32s31_psram_fault_trace_note_cache_sync(
        ESP32S31_PSRAM_FAULT_SYNC_DATA_ERROR, start, end, error);
#endif
    ESP_LOGE(TAG, "JIT data writeback failed: %s",
             esp_err_to_name(error));
    return false;
  }
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
  esp32s31_psram_fault_trace_note_cache_sync(
      ESP32S31_PSRAM_FAULT_SYNC_DATA_DONE, start, end, ESP_OK);
#endif

  const uintptr_t aligned_start = start & ~(line_size - 1u);
  const uintptr_t aligned_end =
      (end + line_size - 1u) & ~(line_size - 1u);
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
  esp32s31_psram_fault_trace_note_cache_sync(
      ESP32S31_PSRAM_FAULT_SYNC_INST_INVALIDATE,
      aligned_start, aligned_end, ESP_OK);
#endif
  error = esp_cache_msync(
      (void *)aligned_start, aligned_end - aligned_start,
      ESP_CACHE_MSYNC_FLAG_DIR_M2C |
          ESP_CACHE_MSYNC_FLAG_TYPE_INST);
  if (error != ESP_OK)
  {
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
    esp32s31_psram_fault_trace_note_cache_sync(
        ESP32S31_PSRAM_FAULT_SYNC_INST_ERROR,
        aligned_start, aligned_end, error);
#endif
    ESP_LOGE(TAG, "JIT instruction invalidate failed: %s",
             esp_err_to_name(error));
    return false;
  }
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
  esp32s31_psram_fault_trace_note_cache_sync(
      ESP32S31_PSRAM_FAULT_SYNC_INST_DONE,
      aligned_start, aligned_end, ESP_OK);
  esp32s31_psram_fault_trace_note_cache_sync(
      ESP32S31_PSRAM_FAULT_SYNC_FENCE,
      aligned_start, aligned_end, ESP_OK);
#endif

  __asm__ __volatile__("fence.i" ::: "memory");
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
  esp32s31_psram_fault_trace_note_cache_sync(
      ESP32S31_PSRAM_FAULT_SYNC_DONE,
      aligned_start, aligned_end, ESP_OK);
#endif
  return true;
}

bool esp32s31_jit_cache_selftest(esp32s31_jit_selftest_result_t *result)
{
  if (result == NULL)
    return false;

  memset(result, 0, sizeof(*result));
  result->rom_cache_address = (uintptr_t)rom_translation_cache;
  result->ram_cache_address = (uintptr_t)ram_translation_cache;
  result->app_text_address = (uintptr_t)esp32s31_jit_cache_sync;
  result->app_rodata_address = (uintptr_t)s_app_rodata_probe;
  result->rom_cache_external = esp_ptr_external_ram(rom_translation_cache);
  result->ram_cache_external = esp_ptr_external_ram(ram_translation_cache);
  result->rom_cache_executable = esp_ptr_executable(rom_translation_cache);
  result->ram_cache_executable = esp_ptr_executable(ram_translation_cache);
  result->app_text_external = esp_ptr_external_ram(
      (const void *)result->app_text_address);
  result->app_rodata_external = esp_ptr_external_ram(s_app_rodata_probe);
  if (!result->rom_cache_external || !result->ram_cache_external ||
      !result->rom_cache_executable || !result->ram_cache_executable ||
      !result->app_text_external || !result->app_rodata_external ||
      (((uintptr_t)rom_translation_cache &
        (ESP32S31_JIT_CACHE_ALIGNMENT - 1u)) != 0u) ||
      (((uintptr_t)ram_translation_cache &
        (ESP32S31_JIT_CACHE_ALIGNMENT - 1u)) != 0u) ||
      (((uintptr_t)rom_translation_cache & 3u) != 0u) ||
      (((uintptr_t)ram_translation_cache & 3u) != 0u))
    return false;

  return execute_cache_probe((uint32_t *)rom_translation_cache,
                             &result->rom_return_value,
                             &result->rom_patched_return_value) &&
         execute_cache_probe((uint32_t *)ram_translation_cache,
                             &result->ram_return_value,
                             &result->ram_patched_return_value);
}
