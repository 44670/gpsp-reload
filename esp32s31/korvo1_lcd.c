#include "korvo1_lcd.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "korvo1_pins.h"
#include "rgb565_scale3x.h"
#include "sdkconfig.h"

#ifndef ESP32S31_KORVO1_LCD_COMPAT_18MHZ
#define ESP32S31_KORVO1_LCD_COMPAT_18MHZ 0
#endif

#ifndef ESP32S31_KORVO1_LCD_USE_GPIO38_DISP
#define ESP32S31_KORVO1_LCD_USE_GPIO38_DISP 0
#endif

#define LCD_FRAME_WAIT_MS 50u
#define LCD_MAX_CONSECUTIVE_TIMEOUTS 3u
#define LCD_FRAME_BYTES \
  (ESP32S31_LCD_WIDTH * ESP32S31_LCD_HEIGHT * sizeof(uint16_t))

static const char *TAG = "korvo1_lcd";

typedef struct {
  esp_lcd_panel_handle_t panel;
  uint16_t *framebuffers[2];
  TaskHandle_t owner_task;
  volatile uint32_t vsync_count;
  volatile uint32_t completed_frames;
  esp32s31_lcd_stats_t stats;
  uint32_t pending_completion;
  uint8_t front_index;
  uint8_t back_index;
  uint8_t consecutive_timeouts;
  uint16_t fps_x10;
  bool fps_valid;
  bool pending;
  bool ready;
} korvo1_lcd_state_t;

/* Callback context must remain in internal RAM. */
static DRAM_ATTR korvo1_lcd_state_t s_lcd;

static bool IRAM_ATTR lcd_notify_owner_from_isr(korvo1_lcd_state_t *state)
{
  BaseType_t higher_priority_task_woken = pdFALSE;
  if (state->owner_task != NULL)
    vTaskNotifyGiveFromISR(state->owner_task, &higher_priority_task_woken);
  return higher_priority_task_woken == pdTRUE;
}

static bool IRAM_ATTR lcd_on_vsync(
    esp_lcd_panel_handle_t panel,
    const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
  (void)panel;
  (void)event_data;
  korvo1_lcd_state_t *state = (korvo1_lcd_state_t *)user_ctx;
  state->vsync_count++;
  return lcd_notify_owner_from_isr(state);
}

static bool IRAM_ATTR lcd_on_frame_complete(
    esp_lcd_panel_handle_t panel,
    const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
  (void)panel;
  (void)event_data;
  korvo1_lcd_state_t *state = (korvo1_lcd_state_t *)user_ctx;
  state->completed_frames++;
  return lcd_notify_owner_from_isr(state);
}

static bool wait_for_counter_change(volatile uint32_t *counter,
                                    uint32_t initial_value,
                                    TickType_t timeout_ticks)
{
  const TickType_t start = xTaskGetTickCount();
  TickType_t remaining = timeout_ticks;

  while (*counter == initial_value)
  {
    if (ulTaskNotifyTake(pdTRUE, remaining) == 0u)
      return false;

    const TickType_t elapsed = xTaskGetTickCount() - start;
    if (elapsed >= timeout_ticks)
      return *counter != initial_value;
    remaining = timeout_ticks - elapsed;
  }
  return true;
}

