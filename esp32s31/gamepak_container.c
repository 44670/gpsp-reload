#include "gamepak_container.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"

#ifndef ESP32S31_LAYOUT_PROBE_PAD_BYTES
#define ESP32S31_LAYOUT_PROBE_PAD_BYTES 0
#endif

#if ESP32S31_LAYOUT_PROBE_PAD_BYTES > 0
static EXT_RAM_BSS_ATTR uint8_t s_layout_probe_pad[
    ESP32S31_LAYOUT_PROBE_PAD_BYTES]
    __attribute__((aligned(ESP32S31_GAMEPAK_PAGE_BYTES)));
#endif

void *esp32s31_gamepak_layout_probe_pad(size_t *bytes)
{
  if (bytes != NULL)
    *bytes = ESP32S31_LAYOUT_PROBE_PAD_BYTES;
#if ESP32S31_LAYOUT_PROBE_PAD_BYTES > 0
  return s_layout_probe_pad;
#else
  return NULL;
#endif
}

static uint32_t read_le32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

static void set_error(char *error, size_t error_bytes, const char *format, ...)
{
  if (error == NULL || error_bytes == 0u)
    return;

  va_list args;
  va_start(args, format);
  (void)vsnprintf(error, error_bytes, format, args);
  va_end(args);
}

static uint32_t crc32_byte(uint32_t crc, uint8_t value)
{
  crc ^= value;
  for (unsigned bit = 0; bit < 8u; bit++)
  {
    const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
    crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
  }
  return crc;
}

uint32_t esp32s31_gamepak_crc32(const void *data, size_t bytes)
{
  const uint8_t *source = (const uint8_t *)data;
  uint32_t crc = UINT32_C(0xffffffff);

  for (size_t i = 0; i < bytes; i++)
    crc = crc32_byte(crc, source[i]);
  return ~crc;
}

bool esp32s31_gamepak_container_probe(const void *image, size_t mapped_bytes)
{
  return image != NULL && mapped_bytes >= ESP32S31_GAMEPAK_MAGIC_BYTES &&
         memcmp(image, ESP32S31_GAMEPAK_MAGIC,
                ESP32S31_GAMEPAK_MAGIC_BYTES) == 0;
}

static uint32_t header_crc32(const uint8_t *image)
{
  uint32_t crc = UINT32_C(0xffffffff);
  for (size_t i = 0; i < ESP32S31_GAMEPAK_HEADER_BYTES; i++)
  {
    const bool crc_field =
        i >= ESP32S31_GAMEPAK_OFF_HEADER_CRC32 &&
        i < ESP32S31_GAMEPAK_OFF_HEADER_CRC32 + sizeof(uint32_t);
    crc = crc32_byte(crc, crc_field ? 0u : image[i]);
  }
  return ~crc;
}

