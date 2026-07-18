#ifndef GPSP_ESP32S31_RGB565_SCALE3X_H
#define GPSP_ESP32S31_RGB565_SCALE3X_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32S31_GBA_WIDTH 240u
#define ESP32S31_GBA_HEIGHT 160u
#define ESP32S31_LCD_WIDTH 800u
#define ESP32S31_LCD_HEIGHT 480u
#define ESP32S31_SCALE_FACTOR 3u
#define ESP32S31_LCD_BAR_WIDTH 40u

/* Clear an entire 800x480 RGB565 output buffer to black. */
bool esp32s31_rgb565_clear_output(void *output, size_t output_pitch);

/*
 * Scale exactly 240x160 RGB565 to the centered 720x480 panel region.
 * The 40-pixel side bars are deliberately left untouched; clear them once
 * with esp32s31_rgb565_clear_output() before the first frame.
 */
bool esp32s31_rgb565_scale3x(void *output, size_t output_pitch,
                            const void *input, unsigned width,
                            unsigned height, size_t input_pitch);
bool esp32s31_rgb565_scale3x_rows(void *output, size_t output_pitch,
                                 const void *input, unsigned source_rows,
                                 size_t input_pitch);

/* Legacy helper for drawing directly into an 800x480 LCD framebuffer. */
bool esp32s31_rgb565_draw_fps(void *output, size_t output_pitch,
                             unsigned fps_x10);
/* Draw the OSD into the native 240x160 GBA image before scaling. */
bool esp32s31_rgb565_draw_fps_gba(void *output, size_t output_pitch,
                                 unsigned fps_x10);

#endif
