#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <libretro.h>

#include "korvo1_lcd.h"
#include "korvo1_touch.h"
#include "rgb565_scale3x.h"

#define GAMEPAK_PARTITION "gamepak"
#define GAMEPAK_PAGE_BYTES 0x8000u
#define FPS_WINDOW_US INT64_C(1000000)
#define STATUS_PERIOD_US INT64_C(5000000)

static const char *TAG = "gpsp-esp32s31";
static const char *g_base_dir = ".";

static const void *g_rom_data;
static size_t g_rom_size;
static size_t g_rom_partition_size;
static esp_partition_mmap_handle_t g_rom_mmap_handle;

static bool g_lcd_ready;
static uint32_t g_emulated_frames;
static uint32_t g_video_frames;
static uint32_t g_frame_hash = UINT32_C(2166136261);
static uint32_t g_fps_x10;
static uint32_t g_fps_window_frames;
static int64_t g_fps_window_start;
static uint16_t g_joypad_mask;
static unsigned g_touch_error_logs;
static unsigned g_video_error_logs;

static const char *lookup_variable(const char *key)
{
  if (strcmp(key, "gpsp_drc") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_bios") == 0)
    return "builtin";
  if (strcmp(key, "gpsp_boot_mode") == 0)
    return "game";
  if (strcmp(key, "gpsp_frameskip") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_frameskip_threshold") == 0)
    return "33";
  if (strcmp(key, "gpsp_frameskip_interval") == 0)
    return "0";
  if (strcmp(key, "gpsp_color_correction") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_frame_mixing") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_rtc") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_rumble") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_serial") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_sprlim") == 0)
    return "disabled";
  return NULL;
}

static void core_log_cb(enum retro_log_level level, const char *format, ...)
{
  static const char *levels[] = {"debug", "info", "warn", "error"};
  const char *name = "unknown";
  if ((unsigned)level < sizeof(levels) / sizeof(levels[0]))
    name = levels[level];

  printf("gpsp[%s] ", name);
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

static bool env_cb(unsigned command, void *data)
{
  switch (command)
  {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      if (data == NULL)
        return false;
      ((struct retro_log_callback *)data)->log = core_log_cb;
      return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return data != NULL &&
             *(const enum retro_pixel_format *)data ==
                 RETRO_PIXEL_FORMAT_RGB565;

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      if (data == NULL)
        return false;
      *(const char **)data = g_base_dir;
      return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      if (data == NULL)
        return false;
      *(bool *)data = false;
      return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      if (data == NULL)
        return false;
      struct retro_variable *variable = (struct retro_variable *)data;
      const char *value = lookup_variable(variable->key);
      if (value == NULL)
        return false;
      variable->value = value;
      return true;
    }

    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      return true;

    default:
      return false;
  }
}

static void update_fps(void)
{
  const int64_t now = esp_timer_get_time();
  if (g_fps_window_start == 0)
    g_fps_window_start = now;

  g_fps_window_frames++;
  const int64_t elapsed = now - g_fps_window_start;
  if (elapsed < FPS_WINDOW_US)
    return;

  const uint64_t scaled_frames =
      (uint64_t)g_fps_window_frames * UINT64_C(10000000);
  g_fps_x10 = (uint32_t)((scaled_frames + (uint64_t)elapsed / 2u) /
                         (uint64_t)elapsed);
  esp32s31_korvo1_lcd_set_fps_x10(g_fps_x10);
  g_fps_window_frames = 0;
  g_fps_window_start = now;
}

static void video_cb(const void *data, unsigned width, unsigned height,
                     size_t pitch)
{
  if (data == NULL)
    return;
  if (width != ESP32S31_GBA_WIDTH || height != ESP32S31_GBA_HEIGHT ||
      pitch < ESP32S31_GBA_WIDTH * sizeof(uint16_t))
  {
    if (g_video_error_logs < 3u)
    {
      ESP_LOGE(TAG, "invalid video frame %ux%u pitch=%zu", width, height,
               pitch);
      g_video_error_logs++;
    }
    return;
  }

  const uint8_t *pixels = (const uint8_t *)data;
  const size_t row_bytes = ESP32S31_GBA_WIDTH * sizeof(uint16_t);
  for (unsigned y = 0; y < ESP32S31_GBA_HEIGHT; y++)
  {
    const uint8_t *row = pixels + (size_t)y * pitch;
    for (size_t x = 0; x < row_bytes; x++)
    {
      g_frame_hash ^= row[x];
      g_frame_hash *= UINT32_C(16777619);
    }
  }

  g_video_frames++;
  update_fps();
  if (g_lcd_ready)
    (void)esp32s31_korvo1_lcd_present_rgb565(data, width, height, pitch);
}

static void audio_cb(int16_t left, int16_t right)
{
  (void)left;
  (void)right;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
  (void)data;
  return frames;
}

static void input_poll_cb(void)
{
  g_joypad_mask = 0;
  if (!esp32s31_korvo1_touch_ready())
    return;

  esp32s31_touch_point_t points[ESP32S31_GT1151_MAX_POINTS];
  size_t point_count = 0;
  const esp_err_t error = esp32s31_korvo1_touch_read(
      points, ESP32S31_GT1151_MAX_POINTS, &point_count);
  if (error != ESP_OK)
  {
    if (g_touch_error_logs < 3u)
    {
      ESP_LOGW(TAG, "touch poll failed: %s", esp_err_to_name(error));
      g_touch_error_logs++;
    }
    return;
  }

  for (size_t i = 0; i < point_count; i++)
  {
    printf("result=PASS command=touch_read point=%u x=%u y=%u "
           "strength=%u track_id=%u\n",
           (unsigned)i, (unsigned)points[i].x, (unsigned)points[i].y,
           (unsigned)points[i].strength, (unsigned)points[i].track_id);
  }
}

static int16_t input_state_cb(unsigned port, unsigned device,
                              unsigned index, unsigned id)
{
  if (port != 0u || device != RETRO_DEVICE_JOYPAD || index != 0u)
    return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    return (int16_t)g_joypad_mask;
  if (id >= 16u)
    return 0;
  return (g_joypad_mask & (uint16_t)(1u << id)) != 0u;
}

static size_t infer_raw_rom_size(const uint8_t *data, size_t capacity)
{
  size_t used = capacity;

  while (used >= sizeof(uint32_t))
  {
    uint32_t tail;
    memcpy(&tail, data + used - sizeof(tail), sizeof(tail));
    if (tail != UINT32_MAX)
      break;
    used -= sizeof(uint32_t);
  }
  while (used > 0u && data[used - 1u] == 0xffu)
    used--;
  if (used == 0u)
    return 0u;

  const size_t rounded =
      (used + GAMEPAK_PAGE_BYTES - 1u) & ~(GAMEPAK_PAGE_BYTES - 1u);
  return rounded < capacity ? rounded : capacity;
}

static bool log_cartridge_header(const uint8_t *rom, size_t size)
{
  if (size < 0xc0u)
  {
    ESP_LOGE(TAG, "raw gamepak image is too small: %zu bytes", size);
    return false;
  }

  char title[13];
  char code[5];
  for (unsigned i = 0; i < 12u; i++)
  {
    const uint8_t value = rom[0xa0u + i];
    title[i] = value >= 0x20u && value <= 0x7eu ? (char)value : '.';
  }
  title[12] = '\0';
  for (unsigned i = 0; i < 4u; i++)
  {
    const uint8_t value = rom[0xacu + i];
    code[i] = value >= 0x20u && value <= 0x7eu ? (char)value : '.';
  }
  code[4] = '\0';

  uint8_t checksum = 0u;
  for (unsigned i = 0xa0u; i < 0xbdu; i++)
    checksum = (uint8_t)(checksum - rom[i]);
  checksum = (uint8_t)(checksum - 0x19u);

  ESP_LOGI(TAG,
           "GBA cartridge title='%s' code='%s' header_checksum=%s "
           "rom_bytes=%zu",
           title, code, checksum == rom[0xbdu] ? "valid" : "invalid", size);
  return true;
}

static bool map_gamepak_partition(struct retro_game_info *info)
{
  if (info == NULL)
    return false;

  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
      GAMEPAK_PARTITION);
  if (partition == NULL)
  {
    ESP_LOGE(TAG, "ROM partition '%s' not found", GAMEPAK_PARTITION);
    return false;
  }

  esp_err_t error = esp_partition_mmap(
      partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &g_rom_data,
      &g_rom_mmap_handle);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "mmap ROM partition '%s' failed: %s", GAMEPAK_PARTITION,
             esp_err_to_name(error));
    return false;
  }

  g_rom_partition_size = partition->size;
  g_rom_size = infer_raw_rom_size((const uint8_t *)g_rom_data,
                                  g_rom_partition_size);
  if (g_rom_size == 0u ||
      !log_cartridge_header((const uint8_t *)g_rom_data, g_rom_size))
  {
    ESP_LOGE(TAG,
             "gamepak partition is blank or invalid; flash a raw .gba first");
    esp_partition_munmap(g_rom_mmap_handle);
    g_rom_data = NULL;
    g_rom_mmap_handle = 0;
    return false;
  }

  info->path = NULL;
  info->data = g_rom_data;
  info->size = g_rom_size;
  info->meta = NULL;

  ESP_LOGI(TAG,
           "mapped raw gamepak: flash=0x%08" PRIx32
           " partition_bytes=%zu inferred_rom_bytes=%zu",
           partition->address, g_rom_partition_size, g_rom_size);
  return true;
}