static void fill_startup_pattern(uint16_t *framebuffer)
{
  static const uint16_t colors[] = {
      0xf800u, 0x07e0u, 0x001fu, 0xffffu,
      0xffe0u, 0x07ffu, 0xf81fu, 0x0000u,
  };

  for (unsigned y = 0; y < ESP32S31_LCD_HEIGHT; y++)
  {
    uint16_t *row = framebuffer + y * ESP32S31_LCD_WIDTH;
    memset(row, 0, ESP32S31_LCD_WIDTH * sizeof(*row));
    for (unsigned x = ESP32S31_LCD_BAR_WIDTH;
         x < ESP32S31_LCD_WIDTH - ESP32S31_LCD_BAR_WIDTH; x++)
      row[x] = colors[((x - ESP32S31_LCD_BAR_WIDTH) * 8u) /
                      (ESP32S31_LCD_WIDTH - ESP32S31_LCD_BAR_WIDTH * 2u)];

    row[ESP32S31_LCD_BAR_WIDTH] = 0xffffu;
    row[ESP32S31_LCD_WIDTH - ESP32S31_LCD_BAR_WIDTH - 1u] = 0xffffu;
  }

  for (unsigned x = ESP32S31_LCD_BAR_WIDTH;
       x < ESP32S31_LCD_WIDTH - ESP32S31_LCD_BAR_WIDTH; x++)
  {
    framebuffer[x] = 0xffffu;
    framebuffer[(ESP32S31_LCD_HEIGHT - 1u) * ESP32S31_LCD_WIDTH + x] =
        0xffffu;
  }
}

static esp_lcd_rgb_panel_config_t lcd_panel_config(void)
{
  esp_lcd_rgb_panel_config_t config = {
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .data_width = 16,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = 2,
      .bounce_buffer_size_px = 0,
      .dma_burst_size = 128,
      .hsync_gpio_num = KORVO1_LCD_HSYNC,
      .vsync_gpio_num = KORVO1_LCD_VSYNC,
      .de_gpio_num = KORVO1_LCD_DE,
      .pclk_gpio_num = KORVO1_LCD_PCLK,
#if ESP32S31_KORVO1_LCD_USE_GPIO38_DISP
      .disp_gpio_num = KORVO1_LCD_EXPERIMENTAL_DISP,
#else
      .disp_gpio_num = GPIO_NUM_NC,
#endif
      .data_gpio_nums = {
          KORVO1_LCD_DATA0, KORVO1_LCD_DATA1,
          KORVO1_LCD_DATA2, KORVO1_LCD_DATA3,
          KORVO1_LCD_DATA4, KORVO1_LCD_DATA5,
          KORVO1_LCD_DATA6, KORVO1_LCD_DATA7,
          KORVO1_LCD_DATA8, KORVO1_LCD_DATA9,
          KORVO1_LCD_DATA10, KORVO1_LCD_DATA11,
          KORVO1_LCD_DATA12, KORVO1_LCD_DATA13,
          KORVO1_LCD_DATA14, KORVO1_LCD_DATA15,
      },
      .flags = {
          .fb_in_psram = true,
      },
  };

#if ESP32S31_KORVO1_LCD_COMPAT_18MHZ
  config.timings = (esp_lcd_rgb_timing_t){
      .pclk_hz = 18000000,
      .h_res = ESP32S31_LCD_WIDTH,
      .v_res = ESP32S31_LCD_HEIGHT,
      .hsync_pulse_width = 40,
      .hsync_back_porch = 40,
      .hsync_front_porch = 48,
      .vsync_pulse_width = 23,
      .vsync_back_porch = 32,
      .vsync_front_porch = 13,
      .flags.pclk_active_neg = true,
  };
#else
  config.timings = (esp_lcd_rgb_timing_t){
      .pclk_hz = 26000000,
      .h_res = ESP32S31_LCD_WIDTH,
      .v_res = ESP32S31_LCD_HEIGHT,
      .hsync_pulse_width = 1,
      .hsync_back_porch = 40,
      .hsync_front_porch = 20,
      .vsync_pulse_width = 1,
      .vsync_back_porch = 10,
      .vsync_front_porch = 5,
      .flags.pclk_active_neg = true,
  };
#endif
  return config;
}

static bool lcd_fail(const char *operation, esp_err_t error)
{
  ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
  s_lcd.ready = false;
  s_lcd.owner_task = NULL;
  if (s_lcd.panel != NULL)
  {
    esp_lcd_panel_del(s_lcd.panel);
    s_lcd.panel = NULL;
  }
  ESP_LOGW(TAG, "LCD unavailable; continuing with UART/headless operation");
  return false;
}

