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
#include "gpsp_profile.h"
#include "korvo1_pins.h"
#include "korvo1_scaler.h"
#include "rgb565_scale3x.h"
#include "sdkconfig.h"

#ifndef ESP32S31_KORVO1_LCD_COMPAT_18MHZ
#define ESP32S31_KORVO1_LCD_COMPAT_18MHZ 0
#endif

#ifndef ESP32S31_KORVO1_LCD_USE_GPIO38_DISP
#define ESP32S31_KORVO1_LCD_USE_GPIO38_DISP 0
#endif

#ifndef ESP32S31_LCD_BOUNCE_MODE
#define ESP32S31_LCD_BOUNCE_MODE 0
#endif

#ifndef ESP32S31_LCD_BOUNCE_SOURCE_ROWS
#define ESP32S31_LCD_BOUNCE_SOURCE_ROWS 10u
#endif

#ifndef ESP32S31_LCD_RENDER_INTERNAL
#define ESP32S31_LCD_RENDER_INTERNAL 1
#endif

#define LCD_FRAME_WAIT_MS 50u
#define LCD_FRAME_BYTES \
  (ESP32S31_LCD_WIDTH * ESP32S31_LCD_HEIGHT * sizeof(uint16_t))
#define GBA_FRAME_STORAGE_BYTES \
  (ESP32S31_GBA_WIDTH * (ESP32S31_GBA_HEIGHT + 1u) * sizeof(uint16_t))
#define GBA_SNAPSHOT_BYTES \
  (ESP32S31_GBA_WIDTH * ESP32S31_GBA_HEIGHT * sizeof(uint16_t))
#define LCD_BOUNCE_SOURCE_ROWS ESP32S31_LCD_BOUNCE_SOURCE_ROWS
#define LCD_BOUNCE_OUTPUT_ROWS \
  (LCD_BOUNCE_SOURCE_ROWS * ESP32S31_SCALE_FACTOR)
#define LCD_BOUNCE_PIXELS \
  (ESP32S31_LCD_WIDTH * LCD_BOUNCE_OUTPUT_ROWS)
#define LCD_BOUNCE_BYTES (LCD_BOUNCE_PIXELS * sizeof(uint16_t))
#define LCD_FRAME_PIXELS \
  (ESP32S31_LCD_WIDTH * ESP32S31_LCD_HEIGHT)

_Static_assert(ESP32S31_GBA_HEIGHT % LCD_BOUNCE_SOURCE_ROWS == 0,
               "bounce source rows must divide the GBA height");
_Static_assert(ESP32S31_LCD_HEIGHT % LCD_BOUNCE_OUTPUT_ROWS == 0,
               "bounce output rows must divide the LCD height");

static const char *TAG = "korvo1_lcd";

#if ESP32S31_LCD_RENDER_INTERNAL
static uint16_t s_render_buffer_storage[
    GBA_FRAME_STORAGE_BYTES / sizeof(uint16_t)] __attribute__((aligned(128)));
#else
static EXT_RAM_BSS_ATTR uint16_t s_render_buffer_storage[
    GBA_FRAME_STORAGE_BYTES / sizeof(uint16_t)] __attribute__((aligned(128)));
#endif

#if ESP32S31_LCD_BOUNCE_MODE
/* One compact, native-resolution frame is the stable PSRAM scan source. */
static EXT_RAM_BSS_ATTR uint16_t s_snapshot_buffer_storage[
    GBA_SNAPSHOT_BYTES / sizeof(uint16_t)] __attribute__((aligned(128)));
#endif

typedef struct {
  esp_lcd_panel_handle_t panel;
  uint16_t *framebuffers[1];
  uint16_t *render_buffer;
#if ESP32S31_LCD_BOUNCE_MODE
  uint16_t *snapshot_buffer;
  volatile bool snapshot_copying;
  volatile uint32_t snapshot_copy_interrupts;
#endif
  TaskHandle_t owner_task;
  volatile uint32_t vsync_count;
  volatile uint32_t completed_frames;
  volatile uint32_t bounce_callbacks;
  volatile uint32_t bounce_discontinuities;
  volatile uint32_t bounce_fill_frame_us;
  volatile uint32_t bounce_fill_last_us;
  volatile uint32_t bounce_fill_max_us;
  volatile uint32_t bounce_expected_pos_px;
  volatile uint32_t bounce_active;
  volatile uint32_t bounce_sequence;
  volatile uint32_t bounce_position_pixels;
  volatile uint32_t bounce_length_bytes;
  volatile uint32_t bounce_source_start;
  volatile uint32_t bounce_source_end;
  volatile uint32_t bounce_begin_cycle;
  volatile uint32_t bounce_end_cycle;
  esp32s31_lcd_stats_t stats;
  uint16_t fps_x10;
  esp32s31_rgb565_fps_osd_t fps_osd[2];
  volatile uint8_t fps_osd_index;
  volatile bool fps_valid;
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
  return state->ready ? false : lcd_notify_owner_from_isr(state);
}

