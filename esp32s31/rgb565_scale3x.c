#include "rgb565_scale3x.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define ESP32S31_IRAM_ATTR IRAM_ATTR
#else
#define ESP32S31_IRAM_ATTR
#endif

#define FPS_GLYPH_WIDTH 5u
#define FPS_GLYPH_HEIGHT 7u
#define FPS_GLYPH_ADVANCE 6u
#define FPS_GLYPH_SCALE 2u
#define FPS_TEXT_LENGTH 8u
#define FPS_TEXT_X (ESP32S31_LCD_BAR_WIDTH + 4u)
#define FPS_TEXT_Y 4u
#define FPS_BACKGROUND_X (ESP32S31_LCD_BAR_WIDTH + 2u)
#define FPS_BACKGROUND_Y 2u
#define FPS_BACKGROUND_WIDTH \
  (FPS_TEXT_LENGTH * FPS_GLYPH_ADVANCE * FPS_GLYPH_SCALE)
#define FPS_BACKGROUND_HEIGHT \
  (FPS_GLYPH_HEIGHT * FPS_GLYPH_SCALE + 4u)

static const uint8_t glyph_f[FPS_GLYPH_HEIGHT] = {
    0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x10u,
};
static const uint8_t glyph_p[FPS_GLYPH_HEIGHT] = {
    0x1eu, 0x11u, 0x11u, 0x1eu, 0x10u, 0x10u, 0x10u,
};
static const uint8_t glyph_s[FPS_GLYPH_HEIGHT] = {
    0x0fu, 0x10u, 0x10u, 0x0eu, 0x01u, 0x01u, 0x1eu,
};
static const uint8_t glyph_dot[FPS_GLYPH_HEIGHT] = {
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0cu, 0x0cu,
};
static const uint8_t glyph_digits[10][FPS_GLYPH_HEIGHT] = {
    {0x0eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0eu},
    {0x04u, 0x0cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu},
    {0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1fu},
    {0x1eu, 0x01u, 0x01u, 0x0eu, 0x01u, 0x01u, 0x1eu},
    {0x02u, 0x06u, 0x0au, 0x12u, 0x1fu, 0x02u, 0x02u},
    {0x1fu, 0x10u, 0x10u, 0x1eu, 0x01u, 0x01u, 0x1eu},
    {0x0eu, 0x10u, 0x10u, 0x1eu, 0x11u, 0x11u, 0x0eu},
    {0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u},
    {0x0eu, 0x11u, 0x11u, 0x0eu, 0x11u, 0x11u, 0x0eu},
    {0x0eu, 0x11u, 0x11u, 0x0fu, 0x01u, 0x01u, 0x0eu},
};

static const uint8_t *fps_glyph(char character)
{
  if (character >= '0' && character <= '9')
    return glyph_digits[(unsigned)(character - '0')];
  if (character == 'F')
    return glyph_f;
  if (character == 'P')
    return glyph_p;
  if (character == 'S')
    return glyph_s;
  if (character == '.')
    return glyph_dot;
  return NULL;
}

static void draw_fps_glyph(uint16_t *output, size_t output_pitch_pixels,
                           unsigned x, unsigned y, unsigned scale,
                           char character)
{
  const uint8_t *glyph = fps_glyph(character);
  if (glyph == NULL)
    return;

  for (unsigned glyph_y = 0; glyph_y < FPS_GLYPH_HEIGHT; glyph_y++)
  {
    for (unsigned glyph_x = 0; glyph_x < FPS_GLYPH_WIDTH; glyph_x++)
    {
      if ((glyph[glyph_y] & (1u << (FPS_GLYPH_WIDTH - glyph_x - 1u))) == 0u)
        continue;

      for (unsigned duplicate_y = 0; duplicate_y < scale;
           duplicate_y++)
      {
        uint16_t *row = output +
            (y + glyph_y * scale + duplicate_y) *
                output_pitch_pixels;
        for (unsigned duplicate_x = 0; duplicate_x < scale;
             duplicate_x++)
          row[x + glyph_x * scale + duplicate_x] = 0xffffu;
      }
    }
  }
}

static void format_fps_text(char text[FPS_TEXT_LENGTH], unsigned fps_x10)
{
  if (fps_x10 > 999u)
    fps_x10 = 999u;

  const unsigned whole = fps_x10 / 10u;
  const char formatted[FPS_TEXT_LENGTH] = {
      'F', 'P', 'S', ' ',
      whole >= 10u ? (char)('0' + whole / 10u) : ' ',
      (char)('0' + whole % 10u), '.', (char)('0' + fps_x10 % 10u),
  };
  memcpy(text, formatted, sizeof(formatted));
}

bool esp32s31_rgb565_prepare_fps_osd(
    esp32s31_rgb565_fps_osd_t *osd, unsigned fps_x10)
{
  if (osd == NULL)
    return false;

  memset(osd, 0, sizeof(*osd));
  char text[FPS_TEXT_LENGTH];
  format_fps_text(text, fps_x10);
  for (unsigned i = 0; i < FPS_TEXT_LENGTH; i++)
  {
    const uint8_t *glyph = fps_glyph(text[i]);
    if (glyph == NULL)
      continue;

    const unsigned glyph_left = 1u + i * FPS_GLYPH_ADVANCE;
    for (unsigned y = 0; y < FPS_GLYPH_HEIGHT; y++)
    {
      for (unsigned x = 0; x < FPS_GLYPH_WIDTH; x++)
      {
        if ((glyph[y] & (1u << (FPS_GLYPH_WIDTH - x - 1u))) == 0u)
          continue;
        const unsigned output_x = glyph_left + x;
        osd->white_rows[y + 1u][output_x / 32u] |=
            UINT32_C(1) << (output_x % 32u);
      }
    }
  }
  return true;
}

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