bool esp32s31_gamepak_container_open(
    const void *image_ptr, size_t mapped_bytes,
    esp32s31_gamepak_container_t *container,
    char *error, size_t error_bytes)
{
  const uint8_t *image = (const uint8_t *)image_ptr;
  if (container == NULL)
  {
    set_error(error, error_bytes, "missing output view");
    return false;
  }
  memset(container, 0, sizeof(*container));

  if (!esp32s31_gamepak_container_probe(image, mapped_bytes))
  {
    set_error(error, error_bytes, "bad or missing magic");
    return false;
  }
  if (mapped_bytes < ESP32S31_GAMEPAK_HEADER_BYTES)
  {
    set_error(error, error_bytes, "mapped image is smaller than header");
    return false;
  }

  const uint32_t version =
      read_le32(image + ESP32S31_GAMEPAK_OFF_VERSION);
  const uint32_t header_bytes =
      read_le32(image + ESP32S31_GAMEPAK_OFF_HEADER_BYTES);
  const uint32_t page_bytes =
      read_le32(image + ESP32S31_GAMEPAK_OFF_PAGE_BYTES);
  const uint32_t rom_bytes =
      read_le32(image + ESP32S31_GAMEPAK_OFF_ROM_BYTES);
  const uint32_t page_count =
      read_le32(image + ESP32S31_GAMEPAK_OFF_PAGE_COUNT);
  const uint32_t stored_page_count =
      read_le32(image + ESP32S31_GAMEPAK_OFF_STORED_PAGE_COUNT);
  const uint32_t data_offset =
      read_le32(image + ESP32S31_GAMEPAK_OFF_DATA_OFFSET);
  const uint32_t image_bytes =
      read_le32(image + ESP32S31_GAMEPAK_OFF_IMAGE_BYTES);
  const uint32_t table_offset =
      read_le32(image + ESP32S31_GAMEPAK_OFF_TABLE_OFFSET);
  const uint32_t table_entry_bytes =
      read_le32(image + ESP32S31_GAMEPAK_OFF_TABLE_ENTRY_BYTES);
  const uint32_t rom_crc32 =
      read_le32(image + ESP32S31_GAMEPAK_OFF_ROM_CRC32);
  const uint32_t expected_header_crc32 =
      read_le32(image + ESP32S31_GAMEPAK_OFF_HEADER_CRC32);
  const uint32_t flags =
      read_le32(image + ESP32S31_GAMEPAK_OFF_FLAGS);

  if (version != ESP32S31_GAMEPAK_VERSION ||
      header_bytes != ESP32S31_GAMEPAK_HEADER_BYTES ||
      page_bytes != ESP32S31_GAMEPAK_PAGE_BYTES ||
      data_offset != ESP32S31_GAMEPAK_HEADER_BYTES ||
      table_entry_bytes != ESP32S31_GAMEPAK_TABLE_ENTRY_BYTES ||
      flags != ESP32S31_GAMEPAK_FLAG_DEDUP)
  {
    set_error(error, error_bytes, "unsupported header geometry or flags");
    return false;
  }
  if (rom_bytes == 0u || rom_bytes > 32u * 1024u * 1024u ||
      page_count == 0u || page_count > ESP32S31_GAMEPAK_MAX_PAGES ||
      page_count !=
          (rom_bytes + ESP32S31_GAMEPAK_PAGE_BYTES - 1u) /
              ESP32S31_GAMEPAK_PAGE_BYTES)
  {
    set_error(error, error_bytes, "invalid logical ROM size or page count");
    return false;
  }
  if (stored_page_count == 0u || stored_page_count > page_count ||
      image_bytes != data_offset +
                         stored_page_count * ESP32S31_GAMEPAK_PAGE_BYTES ||
      image_bytes > mapped_bytes)
  {
    set_error(error, error_bytes, "invalid stored-page extent");
    return false;
  }
  if (table_offset < 0x40u ||
      table_offset + page_count * sizeof(uint32_t) > header_bytes)
  {
    set_error(error, error_bytes, "page table is outside header");
    return false;
  }
  const uint32_t actual_header_crc32 = header_crc32(image);
  if (actual_header_crc32 != expected_header_crc32)
  {
    set_error(error, error_bytes,
              "header CRC mismatch expected=%08x actual=%08x",
              (unsigned)expected_header_crc32,
              (unsigned)actual_header_crc32);
    return false;
  }

  for (uint32_t page = 0; page < page_count; page++)
  {
    const uint32_t offset =
        read_le32(image + table_offset + page * sizeof(uint32_t));
    if (offset < data_offset ||
        (offset - data_offset) % ESP32S31_GAMEPAK_PAGE_BYTES != 0u ||
        offset > image_bytes - ESP32S31_GAMEPAK_PAGE_BYTES)
    {
      set_error(error, error_bytes, "invalid page offset at index %u",
                (unsigned)page);
      return false;
    }
  }

  container->image = image;
  container->mapped_bytes = mapped_bytes;
  container->rom_bytes = rom_bytes;
  container->page_count = page_count;
  container->stored_page_count = stored_page_count;
  container->data_offset = data_offset;
  container->image_bytes = image_bytes;
  container->table_offset = table_offset;
  container->rom_crc32 = rom_crc32;
  if (error != NULL && error_bytes != 0u)
    error[0] = '\0';
  return true;
}

uint32_t esp32s31_gamepak_container_page_offset(
    const esp32s31_gamepak_container_t *container, uint32_t logical_page)
{
  if (container == NULL || container->image == NULL ||
      logical_page >= container->page_count)
    return UINT32_MAX;
  return read_le32(container->image + container->table_offset +
                   logical_page * sizeof(uint32_t));
}

const uint8_t *esp32s31_gamepak_container_page(
    const esp32s31_gamepak_container_t *container, uint32_t logical_page)
{
  const uint32_t offset =
      esp32s31_gamepak_container_page_offset(container, logical_page);
  if (offset == UINT32_MAX)
    return NULL;
  return container->image + offset;
}
