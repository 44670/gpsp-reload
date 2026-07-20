#ifndef GPSP_ESP32S31_GAMEPAK_RUNTIME_PLAN_H
#define GPSP_ESP32S31_GAMEPAK_RUNTIME_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32S31_RUNTIME_PAGE_BYTES 0x8000u
#define ESP32S31_RUNTIME_MAX_PAGES 1024u

typedef enum
{
  ESP32S31_RUNTIME_PAGE_INVALID = 0,
  ESP32S31_RUNTIME_PAGE_PSRAM,
  ESP32S31_RUNTIME_PAGE_FLASH,
} esp32s31_runtime_page_kind_t;

typedef enum
{
  ESP32S31_RUNTIME_PLAN_OK = 0,
  ESP32S31_RUNTIME_PLAN_INVALID,
  ESP32S31_RUNTIME_PLAN_NO_SPACE,
} esp32s31_runtime_plan_result_t;

typedef struct
{
  uint8_t kind[ESP32S31_RUNTIME_MAX_PAGES];
  uint16_t slot[ESP32S31_RUNTIME_MAX_PAGES];
  uint16_t page_count;
  uint16_t unique_pages;
  uint16_t duplicate_pages;
  uint16_t psram_pages;
  uint16_t flash_pages;
} esp32s31_runtime_page_plan_t;

uint32_t esp32s31_runtime_page_hash(const void *data, size_t bytes);

/* A hash hit is never sufficient: equality always includes a byte compare. */
bool esp32s31_runtime_pages_equal(
    uint32_t left_hash, const void *left,
    uint32_t right_hash, const void *right, size_t bytes);

esp32s31_runtime_plan_result_t esp32s31_runtime_plan_pages(
    const uint16_t *canonical_page, uint32_t page_count,
    uint32_t psram_capacity_pages, uint32_t flash_capacity_pages,
    bool allow_flash, esp32s31_runtime_page_plan_t *plan);

#endif
