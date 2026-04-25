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

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif

#include "esp32s3/psram_static.h"
#include "cpu.h"

#if defined(ESP_PLATFORM)

#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "ESP32-S3 static buffers require CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y"
#endif

#include "esp_cache.h"
#include "soc/ext_mem_defs.h"
#endif

#ifndef CONFIG_MMU_PAGE_SIZE
#define CONFIG_MMU_PAGE_SIZE (64 * 1024)
#endif

#define ESP32S3_JIT_CACHE_ALIGNMENT CONFIG_MMU_PAGE_SIZE
#define ESP32S3_MAX_STATIC_ROM_JIT_BYTES (2 * 1024 * 1024)
#define ESP32S3_MAX_STATIC_RAM_JIT_BYTES (384 * 1024)

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
#define ESP32S3_SCREEN_PIXEL_COUNT \
  (GBA_SCREEN_BUFFER_SIZE / sizeof(u16))
#define ESP32S3_STATIC_AUDIO_SAMPLES \
  (ESP32S3_STATIC_AUDIO_FRAMES * 2u)

_Static_assert((ESP32S3_JIT_CACHE_ALIGNMENT &
                (ESP32S3_JIT_CACHE_ALIGNMENT - 1)) == 0,
               "JIT cache alignment must be a power of two");
_Static_assert((GBA_SCREEN_BUFFER_SIZE % sizeof(u16)) == 0,
               "GBA screen buffer must be u16-sized");

static GPSP_EXT_RAM_BSS u8
  esp32s3_rom_translation_cache_storage[ESP32S3_ROM_JIT_STORAGE_SIZE]
  __attribute__((aligned(ESP32S3_JIT_CACHE_ALIGNMENT)));
static GPSP_EXT_RAM_BSS u8
  esp32s3_ram_translation_cache_storage[ESP32S3_RAM_JIT_STORAGE_SIZE]
  __attribute__((aligned(ESP32S3_JIT_CACHE_ALIGNMENT)));

static GPSP_EXT_RAM_BSS u16
  esp32s3_screen_pixels_storage[ESP32S3_SCREEN_PIXEL_COUNT];
static GPSP_EXT_RAM_BSS u16
  esp32s3_processed_pixels_storage[ESP32S3_SCREEN_PIXEL_COUNT];
static GPSP_EXT_RAM_BSS u16
  esp32s3_previous_pixels_storage[ESP32S3_SCREEN_PIXEL_COUNT];
static GPSP_EXT_RAM_BSS s16
  esp32s3_audio_sample_buffer_storage[ESP32S3_STATIC_AUDIO_SAMPLES];

static bool ptr_range_contains(const u8 *base, size_t size,
                               const void *ptr, size_t len)
{
  uintptr_t base_addr = (uintptr_t)base;
  uintptr_t ptr_addr = (uintptr_t)ptr;

  return ptr_addr >= base_addr && len <= size &&
         (ptr_addr - base_addr) <= (size - len);
}

bool esp32s3_static_buffers_init_translation_caches(u8 **rom_cache,
                                                    u8 **ram_cache)
{
  uintptr_t alignment_mask = ESP32S3_JIT_CACHE_ALIGNMENT - 1;

  if ((((uintptr_t)esp32s3_rom_translation_cache_storage) & alignment_mask) ||
      (((uintptr_t)esp32s3_ram_translation_cache_storage) & alignment_mask))
    return false;

  *rom_cache = esp32s3_rom_translation_cache_storage;
  *ram_cache = esp32s3_ram_translation_cache_storage;
  return true;
}

u16 *esp32s3_static_screen_pixels(void)
{
  return esp32s3_screen_pixels_storage;
}

u16 *esp32s3_static_processed_pixels(void)
{
  return esp32s3_processed_pixels_storage;
}

u16 *esp32s3_static_previous_pixels(void)
{
  return esp32s3_previous_pixels_storage;
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
  uintptr_t start = (uintptr_t)data_start;
  uintptr_t end = (uintptr_t)data_end;
  size_t size;

  if (end <= start)
    return;

  size = end - start;

#if defined(ESP_PLATFORM)
  if (esp32s3_jit_data_range_is_static(data_start, size))
  {
    void *exec_start = esp32s3_jit_data_to_exec(data_start);
    size_t inst_line_size = esp_cache_get_line_size_by_addr(exec_start);

    (void)esp_cache_msync(data_start, size,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M |
      ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
      ESP_CACHE_MSYNC_FLAG_UNALIGNED);

    if (inst_line_size != 0)
    {
      uintptr_t exec_addr = (uintptr_t)exec_start;
      uintptr_t inst_start = exec_addr & ~(uintptr_t)(inst_line_size - 1);
      uintptr_t inst_end =
        (exec_addr + size + inst_line_size - 1) &
        ~(uintptr_t)(inst_line_size - 1);

      (void)esp_cache_msync((void *)inst_start, inst_end - inst_start,
        ESP_CACHE_MSYNC_FLAG_DIR_M2C |
        ESP_CACHE_MSYNC_FLAG_TYPE_INST);
    }
    return;
  }
#endif

  __builtin___clear_cache(data_start, data_end);
}