static bool IRAM_ATTR lcd_on_frame_complete(
    esp_lcd_panel_handle_t panel,
    const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
  (void)panel;
  (void)event_data;
  korvo1_lcd_state_t *state = (korvo1_lcd_state_t *)user_ctx;
  state->completed_frames++;
  return false;
}

#if ESP32S31_LCD_BOUNCE_MODE
static bool IRAM_ATTR lcd_on_bounce_empty(
    esp_lcd_panel_handle_t panel, void *bounce_buffer, int position_pixels,
    int length_bytes, void *user_ctx)
{
  (void)panel;
  korvo1_lcd_state_t *state = (korvo1_lcd_state_t *)user_ctx;
  uint32_t begin_cycle;
  __asm__ __volatile__("rdcycle %0" : "=r"(begin_cycle));
  state->bounce_sequence++;
  state->bounce_position_pixels =
      position_pixels >= 0 ? (uint32_t)position_pixels : UINT32_MAX;
  state->bounce_length_bytes =
      length_bytes >= 0 ? (uint32_t)length_bytes : UINT32_MAX;
  state->bounce_source_start = 0u;
  state->bounce_source_end = 0u;
  state->bounce_begin_cycle = begin_cycle;
  __atomic_store_n(&state->bounce_active, 1u, __ATOMIC_RELEASE);
  const int64_t fill_start = esp_timer_get_time();

  const uint32_t position = position_pixels >= 0 ?
      (uint32_t)position_pixels : UINT32_MAX;
  const bool valid_chunk = bounce_buffer != NULL &&
      length_bytes == (int)LCD_BOUNCE_BYTES &&
      position < LCD_FRAME_PIXELS &&
      position % LCD_BOUNCE_PIXELS == 0u;
  if (position != state->bounce_expected_pos_px)
    state->bounce_discontinuities++;

  if (valid_chunk)
  {
    const unsigned output_y = position / ESP32S31_LCD_WIDTH;
    const unsigned source_y = output_y / ESP32S31_SCALE_FACTOR;
    /*
     * A single snapshot cannot be swapped atomically.  While its CPU copy is
     * in progress, render_buffer is nevertheless a complete and immutable
     * frame (present() has not returned to the emulator), so use that SRAM
     * frame instead of ever sampling a half-written PSRAM snapshot.
     */
    const bool snapshot_copying =
        __atomic_load_n(&state->snapshot_copying, __ATOMIC_ACQUIRE);
    const uint16_t *frame_source = snapshot_copying ?
        state->render_buffer : state->snapshot_buffer;
    if (snapshot_copying)
      state->snapshot_copy_interrupts++;
    const uint16_t *source = frame_source +
        (size_t)source_y * ESP32S31_GBA_WIDTH;
    state->bounce_source_start = (uint32_t)(uintptr_t)source;
    state->bounce_source_end = (uint32_t)(uintptr_t)(
        source + (size_t)LCD_BOUNCE_SOURCE_ROWS * ESP32S31_GBA_WIDTH);
    const esp32s31_rgb565_fps_osd_t *osd = NULL;
    if (__atomic_load_n(&state->fps_valid, __ATOMIC_ACQUIRE))
    {
      const uint8_t osd_index =
          __atomic_load_n(&state->fps_osd_index, __ATOMIC_ACQUIRE) & 1u;
      osd = &state->fps_osd[osd_index];
    }
    if (!esp32s31_rgb565_scale3x_rows_osd(
            bounce_buffer, ESP32S31_LCD_WIDTH * sizeof(uint16_t), source,
            source_y, LCD_BOUNCE_SOURCE_ROWS,
            ESP32S31_GBA_WIDTH * sizeof(uint16_t), osd))
      memset(bounce_buffer, 0, (size_t)length_bytes);
    state->bounce_expected_pos_px =
        (position + LCD_BOUNCE_PIXELS) % LCD_FRAME_PIXELS;
  }
  else if (bounce_buffer != NULL && length_bytes > 0)
  {
    memset(bounce_buffer, 0, (size_t)length_bytes);
    state->bounce_discontinuities++;
    state->bounce_expected_pos_px = 0;
  }

  const uint32_t fill_us =
      (uint32_t)(esp_timer_get_time() - fill_start);
  state->bounce_callbacks++;
  if (fill_us > state->bounce_fill_max_us)
    state->bounce_fill_max_us = fill_us;
  const uint32_t frame_fill_us = state->bounce_fill_frame_us + fill_us;
  if (valid_chunk && position + LCD_BOUNCE_PIXELS == LCD_FRAME_PIXELS)
  {
    state->bounce_fill_last_us = frame_fill_us;
    state->bounce_fill_frame_us = 0;
  }
  else
  {
    state->bounce_fill_frame_us = frame_fill_us;
  }

  uint32_t end_cycle;
  __asm__ __volatile__("rdcycle %0" : "=r"(end_cycle));
  state->bounce_end_cycle = end_cycle;
  __atomic_store_n(&state->bounce_active, 0u, __ATOMIC_RELEASE);

  return false;
}
#endif

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

