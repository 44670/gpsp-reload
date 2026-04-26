/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include <stdint.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif

#include "esp32s3/psram_static.h"

#if defined(HAVE_DYNAREC)
#include "cpu.h"
#endif

#if defined(ESP_PLATFORM)

#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "ESP32-S3 static buffers require CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y"
#endif

#if defined(HAVE_DYNAREC)
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_memory_utils.h"
#include "esp_mmu_map.h"
#include "soc/ext_mem_defs.h"

#include "esp32s3/xtensa_codegen.h"
#endif
#endif

#if defined(HAVE_DYNAREC)
#ifndef CONFIG_MMU_PAGE_SIZE
#define CONFIG_MMU_PAGE_SIZE (64 * 1024)
#endif

#define ESP32S3_JIT_CACHE_ALIGNMENT CONFIG_MMU_PAGE_SIZE
#define ESP32S3_MAX_STATIC_ROM_JIT_BYTES (2 * 1024 * 1024)
#define ESP32S3_MAX_STATIC_RAM_JIT_BYTES (384 * 1024)
#define ESP32S3_JIT_SELFTEST_BYTES 16u
#define ESP32S3_JIT_SELFTEST_FIRST 0x37u
#define ESP32S3_JIT_SELFTEST_SECOND 0x5Au

#if defined(ESP_PLATFORM)
#if ROM_TRANSLATION_CACHE_SIZE > ESP32S3_MAX_STATIC_ROM_JIT_BYTES
#error "ESP32-S3 ROM JIT cache must use SMALL_TRANSLATION_CACHE or a <=2MB override"
#endif

#if RAM_TRANSLATION_CACHE_SIZE > ESP32S3_MAX_STATIC_RAM_JIT_BYTES
#error "ESP32-S3 RAM JIT cache must use SMALL_TRANSLATION_CACHE or a <=384KB override"
#endif
#endif

#define ESP32S3_ROM_JIT_STORAGE_SIZE \
  (ROM_TRANSLATION_CACHE_SIZE + TRANSLATION_CACHE_LIMIT_THRESHOLD)
#define ESP32S3_RAM_JIT_STORAGE_SIZE \
  (RAM_TRANSLATION_CACHE_SIZE + TRANSLATION_CACHE_LIMIT_THRESHOLD)
#endif

#define ESP32S3_SCREEN_PIXEL_COUNT \
  (GBA_SCREEN_BUFFER_SIZE / sizeof(u16))
#define ESP32S3_STATIC_AUDIO_SAMPLES \
  (ESP32S3_STATIC_AUDIO_FRAMES * 2u)

#if defined(HAVE_DYNAREC)
_Static_assert((ESP32S3_JIT_CACHE_ALIGNMENT &
                (ESP32S3_JIT_CACHE_ALIGNMENT - 1)) == 0,
               "JIT cache alignment must be a power of two");
#endif
_Static_assert((GBA_SCREEN_BUFFER_SIZE % sizeof(u16)) == 0,
               "GBA screen buffer must be u16-sized");

#if defined(HAVE_DYNAREC)
static GPSP_EXT_RAM_BSS u8
  esp32s3_rom_translation_cache_storage[ESP32S3_ROM_JIT_STORAGE_SIZE]
  __attribute__((aligned(ESP32S3_JIT_CACHE_ALIGNMENT)));
static GPSP_EXT_RAM_BSS u8
  esp32s3_ram_translation_cache_storage[ESP32S3_RAM_JIT_STORAGE_SIZE]
  __attribute__((aligned(ESP32S3_JIT_CACHE_ALIGNMENT)));
#endif

static u16 esp32s3_screen_pixels_storage[ESP32S3_SCREEN_PIXEL_COUNT];
static GPSP_EXT_RAM_BSS s16
  esp32s3_audio_sample_buffer_storage[ESP32S3_STATIC_AUDIO_SAMPLES];

#if defined(HAVE_DYNAREC)
static const char *esp32s3_static_buffers_error = "OK";

