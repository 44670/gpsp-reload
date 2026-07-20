#include "gamepak_runtime_plan.h"

#include <string.h>

uint32_t esp32s31_runtime_page_hash(const void *data, size_t bytes)
{
  if (data == NULL && bytes != 0u)
    return 0u;

  /* FNV-1a is cheap on RV32; collisions are resolved with memcmp below. */
  uint32_t hash = UINT32_C(2166136261);
  const uint8_t *source = (const uint8_t *)data;
  for (size_t i = 0u; i < bytes; i++)
  {
    hash ^= source[i];
    hash *= UINT32_C(16777619);
  }
  return hash;
}

bool esp32s31_runtime_pages_equal(
    uint32_t left_hash, const void *left,
    uint32_t right_hash, const void *right, size_t bytes)
{
  if ((left == NULL || right == NULL) && bytes != 0u)
    return false;
  return left_hash == right_hash &&
      (bytes == 0u || memcmp(left, right, bytes) == 0);
}

esp32s31_runtime_plan_result_t esp32s31_runtime_plan_pages(
    const uint16_t *canonical_page, uint32_t page_count,
    uint32_t psram_capacity_pages, uint32_t flash_capacity_pages,
    bool allow_flash, esp32s31_runtime_page_plan_t *plan)
{
  if (canonical_page == NULL || plan == NULL || page_count == 0u ||
      page_count > ESP32S31_RUNTIME_MAX_PAGES ||
      psram_capacity_pages > UINT16_MAX ||
      flash_capacity_pages > UINT16_MAX)
    return ESP32S31_RUNTIME_PLAN_INVALID;

  memset(plan, 0, sizeof(*plan));
  plan->page_count = (uint16_t)page_count;

  uint32_t psram_slot = 0u;
  uint32_t flash_slot = 0u;
  for (uint32_t page = 0u; page < page_count; page++)
  {
    const uint32_t canonical = canonical_page[page];
    if (canonical > page || canonical >= page_count ||
        canonical_page[canonical] != canonical)
    {
      memset(plan, 0, sizeof(*plan));
      return ESP32S31_RUNTIME_PLAN_INVALID;
    }

    if (canonical != page)
    {
      if (plan->kind[canonical] == ESP32S31_RUNTIME_PAGE_INVALID)
      {
        memset(plan, 0, sizeof(*plan));
        return ESP32S31_RUNTIME_PLAN_INVALID;
      }
      plan->kind[page] = plan->kind[canonical];
      plan->slot[page] = plan->slot[canonical];
      plan->duplicate_pages++;
      continue;
    }

    if (psram_slot < psram_capacity_pages)
    {
      plan->kind[page] = ESP32S31_RUNTIME_PAGE_PSRAM;
      plan->slot[page] = (uint16_t)psram_slot++;
      plan->unique_pages++;
      plan->psram_pages++;
      continue;
    }

    if (allow_flash && flash_slot < flash_capacity_pages)
    {
      plan->kind[page] = ESP32S31_RUNTIME_PAGE_FLASH;
      plan->slot[page] = (uint16_t)flash_slot++;
      plan->unique_pages++;
      plan->flash_pages++;
      continue;
    }

    memset(plan, 0, sizeof(*plan));
    return ESP32S31_RUNTIME_PLAN_NO_SPACE;
  }

  return ESP32S31_RUNTIME_PLAN_OK;
}
