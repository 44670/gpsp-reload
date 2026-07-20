#ifndef GPSP_ESP32S31_GAMEPAK_RUNTIME_H
#define GPSP_ESP32S31_GAMEPAK_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libretro.h>

typedef struct
{
  uint32_t rom_bytes;
  uint32_t logical_pages;
  uint32_t unique_pages;
  uint32_t duplicate_pages;
  uint32_t psram_pages;
  uint32_t flash_pages;
  uint32_t psram_capacity_pages;
  uint32_t flash_capacity_pages;
  uint32_t flash_blocks_skipped;
  uint32_t flash_blocks_written;
  uint32_t flash_sectors_erased;
} esp32s31_runtime_gamepak_stats_t;

/*
 * Before ROM loading begins, the menu may use the two work pages at the front
 * of the static container. The workspace becomes invalid as soon as
 * esp32s31_runtime_gamepak_load() starts.
 */
void *esp32s31_runtime_gamepak_menu_workspace(size_t *workspace_bytes);

/*
 * Hashes 32 KiB pages, confirms hash hits byte-for-byte, then loads each
 * unique page into the static PSRAM pool first. Unique pages that do not fit
 * there may spill into the gamepak partition.
 */
bool esp32s31_runtime_gamepak_load(
    const char *path, struct retro_game_info *info,
    esp32s31_runtime_gamepak_stats_t *stats,
    char *error, size_t error_bytes);

const uint8_t *esp32s31_runtime_gamepak_resolve_page(
    void *context, uint32_t logical_page);

#endif