static void print_status(void)
{
  esp32s31_lcd_stats_t lcd = {0};
  esp32s31_touch_stats_t touch = {0};
  esp32s31_korvo1_lcd_get_stats(&lcd);
  esp32s31_korvo1_touch_get_stats(&touch);

  printf("result=PASS command=gpsp_status backend=interp "
         "emulated_frames=%" PRIu32 " video_frames=%" PRIu32
         " fps_x10=%" PRIu32 " fb_hash=0x%08" PRIx32
         " lcd_ready=%u lcd_submitted=%" PRIu32
         " lcd_completed=%" PRIu32 " lcd_dropped=%" PRIu32
         " lcd_timeouts=%" PRIu32 " lcd_vsync=%" PRIu32
         " scale_us=%" PRIu32 " scale_max_us=%" PRIu32
         " wait_us=%" PRIu32 " wait_max_us=%" PRIu32
         " touch_ready=%u touch_reports=%" PRIu32
         " touch_i2c_errors=%" PRIu32 " touch_crc_errors=%" PRIu32
         " internal_free=%u psram_free=%u stack_words=%u\n",
         g_emulated_frames, g_video_frames, g_fps_x10, g_frame_hash,
         (unsigned)esp32s31_korvo1_lcd_ready(), lcd.submitted_frames,
         lcd.completed_frames, lcd.dropped_frames, lcd.wait_timeouts,
         lcd.vsync_count, lcd.last_scale_us, lcd.max_scale_us,
         lcd.last_wait_us, lcd.max_wait_us,
         (unsigned)esp32s31_korvo1_touch_ready(), touch.reports,
         touch.i2c_errors, touch.checksum_errors,
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                           MALLOC_CAP_8BIT),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM |
                                           MALLOC_CAP_8BIT),
         (unsigned)uxTaskGetStackHighWaterMark(NULL));
  fflush(stdout);
}