#if !ESP32S31_LCD_BOUNCE_MODE
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
#endif

static esp_lcd_rgb_panel_config_t lcd_panel_config(void)
{
  esp_lcd_rgb_panel_config_t config = {
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .data_width = 16,
      .in_color_format = LCD_COLOR_FMT_RGB565,
#if ESP32S31_LCD_BOUNCE_MODE
      .num_fbs = 0,
      .bounce_buffer_size_px = LCD_BOUNCE_PIXELS,
#else
      .num_fbs = 1,
      .bounce_buffer_size_px = 0,
#endif
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
#if ESP32S31_LCD_BOUNCE_MODE
          .no_fb = true,
#else
          .fb_in_psram = true,
#endif
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

static void free_render_buffer(void)
{
  s_lcd.render_buffer = NULL;
#if ESP32S31_LCD_BOUNCE_MODE
  s_lcd.snapshot_buffer = NULL;
#endif
}

static bool allocate_render_buffer(void)
{
  s_lcd.render_buffer = s_render_buffer_storage;
  memset(s_lcd.render_buffer, 0, GBA_FRAME_STORAGE_BYTES);
#if ESP32S31_LCD_BOUNCE_MODE
  s_lcd.snapshot_buffer = s_snapshot_buffer_storage;
  memset(s_lcd.snapshot_buffer, 0, GBA_SNAPSHOT_BYTES);
#endif
  return true;
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
  free_render_buffer();
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

#if ESP32S31_LCD_BOUNCE_MODE
  ESP_LOGI(TAG,
           "internal SRAM before LCD: free=%u largest=%u "
           "dma_free=%u dma_largest=%u; "
           "render=%s/%u bytes snapshot=psram/%u bytes "
           "bounce=2x%u bytes",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                             MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                      MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                             MALLOC_CAP_DMA |
                                             MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                      MALLOC_CAP_DMA |
                                                      MALLOC_CAP_8BIT),
           ESP32S31_LCD_RENDER_INTERNAL ? "sram" : "psram",
           (unsigned)GBA_FRAME_STORAGE_BYTES,
           (unsigned)GBA_SNAPSHOT_BYTES, (unsigned)LCD_BOUNCE_BYTES);
#else
  ESP_LOGI(TAG,
           "PSRAM size=%u free=%u largest=%u; scanout=%s storage=%u bytes",
           (unsigned)esp_psram_get_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
           "framebuffer", (unsigned)LCD_FRAME_BYTES);
#endif

  if (!allocate_render_buffer())
    return lcd_fail("initialize render buffer", ESP_ERR_NO_MEM);
#if ESP32S31_LCD_BOUNCE_MODE
  ESP_LOGI(TAG,
           "snapshot video bounce: render=%s/%u bytes "
           "snapshot=psram/%u bytes source_rows=%u bounce=2x%u bytes; "
           "single-snapshot tearing allowed",
           ESP32S31_LCD_RENDER_INTERNAL ? "sram" : "psram",
           (unsigned)GBA_FRAME_STORAGE_BYTES,
           (unsigned)GBA_SNAPSHOT_BYTES,
           (unsigned)LCD_BOUNCE_SOURCE_ROWS, (unsigned)LCD_BOUNCE_BYTES);
#else
  ESP_LOGI(TAG,
           "direct framebuffer: render=%s/%u bytes scanout=psram/%u bytes "
           "buffers=1 bounce=0; tearing allowed",
           ESP32S31_LCD_RENDER_INTERNAL ? "sram" : "psram",
           (unsigned)GBA_FRAME_STORAGE_BYTES, (unsigned)LCD_FRAME_BYTES);
#endif

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

#if !ESP32S31_LCD_BOUNCE_MODE
  error = esp_lcd_rgb_panel_get_frame_buffer(
      s_lcd.panel, 1, (void **)&s_lcd.framebuffers[0]);
  if (error != ESP_OK)
    return lcd_fail("get RGB framebuffer", error);

  if (!esp32s31_korvo1_scaler_init())
    ESP_LOGW(TAG, "requested scaler unavailable; using CPU fallback");

  /* Initialize the sole scanout buffer before DMA starts. */
  fill_startup_pattern(s_lcd.framebuffers[0]);
  error = esp_cache_msync(
      s_lcd.framebuffers[0], LCD_FRAME_BYTES,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (error != ESP_OK)
    return lcd_fail("sync initial RGB framebuffer", error);
#endif

  const esp_lcd_rgb_panel_event_callbacks_t callbacks = {
      .on_vsync = lcd_on_vsync,
#if ESP32S31_LCD_BOUNCE_MODE
      .on_bounce_empty = lcd_on_bounce_empty,
#endif
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

  s_lcd.ready = true;
  ESP_LOGI(TAG, "Korvo-1 RGB LCD ready; mode=%s VSYNC and DMA active",
#if ESP32S31_LCD_BOUNCE_MODE
           "bounce");
#else
           "framebuffer");
#endif
  return true;
#endif
}

bool esp32s31_korvo1_lcd_ready(void)
{
  return s_lcd.ready;
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

  const size_t source_row_bytes =
      ESP32S31_GBA_WIDTH * sizeof(uint16_t);
  if (pixels != s_lcd.render_buffer || pitch != source_row_bytes)
  {
    ESP_LOGE(TAG, "video frame is not the shared render buffer");
    s_lcd.stats.dropped_frames++;
    return false;
  }

#if ESP32S31_LCD_BOUNCE_MODE
  const uint32_t profile_snapshot = gpsp_profile_begin();
  const int64_t copy_start = esp_timer_get_time();

  /*
   * The bounce ISR switches to the stable SRAM render frame for the short
   * copy interval.  After the release store, all later strips use PSRAM.
   * Both producer and consumer are CPU accesses, so no DMA cache sync is
   * required for this snapshot.
   */
  __atomic_store_n(&s_lcd.snapshot_copying, true, __ATOMIC_RELEASE);
  memcpy(s_lcd.snapshot_buffer, pixels, GBA_SNAPSHOT_BYTES);
  __atomic_store_n(&s_lcd.snapshot_copying, false, __ATOMIC_RELEASE);

  const uint32_t copy_us =
      (uint32_t)(esp_timer_get_time() - copy_start);
  gpsp_profile_end(GPSP_PROFILE_LCD_SNAPSHOT, profile_snapshot);
  s_lcd.stats.last_scale_us = copy_us;
  s_lcd.stats.last_snapshot_copy_us = copy_us;
  if (copy_us > s_lcd.stats.max_scale_us)
    s_lcd.stats.max_scale_us = copy_us;
  if (copy_us > s_lcd.stats.max_snapshot_copy_us)
    s_lcd.stats.max_snapshot_copy_us = copy_us;
  s_lcd.stats.submitted_frames++;
  return true;
#else
  uint16_t *framebuffer = s_lcd.framebuffers[0];
  const uint32_t profile_scale = gpsp_profile_begin();
  const int64_t scale_start = esp_timer_get_time();
  const bool scaled = esp32s31_korvo1_scaler_scale(
      framebuffer, ESP32S31_LCD_WIDTH * sizeof(uint16_t), pixels, width,
      height, pitch);
  const uint32_t scale_us =
      (uint32_t)(esp_timer_get_time() - scale_start);
  gpsp_profile_end(GPSP_PROFILE_LCD_SCALE, profile_scale);
  s_lcd.stats.last_scale_us = scale_us;
  esp32s31_scaler_stats_t scaler_stats = {0};
  esp32s31_korvo1_scaler_get_stats(&scaler_stats);
  s_lcd.stats.last_scale_prepare_us = scaler_stats.prepare_us;
  s_lcd.stats.last_scale_transfer_us = scaler_stats.transfer_us;
  if (!scaler_stats.benchmarked_call &&
      scale_us > s_lcd.stats.max_scale_us)
    s_lcd.stats.max_scale_us = scale_us;
  if (!scaled)
  {
    s_lcd.stats.dropped_frames++;
    return false;
  }

  /* Overlay only after the scaled image is resident in PSRAM. */
  if (__atomic_load_n(&s_lcd.fps_valid, __ATOMIC_ACQUIRE))
  {
    const uint32_t profile_overlay = gpsp_profile_begin();
    (void)esp32s31_rgb565_draw_fps(
        framebuffer, ESP32S31_LCD_WIDTH * sizeof(uint16_t), s_lcd.fps_x10);
    gpsp_profile_end(GPSP_PROFILE_LCD_OVERLAY, profile_overlay);
  }

  /* LCD continuously scans this sole buffer; publish CPU writes to DMA. */
  const uint32_t profile_submit = gpsp_profile_begin();
  const esp_err_t error = esp_cache_msync(
      framebuffer, LCD_FRAME_BYTES,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  gpsp_profile_end(GPSP_PROFILE_LCD_SUBMIT, profile_submit);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "sync RGB framebuffer failed: %s",
             esp_err_to_name(error));
    s_lcd.stats.dropped_frames++;
    return false;
  }

  s_lcd.stats.submitted_frames++;
  return true;
#endif
}

uint16_t *esp32s31_korvo1_lcd_render_buffer(void)
{
  return s_lcd.render_buffer;
}

const char *esp32s31_korvo1_lcd_render_memory_name(void)
{
  return ESP32S31_LCD_RENDER_INTERNAL ? "sram" : "psram";
}

unsigned esp32s31_korvo1_lcd_bounce_source_rows(void)
{
  return LCD_BOUNCE_SOURCE_ROWS;
}

const char *esp32s31_korvo1_lcd_scaler_name(void)
{
#if ESP32S31_LCD_BOUNCE_MODE
  return "lcd_snapshot_bounce";
#else
  return esp32s31_korvo1_scaler_name();
#endif
}

void esp32s31_korvo1_lcd_set_fps_x10(unsigned fps_x10)
{
  if (fps_x10 > ESP32S31_FPS_DISPLAY_MAX_X10)
    fps_x10 = ESP32S31_FPS_DISPLAY_MAX_X10;

  const uint8_t current =
      __atomic_load_n(&s_lcd.fps_osd_index, __ATOMIC_RELAXED) & 1u;
  const uint8_t next = current ^ 1u;
  if (!esp32s31_rgb565_prepare_fps_osd(&s_lcd.fps_osd[next], fps_x10))
    return;
  s_lcd.fps_x10 = (uint16_t)fps_x10;
  __atomic_store_n(&s_lcd.fps_osd_index, next, __ATOMIC_RELEASE);
  __atomic_store_n(&s_lcd.fps_valid, true, __ATOMIC_RELEASE);
}

void esp32s31_korvo1_lcd_get_stats(esp32s31_lcd_stats_t *out)
{
  if (out == NULL)
    return;
  *out = s_lcd.stats;
#if ESP32S31_LCD_BOUNCE_MODE
  /* A direct shared buffer has no publish-completion boundary. */
  out->completed_frames = out->submitted_frames;
#else
  out->completed_frames = s_lcd.completed_frames;
#endif
  out->vsync_count = s_lcd.vsync_count;
#if ESP32S31_LCD_BOUNCE_MODE
  out->last_scale_prepare_us = s_lcd.bounce_fill_last_us;
  out->last_scale_transfer_us = 0;
  out->bounce_callbacks = s_lcd.bounce_callbacks;
  out->bounce_discontinuities = s_lcd.bounce_discontinuities;
  out->bounce_fill_max_us = s_lcd.bounce_fill_max_us;
  out->snapshot_copy_interrupts = s_lcd.snapshot_copy_interrupts;
#endif
}

void IRAM_ATTR esp32s31_korvo1_lcd_get_fault_snapshot(
    esp32s31_lcd_fault_snapshot_t *out)
{
  if (out == NULL)
    return;
  memset(out, 0, sizeof(*out));
#if ESP32S31_LCD_BOUNCE_MODE
  out->bounce_active =
      __atomic_load_n(&s_lcd.bounce_active, __ATOMIC_ACQUIRE);
  out->bounce_sequence = s_lcd.bounce_sequence;
  out->bounce_position_pixels = s_lcd.bounce_position_pixels;
  out->bounce_length_bytes = s_lcd.bounce_length_bytes;
  out->bounce_source_start = s_lcd.bounce_source_start;
  out->bounce_source_end = s_lcd.bounce_source_end;
  out->bounce_begin_cycle = s_lcd.bounce_begin_cycle;
  out->bounce_end_cycle = s_lcd.bounce_end_cycle;
  out->snapshot_start = (uint32_t)(uintptr_t)s_lcd.snapshot_buffer;
  out->snapshot_end = out->snapshot_start + GBA_SNAPSHOT_BYTES;
  out->render_start = (uint32_t)(uintptr_t)s_lcd.render_buffer;
  out->snapshot_copying =
      __atomic_load_n(&s_lcd.snapshot_copying, __ATOMIC_ACQUIRE);
#endif
}