bool ESP32S31_IRAM_ATTR esp32s31_rgb565_scale3x_rows_osd(
    void *output, size_t output_pitch, const void *input,
    unsigned source_y_offset, unsigned source_rows, size_t input_pitch,
    const esp32s31_rgb565_fps_osd_t *osd)
{
  if (output == NULL || input == NULL || source_rows == 0u ||
      source_y_offset >= ESP32S31_GBA_HEIGHT ||
      source_rows > ESP32S31_GBA_HEIGHT - source_y_offset ||
      output_pitch < ESP32S31_LCD_WIDTH * sizeof(uint16_t) ||
      input_pitch < ESP32S31_GBA_WIDTH * sizeof(uint16_t))
    return false;

  const uint8_t *source_row = (const uint8_t *)input;
  uint8_t *destination_row = (uint8_t *)output;
  const size_t scaled_row_bytes =
      ESP32S31_GBA_WIDTH * ESP32S31_SCALE_FACTOR * sizeof(uint16_t);
  const size_t active_offset = ESP32S31_LCD_BAR_WIDTH * sizeof(uint16_t);

  for (unsigned source_y = 0; source_y < source_rows; source_y++)
  {
    const uint16_t *source = (const uint16_t *)source_row;
    uint16_t *scaled = (uint16_t *)(destination_row + active_offset);
    unsigned first_source_x = 0;

    const unsigned frame_y = source_y_offset + source_y;
    if (osd != NULL && frame_y < ESP32S31_GBA_FPS_OSD_HEIGHT)
    {
      const uint32_t *white = osd->white_rows[frame_y];
      for (unsigned source_x = 0;
           source_x < ESP32S31_GBA_FPS_OSD_WIDTH; source_x++)
      {
        const uint16_t pixel =
            (white[source_x / 32u] &
             (UINT32_C(1) << (source_x % 32u))) != 0u
                ? UINT16_C(0xffff)
                : UINT16_C(0x0000);
        scaled[source_x * 3u + 0u] = pixel;
        scaled[source_x * 3u + 1u] = pixel;
        scaled[source_x * 3u + 2u] = pixel;
      }
      first_source_x = ESP32S31_GBA_FPS_OSD_WIDTH;
    }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    const uint16_t *remaining_source = source + first_source_x;
    uint16_t *remaining_scaled = scaled + first_source_x * 3u;
    if ((((uintptr_t)remaining_source | (uintptr_t)remaining_scaled) & 3u) ==
         0u)
    {
      const uint32_t *source_pairs = (const uint32_t *)remaining_source;
      uint32_t *scaled_pairs = (uint32_t *)remaining_scaled;
      const unsigned pair_count =
          (ESP32S31_GBA_WIDTH - first_source_x) / 2u;
      for (unsigned pair = 0; pair < pair_count; pair++)
      {
        const uint32_t pixels = source_pairs[pair];
        const uint32_t first = pixels & 0xffffu;
        const uint32_t second = pixels >> 16;
        scaled_pairs[pair * 3u + 0u] = first | (first << 16);
        scaled_pairs[pair * 3u + 1u] = pixels;
        scaled_pairs[pair * 3u + 2u] = second | (second << 16);
      }
    }
    else
#endif
    {
      for (unsigned source_x = first_source_x;
           source_x < ESP32S31_GBA_WIDTH; source_x++)
      {
        const uint16_t pixel = source[source_x];
        scaled[source_x * 3u + 0u] = pixel;
        scaled[source_x * 3u + 1u] = pixel;
        scaled[source_x * 3u + 2u] = pixel;
      }
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

bool ESP32S31_IRAM_ATTR esp32s31_rgb565_scale3x_rows(
    void *output, size_t output_pitch, const void *input,
    unsigned source_rows, size_t input_pitch)
{
  return esp32s31_rgb565_scale3x_rows_osd(
      output, output_pitch, input, 0u, source_rows, input_pitch, NULL);
}

bool esp32s31_rgb565_scale3x(void *output, size_t output_pitch,
                            const void *input, unsigned width,
                            unsigned height, size_t input_pitch)
{
  if (width != ESP32S31_GBA_WIDTH || height != ESP32S31_GBA_HEIGHT)
    return false;
  return esp32s31_rgb565_scale3x_rows(
      output, output_pitch, input, height, input_pitch);
}

bool esp32s31_rgb565_draw_fps(void *output, size_t output_pitch,
                             unsigned fps_x10)
{
  if (output == NULL ||
      output_pitch < ESP32S31_LCD_WIDTH * sizeof(uint16_t))
    return false;

  char text[FPS_TEXT_LENGTH];
  format_fps_text(text, fps_x10);
  uint16_t *pixels = (uint16_t *)output;
  const size_t pitch_pixels = output_pitch / sizeof(uint16_t);

  for (unsigned y = FPS_BACKGROUND_Y;
       y < FPS_BACKGROUND_Y + FPS_BACKGROUND_HEIGHT; y++)
  {
    memset(pixels + y * pitch_pixels + FPS_BACKGROUND_X, 0,
           FPS_BACKGROUND_WIDTH * sizeof(uint16_t));
  }

  for (unsigned i = 0; i < FPS_TEXT_LENGTH; i++)
  {
    draw_fps_glyph(pixels, pitch_pixels,
                   FPS_TEXT_X + i * FPS_GLYPH_ADVANCE * FPS_GLYPH_SCALE,
                   FPS_TEXT_Y, FPS_GLYPH_SCALE, text[i]);
  }
  return true;
}
