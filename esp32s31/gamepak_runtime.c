#include "gamepak_runtime.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"

#include "game_menu.h"
#include "gamepak_direct_mmap.h"
#include "gamepak_runtime_plan.h"

#ifndef ESP32S31_GAMEPAK_STATIC_BYTES
#define ESP32S31_GAMEPAK_STATIC_BYTES (12u * 1024u * 1024u)
#endif

#ifndef GPSP_ESP32S31_FLASH_SPILL
#define GPSP_ESP32S31_FLASH_SPILL 1
#endif

#define GAMEPAK_PARTITION "gamepak"
#define STATIC_WORK_PAGES 2u
#define FLASH_SECTORS_PER_PAGE \
  (ESP32S31_RUNTIME_PAGE_BYTES / SPI_FLASH_SEC_SIZE)
#define MENU_PROGRESS_GRANULARITY 8u

_Static_assert(
    ESP32S31_GAMEPAK_STATIC_BYTES % ESP32S31_RUNTIME_PAGE_BYTES == 0u,
    "static gamepak container must contain whole 32 KiB pages");
_Static_assert(
    ESP32S31_GAMEPAK_STATIC_BYTES >=
        (STATIC_WORK_PAGES + 1u) * ESP32S31_RUNTIME_PAGE_BYTES,
    "static gamepak container is too small");
_Static_assert(
    ESP32S31_RUNTIME_PAGE_BYTES % SPI_FLASH_SEC_SIZE == 0u,
    "logical pages must contain whole flash erase sectors");
_Static_assert(SPI_FLASH_SEC_SIZE == 4096u,
               "ESP32-S31 flash sector geometry changed");

/*
 * Entire ROM working set is fixed at link time. The first two pages are scan
 * and byte-compare scratch; the remaining pages are immutable deduplicated ROM
 * storage. Keep this large, non-executable array in the external noinit output
 * section: that section follows ordinary external BSS, so the CPU state and
 * generated-code caches stay in low PSRAM instead of being displaced to the
 * last physical PSRAM pages. Every container page that can be observed is
 * overwritten by the directory scan or ROM loader before use, so startup
 * zeroing is neither required nor relied upon. Flash I/O uses a separate 4 KiB
 * internal staging buffer because PSRAM is not directly readable while SPI1
 * cache is disabled.
 */
static EXT_RAM_NOINIT_ATTR uint8_t s_gamepak_container[
    ESP32S31_GAMEPAK_STATIC_BYTES]
    __attribute__((aligned(ESP32S31_RUNTIME_PAGE_BYTES)));
static EXT_RAM_BSS_ATTR uint32_t
    s_page_hash[ESP32S31_RUNTIME_MAX_PAGES];
static EXT_RAM_BSS_ATTR uint16_t
    s_canonical_page[ESP32S31_RUNTIME_MAX_PAGES];
static EXT_RAM_BSS_ATTR const uint8_t
    *s_page_pointer[ESP32S31_RUNTIME_MAX_PAGES];
static EXT_RAM_BSS_ATTR esp32s31_runtime_page_plan_t s_plan;
static DRAM_ATTR uint8_t s_flash_sector_staging[SPI_FLASH_SEC_SIZE]
    __attribute__((aligned(4)));

static const char *TAG = "runtime-gamepak";
static const esp_partition_t *s_partition;
static const void *s_flash_mapping;
static esp_partition_mmap_handle_t s_flash_mapping_handle;
static uint32_t s_page_count;

static uint8_t *scan_page(void)
{
  return s_gamepak_container;
}

static uint8_t *compare_page(void)
{
  return s_gamepak_container + ESP32S31_RUNTIME_PAGE_BYTES;
}

static uint8_t *psram_page(uint32_t slot)
{
  return s_gamepak_container +
      (STATIC_WORK_PAGES + slot) * ESP32S31_RUNTIME_PAGE_BYTES;
}

static uint32_t psram_capacity_pages(void)
{
  return ESP32S31_GAMEPAK_STATIC_BYTES / ESP32S31_RUNTIME_PAGE_BYTES -
      STATIC_WORK_PAGES;
}

