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
  uint32_t last_scale_prepare_us;
  uint32_t last_scale_transfer_us;
  uint32_t bounce_callbacks;
  uint32_t bounce_discontinuities;
  uint32_t bounce_fill_max_us;
  uint32_t last_wait_us;
  uint32_t max_wait_us;
} esp32s31_lcd_stats_t;

bool esp32s31_korvo1_lcd_init(void);
bool esp32s31_korvo1_lcd_ready(void);
bool esp32s31_korvo1_lcd_present_rgb565(const void *pixels,
                                        unsigned width,
                                        unsigned height,
                                        size_t pitch);
/* Non-NULL only in no-framebuffer mode; gpSP renders its next frame here. */
uint16_t *esp32s31_korvo1_lcd_render_buffer(void);
void esp32s31_korvo1_lcd_set_fps_x10(unsigned fps_x10);
void esp32s31_korvo1_lcd_get_stats(esp32s31_lcd_stats_t *out);
const char *esp32s31_korvo1_lcd_scaler_name(void);

#endif
