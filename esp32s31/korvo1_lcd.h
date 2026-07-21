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
  uint32_t last_snapshot_copy_us;
  uint32_t max_snapshot_copy_us;
  uint32_t snapshot_copy_interrupts;
  uint32_t last_wait_us;
  uint32_t max_wait_us;
} esp32s31_lcd_stats_t;

/* Internal-RAM-only state used by the pre-clear PSRAM fault ISR.  Pointer
 * fields are captured as integer values; taking this snapshot must never
 * dereference the PSRAM scan source while the controller is faulting. */
typedef struct {
  uint32_t bounce_active;
  uint32_t bounce_sequence;
  uint32_t bounce_position_pixels;
  uint32_t bounce_length_bytes;
  uint32_t bounce_source_start;
  uint32_t bounce_source_end;
  uint32_t bounce_begin_cycle;
  uint32_t bounce_end_cycle;
  uint32_t snapshot_start;
  uint32_t snapshot_end;
  uint32_t render_start;
  uint32_t snapshot_copying;
} esp32s31_lcd_fault_snapshot_t;

bool esp32s31_korvo1_lcd_init(void);
bool esp32s31_korvo1_lcd_ready(void);
bool esp32s31_korvo1_lcd_present_rgb565(const void *pixels,
                                        unsigned width,
                                        unsigned height,
                                        size_t pitch);
/* gpSP's shared RGB565 render target, independent of LCD scanout mode. */
uint16_t *esp32s31_korvo1_lcd_render_buffer(void);
const char *esp32s31_korvo1_lcd_render_memory_name(void);
unsigned esp32s31_korvo1_lcd_bounce_source_rows(void);
void esp32s31_korvo1_lcd_set_fps_x10(unsigned fps_x10);
void esp32s31_korvo1_lcd_get_stats(esp32s31_lcd_stats_t *out);
void esp32s31_korvo1_lcd_get_fault_snapshot(
    esp32s31_lcd_fault_snapshot_t *out);
const char *esp32s31_korvo1_lcd_scaler_name(void);

#endif