void *esp32s31_runtime_gamepak_menu_workspace(size_t *workspace_bytes)
{
  if (workspace_bytes != NULL)
    *workspace_bytes = STATIC_WORK_PAGES * ESP32S31_RUNTIME_PAGE_BYTES;
  return s_gamepak_container;
}

static void set_error(char *error, size_t error_bytes,
                      const char *format, ...)
{
  if (error == NULL || error_bytes == 0u)
    return;
  va_list arguments;
  va_start(arguments, format);
  (void)vsnprintf(error, error_bytes, format, arguments);
  va_end(arguments);
}

static void reset_source(void)
{
  if (s_flash_mapping != NULL)
  {
    esp_partition_munmap(s_flash_mapping_handle);
    s_flash_mapping = NULL;
    s_flash_mapping_handle = 0;
  }
  memset(s_page_hash, 0, sizeof(s_page_hash));
  memset(s_canonical_page, 0, sizeof(s_canonical_page));
  memset(s_page_pointer, 0, sizeof(s_page_pointer));
  memset(&s_plan, 0, sizeof(s_plan));
  s_partition = NULL;
  s_page_count = 0u;
}

static bool read_rom_page(FILE *file, uint32_t page,
                          uint32_t rom_bytes, uint8_t *buffer,
                          char *error, size_t error_bytes)
{
  if (file == NULL || buffer == NULL)
  {
    set_error(error, error_bytes, "invalid TF page read");
    return false;
  }
  memset(buffer, 0xff, ESP32S31_RUNTIME_PAGE_BYTES);
  const uint32_t offset = page * ESP32S31_RUNTIME_PAGE_BYTES;
  uint32_t bytes = rom_bytes - offset;
  if (bytes > ESP32S31_RUNTIME_PAGE_BYTES)
    bytes = ESP32S31_RUNTIME_PAGE_BYTES;
  if (fseek(file, (long)offset, SEEK_SET) != 0 ||
      (bytes != 0u && fread(buffer, 1u, bytes, file) != bytes))
  {
    set_error(error, error_bytes, "TF read failed at page %u",
              (unsigned)page);
    return false;
  }
  return true;
}

static bool scan_pages(FILE *file, FILE *compare_file, const char *path,
                       uint32_t rom_bytes, uint32_t page_count,
                       char *error, size_t error_bytes)
{
  uint32_t unique_pages = 0u;
  uint32_t duplicate_pages = 0u;
  for (uint32_t page = 0u; page < page_count; page++)
  {
    if (!read_rom_page(file, page, rom_bytes, scan_page(),
                       error, error_bytes))
      return false;
    const uint32_t hash = esp32s31_runtime_page_hash(
        scan_page(), ESP32S31_RUNTIME_PAGE_BYTES);
    s_page_hash[page] = hash;
    s_canonical_page[page] = (uint16_t)page;

    for (uint32_t candidate = 0u; candidate < page; candidate++)
    {
      if (s_canonical_page[candidate] != candidate ||
          s_page_hash[candidate] != hash)
        continue;
      if (!read_rom_page(compare_file, candidate, rom_bytes, compare_page(),
                         error, error_bytes))
        return false;
      if (esp32s31_runtime_pages_equal(
              hash, scan_page(), s_page_hash[candidate], compare_page(),
              ESP32S31_RUNTIME_PAGE_BYTES))
      {
        s_canonical_page[page] = (uint16_t)candidate;
        break;
      }
    }

    if (s_canonical_page[page] == page)
      unique_pages++;
    else
      duplicate_pages++;
    if ((page % MENU_PROGRESS_GRANULARITY) == 0u ||
        page + 1u == page_count)
    {
      char detail[64];
      snprintf(detail, sizeof(detail), "UNIQUE %u  DUP %u",
               (unsigned)unique_pages, (unsigned)duplicate_pages);
      esp32s31_game_menu_show_progress(
          "HASHING 32K PAGES", path, page + 1u, page_count, detail);
    }
  }
  return true;
}