static void stay_available_after_failure(const char *stage)
{
  for (;;)
  {
    printf("result=FAIL command=gpsp_boot backend=interp stage=%s\n", stage);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void app_main(void)
{
  ESP_LOGI(TAG, "ESP32-S31 Korvo-1 gpSP interpreter boot");

  g_lcd_ready = esp32s31_korvo1_lcd_init();
  printf("result=%s command=lcd_init ready=%u\n",
         g_lcd_ready ? "PASS" : "FAIL", (unsigned)g_lcd_ready);

  const bool touch_ready = esp32s31_korvo1_touch_init();
  printf("result=%s command=touch_init ready=%u\n",
         touch_ready ? "PASS" : "FAIL", (unsigned)touch_ready);

  struct retro_game_info game_info;
  memset(&game_info, 0, sizeof(game_info));
  if (!map_gamepak_partition(&game_info))
    stay_available_after_failure("map_gamepak");

  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);
  retro_init();

  if (!retro_load_game(&game_info))
    stay_available_after_failure("retro_load_game");

  printf("result=PASS command=gpsp_boot backend=interp rom_bytes=%zu "
         "partition_bytes=%zu lcd_ready=%u touch_ready=%u\n",
         g_rom_size, g_rom_partition_size, (unsigned)g_lcd_ready,
         (unsigned)touch_ready);
  fflush(stdout);

  int64_t next_status = esp_timer_get_time() + STATUS_PERIOD_US;
  for (;;)
  {
    retro_run();
    g_emulated_frames++;

    const int64_t now = esp_timer_get_time();
    if (now >= next_status)
    {
      print_status();
      next_status = now + STATUS_PERIOD_US;
    }

    vTaskDelay(1);
  }
}
