#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "korvo1_lcd.h"
#include "korvo1_touch.h"
#include "rgb565_scale3x.h"

#define TEST_FRAME_PIXELS (ESP32S31_GBA_WIDTH * ESP32S31_GBA_HEIGHT)
#define STATUS_PERIOD_US 5000000

static const char *TAG = "s31_board_smoke";

static uint16_t test_pattern_color(unsigned x)
{
  static const uint16_t colors[] = {
      0xf800u, 0x07e0u, 0x001fu, 0xffffu,
      0xffe0u, 0x07ffu, 0xf81fu, 0x0000u,
  };
  return colors[(x * 8u) / ESP32S31_GBA_WIDTH];
}

static void make_test_frame(uint16_t *frame, unsigned moving_x)
{
  for (unsigned y = 0; y < ESP32S31_GBA_HEIGHT; y++)
  {
    for (unsigned x = 0; x < ESP32S31_GBA_WIDTH; x++)
      frame[y * ESP32S31_GBA_WIDTH + x] = test_pattern_color(x);

    frame[y * ESP32S31_GBA_WIDTH] = 0xffffu;
    frame[y * ESP32S31_GBA_WIDTH + ESP32S31_GBA_WIDTH - 1u] = 0xffffu;
    frame[y * ESP32S31_GBA_WIDTH + moving_x] = 0xffffu;
  }

  for (unsigned x = 0; x < ESP32S31_GBA_WIDTH; x++)
  {
    frame[x] = 0xffffu;
    frame[(ESP32S31_GBA_HEIGHT - 1u) * ESP32S31_GBA_WIDTH + x] = 0xffffu;
  }
}

static void print_stats(void)
{
  esp32s31_lcd_stats_t lcd = {0};
  esp32s31_touch_stats_t touch = {0};
  esp32s31_korvo1_lcd_get_stats(&lcd);
  esp32s31_korvo1_touch_get_stats(&touch);
  printf("result=PASS command=board_stats lcd_ready=%u touch_ready=%u "
         "lcd_submitted=%" PRIu32 " lcd_completed=%" PRIu32
         " lcd_dropped=%" PRIu32 " lcd_timeouts=%" PRIu32
         " lcd_vsync=%" PRIu32 " scale_us=%" PRIu32
         " scale_max_us=%" PRIu32 " wait_us=%" PRIu32
         " wait_max_us=%" PRIu32 " touch_polls=%" PRIu32
         " touch_reports=%" PRIu32 " touch_points=%" PRIu32
         " touch_i2c_errors=%" PRIu32 " touch_crc_errors=%" PRIu32
         "\n",
         (unsigned)esp32s31_korvo1_lcd_ready(),
         (unsigned)esp32s31_korvo1_touch_ready(),
         lcd.submitted_frames, lcd.completed_frames, lcd.dropped_frames,
         lcd.wait_timeouts, lcd.vsync_count, lcd.last_scale_us,
         lcd.max_scale_us, lcd.last_wait_us, lcd.max_wait_us,
         touch.polls, touch.reports, touch.points, touch.i2c_errors,
         touch.checksum_errors);
}

void app_main(void)
{
  ESP_LOGI(TAG, "ESP32-S31-Korvo-1 dependency-free board smoke start");

  const bool lcd_ready = esp32s31_korvo1_lcd_init();
  printf("result=%s command=lcd_init ready=%u\n",
         lcd_ready ? "PASS" : "FAIL", lcd_ready);

  const bool touch_ready = esp32s31_korvo1_touch_init();
  printf("result=%s command=touch_init ready=%u\n",
         touch_ready ? "PASS" : "FAIL", touch_ready);

  uint16_t *test_frame = NULL;
  if (lcd_ready)
  {
    test_frame = heap_caps_malloc(TEST_FRAME_PIXELS * sizeof(*test_frame),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_frame == NULL)
      ESP_LOGE(TAG, "failed to allocate PSRAM test source frame");
  }

  unsigned moving_x = 0;
  int64_t next_status = esp_timer_get_time() + STATUS_PERIOD_US;
  uint32_t touch_error_logs = 0;

  for (;;)
  {
    if (test_frame != NULL && esp32s31_korvo1_lcd_ready())
    {
      make_test_frame(test_frame, moving_x);
      (void)esp32s31_korvo1_lcd_present_rgb565(
          test_frame, ESP32S31_GBA_WIDTH, ESP32S31_GBA_HEIGHT,
          ESP32S31_GBA_WIDTH * sizeof(uint16_t));
      moving_x = (moving_x + 1u) % ESP32S31_GBA_WIDTH;
    }

    if (esp32s31_korvo1_touch_ready())
    {
      esp32s31_touch_point_t points[ESP32S31_GT1151_MAX_POINTS];
      size_t point_count = 0;
      const esp_err_t error = esp32s31_korvo1_touch_read(
          points, ESP32S31_GT1151_MAX_POINTS, &point_count);
      if (error == ESP_OK)
      {
        for (size_t i = 0; i < point_count; i++)
        {
          printf("result=PASS command=touch_read point=%u x=%u y=%u "
                 "strength=%u track_id=%u\n",
                 (unsigned)i, (unsigned)points[i].x,
                 (unsigned)points[i].y, (unsigned)points[i].strength,
                 (unsigned)points[i].track_id);
        }
      }
      else if (touch_error_logs < 3u)
      {
        ESP_LOGW(TAG, "touch poll failed: %s", esp_err_to_name(error));
        touch_error_logs++;
      }
    }

    const int64_t now = esp_timer_get_time();
    if (now >= next_status)
    {
      print_stats();
      next_status = now + STATUS_PERIOD_US;
    }

    if (test_frame == NULL || !esp32s31_korvo1_lcd_ready())
      vTaskDelay(pdMS_TO_TICKS(10));
    else
      taskYIELD();
  }
}