static bool prepare_flash_page(const esp_partition_t *partition,
                               uint32_t slot, const uint8_t *source,
                               esp32s31_runtime_gamepak_stats_t *stats,
                               char *error, size_t error_bytes)
{
  const size_t offset = (size_t)slot * ESP32S31_RUNTIME_PAGE_BYTES;
  bool equal = true;
  for (size_t sector = 0u; sector < FLASH_SECTORS_PER_PAGE; sector++)
  {
    const size_t sector_offset = sector * SPI_FLASH_SEC_SIZE;
    const esp_err_t result = esp_partition_read(
        partition, offset + sector_offset, s_flash_sector_staging,
        SPI_FLASH_SEC_SIZE);
    if (result != ESP_OK)
    {
      set_error(error, error_bytes, "flash read failed: %s",
                esp_err_to_name(result));
      return false;
    }
    if (memcmp(source + sector_offset, s_flash_sector_staging,
               SPI_FLASH_SEC_SIZE) != 0)
    {
      equal = false;
      break;
    }
  }
  if (equal)
  {
    stats->flash_blocks_skipped++;
    return true;
  }

  esp_err_t result = esp_partition_erase_range(
      partition, offset, ESP32S31_RUNTIME_PAGE_BYTES);
  if (result != ESP_OK)
  {
    set_error(error, error_bytes, "flash erase failed: %s",
              esp_err_to_name(result));
    return false;
  }
  stats->flash_sectors_erased += FLASH_SECTORS_PER_PAGE;

  for (size_t sector = 0u; sector < FLASH_SECTORS_PER_PAGE; sector++)
  {
    const size_t sector_offset = sector * SPI_FLASH_SEC_SIZE;
    memcpy(s_flash_sector_staging, source + sector_offset,
           SPI_FLASH_SEC_SIZE);
    result = esp_partition_write(
        partition, offset + sector_offset, s_flash_sector_staging,
        SPI_FLASH_SEC_SIZE);
    if (result != ESP_OK)
    {
      set_error(error, error_bytes, "flash write failed: %s",
                esp_err_to_name(result));
      return false;
    }
  }

  for (size_t sector = 0u; sector < FLASH_SECTORS_PER_PAGE; sector++)
  {
    const size_t sector_offset = sector * SPI_FLASH_SEC_SIZE;
    result = esp_partition_read(
        partition, offset + sector_offset, s_flash_sector_staging,
        SPI_FLASH_SEC_SIZE);
    if (result != ESP_OK ||
        memcmp(source + sector_offset, s_flash_sector_staging,
               SPI_FLASH_SEC_SIZE) != 0)
    {
      set_error(error, error_bytes, "flash verify failed at slot %u",
                (unsigned)slot);
      return false;
    }
  }
  stats->flash_blocks_written++;
  return true;
}

static void show_flash_progress(
    const char *path, const esp32s31_runtime_gamepak_stats_t *stats,
    uint32_t total_blocks)
{
  char detail[64];
  snprintf(detail, sizeof(detail), "SKIP %u  WRITE %u BLOCKS",
           (unsigned)stats->flash_blocks_skipped,
           (unsigned)stats->flash_blocks_written);
  esp32s31_game_menu_show_progress(
      "FLASH 32K BLOCKS", path,
      stats->flash_blocks_skipped + stats->flash_blocks_written,
      total_blocks, detail);
}

