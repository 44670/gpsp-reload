#ifndef ESP32S31_GAMEPAK_CONTAINER_H
#define ESP32S31_GAMEPAK_CONTAINER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32S31_GAMEPAK_MAGIC "GPSPGBAP"
#define ESP32S31_GAMEPAK_MAGIC_BYTES 8u
#define ESP32S31_GAMEPAK_VERSION 1u
#define ESP32S31_GAMEPAK_HEADER_BYTES 0x8000u
#define ESP32S31_GAMEPAK_PAGE_BYTES 0x8000u
#define ESP32S31_GAMEPAK_MAX_PAGES 1024u
#define ESP32S31_GAMEPAK_TABLE_OFFSET 0x100u
#define ESP32S31_GAMEPAK_TABLE_ENTRY_BYTES 4u
#define ESP32S31_GAMEPAK_FLAG_DEDUP UINT32_C(0x00000001)

/* Little-endian, fixed-width fields in the first 32 KiB flash block. */
#define ESP32S31_GAMEPAK_OFF_MAGIC 0x00u
#define ESP32S31_GAMEPAK_OFF_VERSION 0x08u
#define ESP32S31_GAMEPAK_OFF_HEADER_BYTES 0x0cu
#define ESP32S31_GAMEPAK_OFF_PAGE_BYTES 0x10u
#define ESP32S31_GAMEPAK_OFF_ROM_BYTES 0x14u
#define ESP32S31_GAMEPAK_OFF_PAGE_COUNT 0x18u
#define ESP32S31_GAMEPAK_OFF_STORED_PAGE_COUNT 0x1cu
#define ESP32S31_GAMEPAK_OFF_DATA_OFFSET 0x20u
#define ESP32S31_GAMEPAK_OFF_IMAGE_BYTES 0x24u
#define ESP32S31_GAMEPAK_OFF_TABLE_OFFSET 0x28u
#define ESP32S31_GAMEPAK_OFF_TABLE_ENTRY_BYTES 0x2cu
#define ESP32S31_GAMEPAK_OFF_ROM_CRC32 0x30u
#define ESP32S31_GAMEPAK_OFF_HEADER_CRC32 0x34u
#define ESP32S31_GAMEPAK_OFF_FLAGS 0x38u

typedef struct
{
  const uint8_t *image;
  size_t mapped_bytes;
  uint32_t rom_bytes;
  uint32_t page_count;
  uint32_t stored_page_count;
  uint32_t data_offset;
  uint32_t image_bytes;
  uint32_t table_offset;
  uint32_t rom_crc32;
} esp32s31_gamepak_container_t;

bool esp32s31_gamepak_container_probe(const void *image, size_t mapped_bytes);

bool esp32s31_gamepak_container_open(
    const void *image, size_t mapped_bytes,
    esp32s31_gamepak_container_t *container,
    char *error, size_t error_bytes);

const uint8_t *esp32s31_gamepak_container_page(
    const esp32s31_gamepak_container_t *container, uint32_t logical_page);

uint32_t esp32s31_gamepak_container_page_offset(
    const esp32s31_gamepak_container_t *container, uint32_t logical_page);

uint32_t esp32s31_gamepak_crc32(const void *data, size_t bytes);

/* Returns the optional direct-boot layout probe reservation. */
void *esp32s31_gamepak_layout_probe_pad(size_t *bytes);

#endif