bool esp32s31_korvo1_lcd_init(void)
{
  if (s_lcd.ready)
    return true;

#ifndef CONFIG_IDF_TARGET_ESP32S31
  ESP_LOGE(TAG, "driver requires CONFIG_IDF_TARGET_ESP32S31");
  return false;
#else
  memset(&s_lcd, 0, sizeof(s_lcd));
  s_lcd.owner_task = xTaskGetCurrentTaskHandle();

  if (!esp_psram_is_initialized())
  {
    ESP_LOGE(TAG, "PSRAM is not initialized");
    ESP_LOGW(TAG, "LCD unavailable; continuing with UART/headless operation");
    return false;
  }

  ESP_LOGI(TAG,
           "PSRAM size=%u free=%u largest=%u; framebuffers=%u bytes",
           (unsigned)esp_psram_get_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
           (unsigned)(LCD_FRAME_BYTES * 2u));

  esp_lcd_rgb_panel_config_t panel_config = lcd_panel_config();
  ESP_LOGI(TAG,
           "RGB panel 800x480 RGB565 pclk=%" PRIu32
           " profile=%s gpio38_disp=%d",
           panel_config.timings.pclk_hz,
#if ESP32S31_KORVO1_LCD_COMPAT_18MHZ
           "compat-18mhz",
#else
           "factory-26mhz",
#endif
           ESP32S31_KORVO1_LCD_USE_GPIO38_DISP);

  esp_err_t error = esp_lcd_new_rgb_panel(&panel_config, &s_lcd.panel);
  if (error != ESP_OK)
    return lcd_fail("create RGB panel", error);

  error = esp_lcd_rgb_panel_get_frame_buffer(
      s_lcd.panel, 2, (void **)&s_lcd.framebuffers[0],
      (void **)&s_lcd.framebuffers[1]);
  if (error != ESP_OK)
    return lcd_fail("get RGB framebuffers", error);

  /* Initialize both buffers before DMA starts, so no uninitialized scanout. */
  fill_startup_pattern(s_lcd.framebuffers[0]);
  memcpy(s_lcd.framebuffers[1], s_lcd.framebuffers[0], LCD_FRAME_BYTES);
  for (size_t i = 0; i < 2u; i++)
  {
    error = esp_cache_msync(
        s_lcd.framebuffers[i], LCD_FRAME_BYTES,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (error != ESP_OK)
      return lcd_fail("sync initial RGB framebuffer", error);
  }

  const esp_lcd_rgb_panel_event_callbacks_t callbacks = {
      .on_vsync = lcd_on_vsync,
      .on_frame_buf_complete = lcd_on_frame_complete,
  };
  error = esp_lcd_rgb_panel_register_event_callbacks(
      s_lcd.panel, &callbacks, &s_lcd);
  if (error != ESP_OK)
    return lcd_fail("register RGB callbacks", error);

  error = esp_lcd_panel_reset(s_lcd.panel);
  if (error != ESP_OK)
    return lcd_fail("reset RGB panel", error);

  const uint32_t initial_vsync = s_lcd.vsync_count;
  error = esp_lcd_panel_init(s_lcd.panel);
  if (error != ESP_OK)
    return lcd_fail("initialize RGB panel", error);

  error = esp_lcd_panel_disp_on_off(s_lcd.panel, true);
  if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED)
    return lcd_fail("enable RGB panel", error);

  if (!wait_for_counter_change(&s_lcd.vsync_count, initial_vsync,
                               pdMS_TO_TICKS(LCD_FRAME_WAIT_MS)))
    return lcd_fail("wait for initial VSYNC", ESP_ERR_TIMEOUT);

  s_lcd.front_index = 0;
  s_lcd.back_index = 1;
  s_lcd.ready = true;
  ESP_LOGI(TAG, "Korvo-1 RGB LCD ready; VSYNC and DMA are active");
  return true;
#endif
}

bool esp32s31_korvo1_lcd_ready(void)
{
  return s_lcd.ready;
}