static bool load_pages(FILE *file, FILE *compare_file, const char *path,
                       uint32_t rom_bytes, uint32_t page_count,
                       esp32s31_runtime_gamepak_stats_t *stats,
                       char *error, size_t error_bytes)
{
  for (uint32_t page = 0u; page < page_count; page++)
  {
    if (!read_rom_page(file, page, rom_bytes, scan_page(),
                       error, error_bytes))
      return false;
    const uint32_t hash = esp32s31_runtime_page_hash(
        scan_page(), ESP32S31_RUNTIME_PAGE_BYTES);
    if (hash != s_page_hash[page])
    {
      set_error(error, error_bytes,
                "TF file changed after scan at page %u",
                (unsigned)page);
      return false;
    }

    const uint32_t canonical = s_canonical_page[page];
    if (canonical != page)
    {
      if (!read_rom_page(compare_file, canonical, rom_bytes, compare_page(),
                         error, error_bytes))
        return false;
      const uint32_t canonical_hash = esp32s31_runtime_page_hash(
          compare_page(), ESP32S31_RUNTIME_PAGE_BYTES);
      if (canonical_hash != s_page_hash[canonical] ||
          !esp32s31_runtime_pages_equal(
              hash, scan_page(), canonical_hash, compare_page(),
              ESP32S31_RUNTIME_PAGE_BYTES))
      {
        set_error(error, error_bytes,
                  "TF dedup changed after scan at page %u",
                  (unsigned)page);
        return false;
      }
      if (s_plan.kind[page] == ESP32S31_RUNTIME_PAGE_PSRAM)
      {
        s_page_pointer[page] = s_page_pointer[canonical];
        if (s_page_pointer[page] == NULL)
        {
          set_error(error, error_bytes,
                    "missing canonical PSRAM page %u",
                    (unsigned)canonical);
          return false;
        }
      }
    }
    else if (s_plan.kind[page] == ESP32S31_RUNTIME_PAGE_PSRAM)
    {
      uint8_t *destination = psram_page(s_plan.slot[page]);
      memcpy(destination, scan_page(), ESP32S31_RUNTIME_PAGE_BYTES);
      s_page_pointer[page] = destination;
    }
    else if (s_plan.kind[page] == ESP32S31_RUNTIME_PAGE_FLASH)
    {
      /* Keep the cumulative counters visible during the potentially slow I/O. */
      show_flash_progress(path, stats, s_plan.flash_pages);
      if (!prepare_flash_page(
              s_partition, s_plan.slot[page], scan_page(), stats,
              error, error_bytes))
        return false;
      show_flash_progress(path, stats, s_plan.flash_pages);
    }
    else
    {
      set_error(error, error_bytes, "invalid page plan at %u",
                (unsigned)page);
      return false;
    }

    const bool processed_flash =
        canonical == page &&
        s_plan.kind[page] == ESP32S31_RUNTIME_PAGE_FLASH;
    if (!processed_flash &&
        ((page % MENU_PROGRESS_GRANULARITY) == 0u ||
         page + 1u == page_count))
    {
      char detail[64];
      snprintf(detail, sizeof(detail), "UNIQUE %u  DUP %u%s",
               (unsigned)s_plan.unique_pages,
               (unsigned)s_plan.duplicate_pages,
               s_plan.flash_pages == 0u ? "" : "  FLASH OVERFLOW");
      esp32s31_game_menu_show_progress(
          "LOADING DEDUP PAGES", path, page + 1u, page_count, detail);
    }
  }
  return true;
}

static bool map_flash_pages(char *error, size_t error_bytes)
{
  if (s_plan.flash_pages == 0u)
    return true;

  const size_t bytes =
      (size_t)s_plan.flash_pages * ESP32S31_RUNTIME_PAGE_BYTES;
  const esp_err_t result = esp_partition_mmap(
      s_partition, 0u, bytes, ESP_PARTITION_MMAP_DATA,
      &s_flash_mapping, &s_flash_mapping_handle);
  if (result != ESP_OK)
  {
    set_error(error, error_bytes, "flash mmap failed: %s",
              esp_err_to_name(result));
    return false;
  }

  for (uint32_t page = 0u; page < s_page_count; page++)
  {
    if (s_plan.kind[page] == ESP32S31_RUNTIME_PAGE_FLASH)
      s_page_pointer[page] = (const uint8_t *)s_flash_mapping +
          (size_t)s_plan.slot[page] * ESP32S31_RUNTIME_PAGE_BYTES;
  }
  return true;
}

#if defined(GPSP_ESP32S31_PSRAM_FAULT_TRACE) && \
    GPSP_ESP32S31_PSRAM_FAULT_TRACE
static uint32_t verify_hash_bytes(uint32_t hash, const uint8_t *data,
                                  size_t bytes)
{
  for (size_t index = 0u; index < bytes; index++)
  {
    hash ^= data[index];
    hash *= UINT32_C(16777619);
  }
  return hash;
}

