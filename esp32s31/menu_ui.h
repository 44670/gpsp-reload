#ifndef GPSP_ESP32S31_MENU_UI_H
#define GPSP_ESP32S31_MENU_UI_H

#include <stddef.h>
#include <stdint.h>

#define ESP32S31_MENU_WIDTH 240u
#define ESP32S31_MENU_HEIGHT 160u
#define ESP32S31_MENU_PITCH_BYTES \
  (ESP32S31_MENU_WIDTH * sizeof(uint16_t))

void esp32s31_menu_fill(uint16_t *pixels, uint16_t color);

void esp32s31_menu_fill_rect(uint16_t *pixels, int x, int y,
                             int width, int height, uint16_t color);

void esp32s31_menu_draw_text(uint16_t *pixels, int x, int y,
                             int max_width, const char *text,
                             uint16_t foreground,
                             uint16_t background);

void esp32s31_menu_draw_progress(uint16_t *pixels, int x, int y,
                                 int width, int height,
                                 uint32_t completed, uint32_t total,
                                 uint16_t foreground,
                                 uint16_t background,
                                 uint16_t border);

#endif