static bool finish_pending_frame(void)
{
  if (!s_lcd.pending)
    return true;

  const int64_t wait_start = esp_timer_get_time();
  const bool completed = wait_for_counter_change(
      &s_lcd.completed_frames, s_lcd.pending_completion,
      pdMS_TO_TICKS(LCD_FRAME_WAIT_MS));
  const uint32_t wait_us = (uint32_t)(esp_timer_get_time() - wait_start);
  s_lcd.stats.last_wait_us = wait_us;
  if (wait_us > s_lcd.stats.max_wait_us)
    s_lcd.stats.max_wait_us = wait_us;

  if (!completed)
  {
    s_lcd.stats.wait_timeouts++;
    s_lcd.consecutive_timeouts++;
    if (s_lcd.consecutive_timeouts >= LCD_MAX_CONSECUTIVE_TIMEOUTS)
    {
      s_lcd.ready = false;
      ESP_LOGE(TAG, "LCD disabled after %u consecutive frame timeouts",
               (unsigned)s_lcd.consecutive_timeouts);
    }
    return false;
  }

  const uint8_t old_front = s_lcd.front_index;
  s_lcd.front_index = s_lcd.back_index;
  s_lcd.back_index = old_front;
  s_lcd.pending = false;
  s_lcd.consecutive_timeouts = 0;
  return true;
}

bool esp32s31_korvo1_lcd_present_rgb565(const void *pixels,
                                        unsigned width,
                                        unsigned height,
                                        size_t pitch)
{
  if (!s_lcd.ready || pixels == NULL || width != ESP32S31_GBA_WIDTH ||
      height != ESP32S31_GBA_HEIGHT ||
      pitch < ESP32S31_GBA_WIDTH * sizeof(uint16_t))
  {
    s_lcd.stats.dropped_frames++;
    return false;
  }

  if (!finish_pending_frame())
  {
    s_lcd.stats.dropped_frames++;
    return false;
  }

  uint16_t *back = s_lcd.framebuffers[s_lcd.back_index];
  const int64_t scale_start = esp_timer_get_time();
  const bool scaled = esp32s31_rgb565_scale3x(
      back, ESP32S31_LCD_WIDTH * sizeof(uint16_t), pixels, width, height,
      pitch);
  const uint32_t scale_us =
      (uint32_t)(esp_timer_get_time() - scale_start);
  s_lcd.stats.last_scale_us = scale_us;
  if (scale_us > s_lcd.stats.max_scale_us)
    s_lcd.stats.max_scale_us = scale_us;
  if (!scaled)
  {
    s_lcd.stats.dropped_frames++;
    return false;
  }

  if (s_lcd.fps_valid)
    (void)esp32s31_rgb565_draw_fps(
        back, ESP32S31_LCD_WIDTH * sizeof(uint16_t), s_lcd.fps_x10);

  /* The old completion must not satisfy the wait for this submission. */
  (void)ulTaskNotifyTake(pdTRUE, 0);
  s_lcd.pending_completion = s_lcd.completed_frames;
  const esp_err_t error = esp_lcd_panel_draw_bitmap(
      s_lcd.panel, 0, 0, ESP32S31_LCD_WIDTH, ESP32S31_LCD_HEIGHT, back);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "submit RGB framebuffer failed: %s",
             esp_err_to_name(error));
    s_lcd.stats.dropped_frames++;
    return false;
  }

  s_lcd.pending = true;
  s_lcd.stats.submitted_frames++;
  return true;
}

void esp32s31_korvo1_lcd_set_fps_x10(unsigned fps_x10)
{
  if (fps_x10 > 999u)
    fps_x10 = 999u;
  s_lcd.fps_x10 = (uint16_t)fps_x10;
  s_lcd.fps_valid = true;
}

void esp32s31_korvo1_lcd_get_stats(esp32s31_lcd_stats_t *out)
{
  if (out == NULL)
    return;
  *out = s_lcd.stats;
  out->completed_frames = s_lcd.completed_frames;
  out->vsync_count = s_lcd.vsync_count;
}