/* The scan/load passes prove the duplicate decision against the TF file, but
 * a PSRAM destination used to be trusted immediately after memcpy.  A fault
 * build performs a third, exact byte comparison against every final logical
 * mapping.  This distinguishes a dedup/map/copy defect from a later PSRAM or
 * JIT execution failure without relying on another hash match. */
static bool verify_loaded_pages(const char *path, uint32_t rom_bytes,
                                uint32_t page_count,
                                char *error, size_t error_bytes)
{
  FILE *file = fopen(path, "rb");
  uint32_t source_hash = UINT32_C(2166136261);
  uint32_t mapped_hash = UINT32_C(2166136261);

  if (file == NULL)
  {
    set_error(error, error_bytes, "cannot reopen selected ROM for verify");
    return false;
  }

  for (uint32_t page = 0u; page < page_count; page++)
  {
    if (!read_rom_page(file, page, rom_bytes, scan_page(),
                       error, error_bytes))
    {
      fclose(file);
      return false;
    }
    const uint8_t *const mapped = s_page_pointer[page];
    if (mapped == NULL ||
        memcmp(scan_page(), mapped, ESP32S31_RUNTIME_PAGE_BYTES) != 0)
    {
      set_error(error, error_bytes,
                "final mapped ROM verify failed at page %u canonical %u",
                (unsigned)page, (unsigned)s_canonical_page[page]);
      fclose(file);
      return false;
    }
    source_hash = verify_hash_bytes(
        source_hash, scan_page(), ESP32S31_RUNTIME_PAGE_BYTES);
    mapped_hash = verify_hash_bytes(
        mapped_hash, mapped, ESP32S31_RUNTIME_PAGE_BYTES);
  }
  fclose(file);

  ESP_LOGI(TAG,
           "final mapped ROM byte verify passed: pages=%" PRIu32
           " source_fnv=0x%08" PRIx32 " mapped_fnv=0x%08" PRIx32,
           page_count, source_hash, mapped_hash);
  printf("result=PASS command=runtime_gamepak_verify method=byte_compare "
         "pages=%" PRIu32 " source_fnv=0x%08" PRIx32
         " mapped_fnv=0x%08" PRIx32 "\n",
         page_count, source_hash, mapped_hash);
  return true;
}
#endif

const uint8_t *esp32s31_runtime_gamepak_resolve_page(
    void *context, uint32_t logical_page)
{
  (void)context;
  if (logical_page >= s_page_count)
    return NULL;
  return s_page_pointer[logical_page];
}

