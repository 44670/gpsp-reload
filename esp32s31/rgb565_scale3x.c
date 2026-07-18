#include "rgb565_scale3x.h"

#include <string.h>

bool esp32s31_rgb565_clear_output(void *output, size_t output_pitch)
{
  if (output == NULL ||
      output_pitch < ESP32S31_LCD_WIDTH * sizeof(uint16_t))
    return false;

  uint8_t *row = (uint8_t *)output;
  for (unsigned y = 0; y < ESP32S31_LCD_HEIGHT; y++)
  {
    memset(row, 0, ESP32S31_LCD_WIDTH * sizeof(uint16_t));
    row += output_pitch;
  }
  return true;
}

bool esp32s31_rgb565_scale3x(void *output, size_t output_pitch,
                            const void *input, unsigned width,
                            unsigned height, size_t input_pitch)
{
  if (output == NULL || input == NULL || width != ESP32S31_GBA_WIDTH ||
      height != ESP32S31_GBA_HEIGHT ||
      output_pitch < ESP32S31_LCD_WIDTH * sizeof(uint16_t) ||
      input_pitch < ESP32S31_GBA_WIDTH * sizeof(uint16_t))
    return false;

  const uint8_t *source_row = (const uint8_t *)input;
  uint8_t *destination_row = (uint8_t *)output;
  const size_t scaled_row_bytes =
      ESP32S31_GBA_WIDTH * ESP32S31_SCALE_FACTOR * sizeof(uint16_t);
  const size_t active_offset = ESP32S31_LCD_BAR_WIDTH * sizeof(uint16_t);

  for (unsigned source_y = 0; source_y < ESP32S31_GBA_HEIGHT; source_y++)
  {
    const uint16_t *source = (const uint16_t *)source_row;
    uint16_t *scaled = (uint16_t *)(destination_row + active_offset);

    for (unsigned source_x = 0; source_x < ESP32S31_GBA_WIDTH; source_x++)
    {
      const uint16_t pixel = source[source_x];
      scaled[source_x * 3u + 0u] = pixel;
      scaled[source_x * 3u + 1u] = pixel;
      scaled[source_x * 3u + 2u] = pixel;
    }

    memcpy(destination_row + output_pitch + active_offset, scaled,
           scaled_row_bytes);
    memcpy(destination_row + output_pitch * 2u + active_offset, scaled,
           scaled_row_bytes);

    source_row += input_pitch;
    destination_row += output_pitch * ESP32S31_SCALE_FACTOR;
  }
  return true;
}
