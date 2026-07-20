#include "menu_ui.h"

#include <ctype.h>
#include <stdbool.h>

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_ADVANCE 6

static const uint8_t s_digits[10][FONT_WIDTH] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e},
    {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e},
};

static const uint8_t s_letters[26][FONT_WIDTH] = {
    {0x7e, 0x11, 0x11, 0x11, 0x7e},
    {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22},
    {0x7f, 0x41, 0x41, 0x22, 0x1c},
    {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a},
    {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3f, 0x01},
    {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f},
    {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e},
    {0x7f, 0x09, 0x09, 0x09, 0x06},
    {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f},
    {0x1f, 0x20, 0x40, 0x20, 0x1f},
    {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
};

static const uint8_t *glyph_for(char character)
{
  static const uint8_t blank[FONT_WIDTH] = {0, 0, 0, 0, 0};
  static const uint8_t unknown[FONT_WIDTH] = {0x02, 0x01, 0x51, 0x09, 0x06};
  static const uint8_t dot[FONT_WIDTH] = {0, 0x60, 0x60, 0, 0};
  static const uint8_t comma[FONT_WIDTH] = {0, 0x50, 0x30, 0, 0};
  static const uint8_t colon[FONT_WIDTH] = {0, 0x36, 0x36, 0, 0};
  static const uint8_t dash[FONT_WIDTH] = {0x08, 0x08, 0x08, 0x08, 0x08};
  static const uint8_t underscore[FONT_WIDTH] = {0x40, 0x40, 0x40, 0x40, 0x40};
  static const uint8_t slash[FONT_WIDTH] = {0x20, 0x10, 0x08, 0x04, 0x02};
  static const uint8_t backslash[FONT_WIDTH] = {0x02, 0x04, 0x08, 0x10, 0x20};
  static const uint8_t left_bracket[FONT_WIDTH] = {0, 0x7f, 0x41, 0x41, 0};
  static const uint8_t right_bracket[FONT_WIDTH] = {0, 0x41, 0x41, 0x7f, 0};
  static const uint8_t left_paren[FONT_WIDTH] = {0, 0x1c, 0x22, 0x41, 0};
  static const uint8_t right_paren[FONT_WIDTH] = {0, 0x41, 0x22, 0x1c, 0};
  static const uint8_t less[FONT_WIDTH] = {0x08, 0x14, 0x22, 0x41, 0};
  static const uint8_t greater[FONT_WIDTH] = {0, 0x41, 0x22, 0x14, 0x08};
  static const uint8_t plus[FONT_WIDTH] = {0x08, 0x08, 0x3e, 0x08, 0x08};
  static const uint8_t equals[FONT_WIDTH] = {0x14, 0x14, 0x14, 0x14, 0x14};
  static const uint8_t percent[FONT_WIDTH] = {0x63, 0x13, 0x08, 0x64, 0x63};

  const unsigned value = (unsigned char)character;
  if (value >= '0' && value <= '9')
    return s_digits[value - '0'];
  if (value >= 'a' && value <= 'z')
    return s_letters[value - 'a'];
  if (value >= 'A' && value <= 'Z')
    return s_letters[value - 'A'];

  switch (character)
  {
    case ' ': return blank;
    case '.': return dot;
    case ',': return comma;
    case ':': return colon;
    case '-': return dash;
    case '_': return underscore;
    case '/': return slash;
    case '\\': return backslash;
    case '[': return left_bracket;
    case ']': return right_bracket;
    case '(': return left_paren;
    case ')': return right_paren;
    case '<': return less;
    case '>': return greater;
    case '+': return plus;
    case '=': return equals;
    case '%': return percent;
    default: return unknown;
  }
}

void esp32s31_menu_fill(uint16_t *pixels, uint16_t color)
{
  if (pixels == NULL)
    return;
  for (size_t i = 0u; i < ESP32S31_MENU_WIDTH * ESP32S31_MENU_HEIGHT; i++)
    pixels[i] = color;
}

void esp32s31_menu_fill_rect(uint16_t *pixels, int x, int y,
                             int width, int height, uint16_t color)
{
  if (pixels == NULL || width <= 0 || height <= 0)
    return;

  int left = x < 0 ? 0 : x;
  int top = y < 0 ? 0 : y;
  int right = x + width;
  int bottom = y + height;
  if (right > (int)ESP32S31_MENU_WIDTH)
    right = (int)ESP32S31_MENU_WIDTH;
  if (bottom > (int)ESP32S31_MENU_HEIGHT)
    bottom = (int)ESP32S31_MENU_HEIGHT;
  if (left >= right || top >= bottom)
    return;

  for (int row = top; row < bottom; row++)
  {
    uint16_t *destination = pixels +
        (size_t)row * ESP32S31_MENU_WIDTH + (size_t)left;
    for (int column = left; column < right; column++)
      *destination++ = color;
  }
}

void esp32s31_menu_draw_text(uint16_t *pixels, int x, int y,
                             int max_width, const char *text,
                             uint16_t foreground,
                             uint16_t background)
{
  if (pixels == NULL || text == NULL || max_width <= 0)
    return;

  const int limit = x + max_width;
  for (size_t index = 0u; text[index] != '\0'; index++)
  {
    if (x + FONT_WIDTH > limit)
      break;
    const uint8_t *glyph = glyph_for(text[index]);
    for (int glyph_y = 0; glyph_y < FONT_HEIGHT; glyph_y++)
    {
      const int output_y = y + glyph_y;
      if (output_y < 0 || output_y >= (int)ESP32S31_MENU_HEIGHT)
        continue;
      for (int glyph_x = 0; glyph_x < FONT_ADVANCE; glyph_x++)
      {
        const int output_x = x + glyph_x;
        if (output_x < 0 || output_x >= (int)ESP32S31_MENU_WIDTH ||
            output_x >= limit)
          continue;
        const bool set = glyph_x < FONT_WIDTH &&
            (glyph[glyph_x] & (uint8_t)(1u << glyph_y)) != 0u;
        pixels[(size_t)output_y * ESP32S31_MENU_WIDTH +
               (size_t)output_x] = set ? foreground : background;
      }
    }
    x += FONT_ADVANCE;
  }
}

void esp32s31_menu_draw_progress(uint16_t *pixels, int x, int y,
                                 int width, int height,
                                 uint32_t completed, uint32_t total,
                                 uint16_t foreground,
                                 uint16_t background,
                                 uint16_t border)
{
  if (pixels == NULL || width < 3 || height < 3)
    return;

  esp32s31_menu_fill_rect(pixels, x, y, width, height, border);
  esp32s31_menu_fill_rect(pixels, x + 1, y + 1, width - 2, height - 2,
                          background);
  if (total == 0u)
    return;
  if (completed > total)
    completed = total;
  const uint32_t inner = (uint32_t)(width - 2);
  const int filled = (int)(((uint64_t)inner * completed) / total);
  esp32s31_menu_fill_rect(pixels, x + 1, y + 1, filled, height - 2,
                          foreground);
}
