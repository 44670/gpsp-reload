#ifndef GPSP_GAMEPAK_DIRECT_MMAP_H
#define GPSP_GAMEPAK_DIRECT_MMAP_H

#include <stdbool.h>
#include <stdint.h>

#ifndef GPSP_DIRECT_MMAP_GAMEPAK
#define GPSP_DIRECT_MMAP_GAMEPAK 0
#endif

#if GPSP_DIRECT_MMAP_GAMEPAK

#ifdef __cplusplus
extern "C" {
#endif

typedef const uint8_t *(*gamepak_direct_page_resolver_t)(
    void *context, uint32_t logical_page);

/*
 * Configure a page-addressable, immutable ROM source. Each resolver result
 * must remain valid and directly readable for the lifetime of the loaded ROM.
 */
void gamepak_set_direct_mmap_source(
    gamepak_direct_page_resolver_t resolver, void *context,
    uint32_t rom_bytes);

/* Read the virtual GPIO bytes at ROM offsets 0xc4..0xc9 without modifying ROM. */
uint32_t gamepak_direct_gpio_read(uint32_t address, unsigned width_bytes);

#ifdef __cplusplus
}
#endif

static inline bool gamepak_direct_gpio_read_needed(
    uint32_t address, unsigned width_bytes)
{
  const uint32_t region = address >> 24;
  const uint32_t offset = address & UINT32_C(0x01ffffff);
  return region >= 0x08u && region <= 0x0cu &&
         offset <= 0xc9u && offset + width_bytes > 0xc4u;
}

#else

static inline bool gamepak_direct_gpio_read_needed(
    uint32_t address, unsigned width_bytes)
{
  (void)address;
  (void)width_bytes;
  return false;
}

#endif

#endif
