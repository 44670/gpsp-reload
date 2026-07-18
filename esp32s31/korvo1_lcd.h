#ifndef GPSP_ESP32S31_KORVO1_LCD_H
#define GPSP_ESP32S31_KORVO1_LCD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t submitted_frames;
  uint32_t completed_frames;
  uint32_t dropped_frames;
  uint32_t wait_timeouts;
  uint32_t vsync_count;
  uint32_t last_scale_us;
  uint32_t max_scale_us;
  uint32_t last_wait_us;
  uint32_t max_wait_us;
} esp32s31_lcd_stats_t;

bool esp32s31_korvo1_lcd_init(void);
bool esp32s31_korvo1_lcd_ready(void);
bool esp32s31_korvo1_lcd_present_rgb565(const void *pixels,
                                        unsigned width,
                                        unsigned height,
                                        size_t pitch);
void esp32s31_korvo1_lcd_get_stats(esp32s31_lcd_stats_t *out);

#endif