bool esp32s31_runtime_gamepak_load(
    const char *path, struct retro_game_info *info,
    esp32s31_runtime_gamepak_stats_t *stats,
    char *error, size_t error_bytes)
{
  if (path == NULL || info == NULL || stats == NULL)
  {
    set_error(error, error_bytes, "invalid ROM load arguments");
    return false;
  }

  reset_source();
  memset(stats, 0, sizeof(*stats));

  struct stat status;
  if (stat(path, &status) != 0 || status.st_size < 0xc0 ||
      (uint64_t)status.st_size > 32u * 1024u * 1024u)
  {
    set_error(error, error_bytes, "ROM size must be 192 bytes to 32 MiB");
    return false;
  }
  const uint32_t rom_bytes = (uint32_t)status.st_size;
  const uint32_t page_count =
      (rom_bytes + ESP32S31_RUNTIME_PAGE_BYTES - 1u) /
      ESP32S31_RUNTIME_PAGE_BYTES;

  FILE *file = fopen(path, "rb");
  FILE *compare_file = fopen(path, "rb");
  if (file == NULL || compare_file == NULL)
  {
    if (file != NULL)
      fclose(file);
    if (compare_file != NULL)
      fclose(compare_file);
    set_error(error, error_bytes, "cannot open selected ROM");
    return false;
  }
  bool success = scan_pages(
      file, compare_file, path, rom_bytes, page_count, error, error_bytes);
  fclose(file);
  fclose(compare_file);
  if (!success)
    return false;

  s_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
      GAMEPAK_PARTITION);
  const uint32_t flash_capacity = s_partition == NULL ? 0u :
      (uint32_t)(s_partition->size / ESP32S31_RUNTIME_PAGE_BYTES);
  const esp32s31_runtime_plan_result_t plan_result =
      esp32s31_runtime_plan_pages(
          s_canonical_page, page_count, psram_capacity_pages(), flash_capacity,
          GPSP_ESP32S31_FLASH_SPILL != 0, &s_plan);
  if (plan_result != ESP32S31_RUNTIME_PLAN_OK)
  {
    uint32_t unique_pages = 0u;
    for (uint32_t page = 0u; page < page_count; page++)
      unique_pages += s_canonical_page[page] == page;
    set_error(error, error_bytes,
              "%s: unique=%u PSRAM=%u flash=%u",
              plan_result == ESP32S31_RUNTIME_PLAN_NO_SPACE ?
                  "no space" : "invalid dedup plan",
              (unsigned)unique_pages,
              (unsigned)psram_capacity_pages(),
              (unsigned)(GPSP_ESP32S31_FLASH_SPILL ?
                         flash_capacity : 0u));
    return false;
  }
  if (s_plan.flash_pages != 0u && s_partition == NULL)
  {
    set_error(error, error_bytes, "gamepak flash partition is missing");
    return false;
  }

  stats->rom_bytes = rom_bytes;
  stats->logical_pages = page_count;
  stats->unique_pages = s_plan.unique_pages;
  stats->duplicate_pages = s_plan.duplicate_pages;
  stats->psram_pages = s_plan.psram_pages;
  stats->flash_pages = s_plan.flash_pages;
  stats->psram_capacity_pages = psram_capacity_pages();
  stats->flash_capacity_pages = flash_capacity;

  file = fopen(path, "rb");
  compare_file = fopen(path, "rb");
  if (file == NULL || compare_file == NULL)
  {
    if (file != NULL)
      fclose(file);
    if (compare_file != NULL)
      fclose(compare_file);
    set_error(error, error_bytes, "cannot reopen selected ROM");
    return false;
  }
  success = load_pages(
      file, compare_file, path, rom_bytes, page_count,
      stats, error, error_bytes);
  fclose(file);
  fclose(compare_file);
  if (!success)
    return false;

  s_page_count = page_count;
  if (!map_flash_pages(error, error_bytes))
  {
    reset_source();
    return false;
  }
  for (uint32_t page = 0u; page < page_count; page++)
  {
    if (s_page_pointer[page] == NULL)
    {
      set_error(error, error_bytes, "missing page mapping at %u",
                (unsigned)page);
      reset_source();
      return false;
    }
  }
#if defined(GPSP_ESP32S31_PSRAM_FAULT_TRACE) && \
    GPSP_ESP32S31_PSRAM_FAULT_TRACE
  if (!verify_loaded_pages(
          path, rom_bytes, page_count, error, error_bytes))
  {
    reset_source();
    return false;
  }
#endif

  gamepak_set_direct_mmap_source(
      esp32s31_runtime_gamepak_resolve_page, NULL, rom_bytes);
  info->path = NULL;
  info->data = s_page_pointer[0u];
  info->size = rom_bytes;
  info->meta = NULL;
  if (error != NULL && error_bytes != 0u)
    error[0] = '\0';

  ESP_LOGI(TAG,
           "runtime ROM mapped: bytes=%" PRIu32 " pages=%" PRIu32
           " unique=%" PRIu32 " duplicate=%" PRIu32
           " psram=%" PRIu32 "/%" PRIu32
           " flash=%" PRIu32 "/%" PRIu32
           " flash_skip_blocks=%" PRIu32
           " flash_write_blocks=%" PRIu32
           " erased_4k=%" PRIu32,
           stats->rom_bytes, stats->logical_pages,
           stats->unique_pages, stats->duplicate_pages,
           stats->psram_pages, stats->psram_capacity_pages,
           stats->flash_pages, stats->flash_capacity_pages,
           stats->flash_blocks_skipped, stats->flash_blocks_written,
           stats->flash_sectors_erased);
  return true;
}