static bool ptr_range_contains(const u8 *base, size_t size,
                               const void *ptr, size_t len)
{
  uintptr_t base_addr = (uintptr_t)base;
  uintptr_t ptr_addr = (uintptr_t)ptr;

  return ptr_addr >= base_addr && len <= size &&
         (ptr_addr - base_addr) <= (size - len);
}

const char *esp32s3_static_buffers_last_error(void)
{
  return esp32s3_static_buffers_error;
}

#if defined(ESP_PLATFORM)
static bool esp32s3_static_set_error(const char *message)
{
  esp32s3_static_buffers_error = message;
  return false;
}

static bool esp32s3_jit_cache_sync_checked(void *data_start, void *data_end)
{
  uintptr_t start = (uintptr_t)data_start;
  uintptr_t end = (uintptr_t)data_end;
  size_t size;

  if (end <= start)
    return true;

  size = end - start;

  if (esp32s3_jit_data_range_is_static(data_start, size))
  {
    void *exec_start = esp32s3_jit_data_to_exec(data_start);
    size_t inst_line_size = esp_cache_get_line_size_by_addr(exec_start);
    esp_err_t err;

    if (inst_line_size == 0)
      inst_line_size = esp_cache_get_line_size_by_addr(data_start);

    err = esp_cache_msync(data_start, size,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M |
      ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
      ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (err != ESP_OK)
      return false;

    if (inst_line_size == 0)
      return false;

    {
      uintptr_t exec_addr = (uintptr_t)exec_start;
      uintptr_t inst_start = exec_addr & ~(uintptr_t)(inst_line_size - 1);
      uintptr_t inst_end =
        (exec_addr + size + inst_line_size - 1) &
        ~(uintptr_t)(inst_line_size - 1);

      err = esp_cache_msync((void *)inst_start, inst_end - inst_start,
        ESP_CACHE_MSYNC_FLAG_DIR_M2C |
        ESP_CACHE_MSYNC_FLAG_TYPE_INST);
      if (err != ESP_OK)
        return false;
    }

    return true;
  }

  __builtin___clear_cache(data_start, data_end);
  return true;
}

static bool esp32s3_validate_jit_page(const u8 *data_page)
{
  u8 *exec_page = esp32s3_jit_data_to_exec(data_page);
  u8 *round_trip = esp32s3_jit_exec_to_data(exec_page);
  esp_paddr_t data_paddr = 0;
  esp_paddr_t exec_paddr = 0;
  mmu_target_t data_target = 0;
  mmu_target_t exec_target = 0;

  if (!esp_ptr_external_ram(data_page))
    return esp32s3_static_set_error(
      "ESP32-S3 JIT data page is not mapped to PSRAM.");

  if (round_trip != data_page)
    return esp32s3_static_set_error(
      "ESP32-S3 JIT data/exec alias round trip failed.");

  if (!esp_ptr_executable(exec_page))
    return esp32s3_static_set_error(
      "ESP32-S3 JIT exec alias is not in an executable range.");

  if (esp_mmu_vaddr_to_paddr((void *)data_page, &data_paddr,
                             &data_target) != ESP_OK ||
      data_target != MMU_TARGET_PSRAM0)
    return esp32s3_static_set_error(
      "ESP32-S3 JIT data alias does not resolve to PSRAM.");

  if (esp_mmu_vaddr_to_paddr(exec_page, &exec_paddr,
                             &exec_target) != ESP_OK ||
      exec_target != MMU_TARGET_PSRAM0)
    return esp32s3_static_set_error(
      "ESP32-S3 JIT exec alias does not resolve to PSRAM.");

  if (exec_paddr != data_paddr)
    return esp32s3_static_set_error(
      "ESP32-S3 JIT data and exec aliases map different pages.");

  return true;
}

static bool esp32s3_validate_jit_range(const u8 *base, size_t size)
{
  uintptr_t start = (uintptr_t)base;
  uintptr_t end = start + size;
  uintptr_t page;

  if ((start & (ESP32S3_JIT_CACHE_ALIGNMENT - 1)) != 0)
    return esp32s3_static_set_error(
      "ESP32-S3 static translation cache is not page-aligned.");

  if (size == 0 || end < start)
    return esp32s3_static_set_error(
      "ESP32-S3 static translation cache range is invalid.");

  for (page = start; page < end; page += ESP32S3_JIT_CACHE_ALIGNMENT)
  {
    if (!esp32s3_validate_jit_page((const u8 *)page))
      return false;
  }

  return true;
}

static void esp32s3_emit_jit_selftest_return(u8 *data_ptr, u32 value)
{
  u8 *ptr = data_ptr;

  memset(data_ptr, 0, ESP32S3_JIT_SELFTEST_BYTES);
  xtensa_emit_entry_sp_32(&ptr);
  xtensa_emit_movi(&ptr, 2, value);
  xtensa_emit_retw_n(&ptr);
}

static bool esp32s3_run_jit_selftest_call(u8 *data_ptr, u32 expected)
{
  typedef u32 (*esp32s3_jit_selftest_fn)(void);

  u8 *exec_ptr = esp32s3_jit_data_to_exec(data_ptr);
  esp32s3_jit_selftest_fn fn =
    (esp32s3_jit_selftest_fn)(uintptr_t)exec_ptr;
  u32 actual = fn();

  return actual == expected;
}

static bool esp32s3_static_jit_selftest(void)
{
  u8 *data_ptr = esp32s3_rom_translation_cache_storage;

  esp32s3_emit_jit_selftest_return(data_ptr, ESP32S3_JIT_SELFTEST_FIRST);
  if (!esp32s3_jit_cache_sync_checked(data_ptr,
                                      data_ptr + ESP32S3_JIT_SELFTEST_BYTES))
    return esp32s3_static_set_error(
      "ESP32-S3 JIT self-test cache sync failed.");

  if (!esp32s3_run_jit_selftest_call(data_ptr, ESP32S3_JIT_SELFTEST_FIRST))
    return esp32s3_static_set_error(
      "ESP32-S3 JIT self-test first execution failed.");

  esp32s3_emit_jit_selftest_return(data_ptr, ESP32S3_JIT_SELFTEST_SECOND);
  if (!esp32s3_jit_cache_sync_checked(data_ptr,
                                      data_ptr + ESP32S3_JIT_SELFTEST_BYTES))
    return esp32s3_static_set_error(
      "ESP32-S3 JIT self-test rewrite cache sync failed.");

  if (!esp32s3_run_jit_selftest_call(data_ptr, ESP32S3_JIT_SELFTEST_SECOND))
    return esp32s3_static_set_error(
      "ESP32-S3 JIT self-test rewrite execution failed.");

  memset(data_ptr, 0, ESP32S3_JIT_SELFTEST_BYTES);
  if (!esp32s3_jit_cache_sync_checked(data_ptr,
                                      data_ptr + ESP32S3_JIT_SELFTEST_BYTES))
    return esp32s3_static_set_error(
      "ESP32-S3 JIT self-test cleanup cache sync failed.");

  return true;
}
#endif

bool esp32s3_static_buffers_init_translation_caches(u8 **rom_cache,
                                                    u8 **ram_cache)
{
  uintptr_t alignment_mask = ESP32S3_JIT_CACHE_ALIGNMENT - 1;

  esp32s3_static_buffers_error = "OK";

  if ((((uintptr_t)esp32s3_rom_translation_cache_storage) & alignment_mask) ||
      (((uintptr_t)esp32s3_ram_translation_cache_storage) & alignment_mask))
  {
    esp32s3_static_buffers_error =
      "ESP32-S3 static translation caches are not page-aligned.";
    return false;
  }

#if defined(ESP_PLATFORM)
  if (!esp32s3_validate_jit_range(esp32s3_rom_translation_cache_storage,
                                  sizeof(esp32s3_rom_translation_cache_storage)) ||
      !esp32s3_validate_jit_range(esp32s3_ram_translation_cache_storage,
                                  sizeof(esp32s3_ram_translation_cache_storage)) ||
      !esp32s3_static_jit_selftest())
    return false;
#endif

  *rom_cache = esp32s3_rom_translation_cache_storage;
  *ram_cache = esp32s3_ram_translation_cache_storage;
  return true;
}
#endif

u16 *esp32s3_static_screen_pixels(void)
{
  return esp32s3_screen_pixels_storage;
}

u16 *esp32s3_static_processed_pixels(void)
{
  return NULL;
}

u16 *esp32s3_static_previous_pixels(void)
{
  return NULL;
}

s16 *esp32s3_static_audio_sample_buffer(size_t required_bytes)
{
  if (required_bytes > sizeof(esp32s3_audio_sample_buffer_storage))
    return NULL;

  return esp32s3_audio_sample_buffer_storage;
}

size_t esp32s3_static_audio_sample_buffer_size(void)
{
  return sizeof(esp32s3_audio_sample_buffer_storage);
}

#if defined(HAVE_DYNAREC)
bool esp32s3_jit_data_range_is_static(const void *data_start, size_t size)
{
  return ptr_range_contains(esp32s3_rom_translation_cache_storage,
                            sizeof(esp32s3_rom_translation_cache_storage),
                            data_start, size) ||
         ptr_range_contains(esp32s3_ram_translation_cache_storage,
                            sizeof(esp32s3_ram_translation_cache_storage),
                            data_start, size);
}

u8 *esp32s3_jit_data_to_exec(const void *data_ptr)
{
#if defined(ESP_PLATFORM)
  if (esp32s3_jit_data_range_is_static(data_ptr, 1))
  {
    uintptr_t linear = (uintptr_t)data_ptr & SOC_MMU_LINEAR_ADDR_MASK;
    return (u8 *)(uintptr_t)(SOC_MMU_IBUS_VADDR_BASE | linear);
  }
#endif

  return (u8 *)data_ptr;
}

u8 *esp32s3_jit_exec_to_data(const void *exec_ptr)
{
#if defined(ESP_PLATFORM)
  uintptr_t exec_addr = (uintptr_t)exec_ptr;

  if (exec_addr >= SOC_MMU_IBUS_VADDR_BASE &&
      exec_addr < (SOC_MMU_IBUS_VADDR_BASE + SOC_MMU_LINEAR_ADDR_MASK + 1u))
  {
    uintptr_t linear = exec_addr & SOC_MMU_LINEAR_ADDR_MASK;
    uintptr_t rom_linear =
      (uintptr_t)esp32s3_rom_translation_cache_storage &
      SOC_MMU_LINEAR_ADDR_MASK;
    uintptr_t ram_linear =
      (uintptr_t)esp32s3_ram_translation_cache_storage &
      SOC_MMU_LINEAR_ADDR_MASK;

    if (linear >= rom_linear &&
        (linear - rom_linear) < sizeof(esp32s3_rom_translation_cache_storage))
      return esp32s3_rom_translation_cache_storage + (linear - rom_linear);

    if (linear >= ram_linear &&
        (linear - ram_linear) < sizeof(esp32s3_ram_translation_cache_storage))
      return esp32s3_ram_translation_cache_storage + (linear - ram_linear);
  }
#endif

  return (u8 *)exec_ptr;
}

void esp32s3_jit_cache_sync(void *data_start, void *data_end)
{
#if defined(ESP_PLATFORM)
  (void)esp32s3_jit_cache_sync_checked(data_start, data_end);
#else
  uintptr_t start = (uintptr_t)data_start;
  uintptr_t end = (uintptr_t)data_end;

  if (end <= start)
    return;

  __builtin___clear_cache(data_start, data_end);
#endif
}
#endif
