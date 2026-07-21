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
#include "gpsp_profile.h"

#include <libretro.h>

#include "gamepak_direct_mmap.h"
#if GPSP_ESP32S31_MENU_BOOT
#include "game_menu.h"
#include "gamepak_runtime.h"
#include "korvo1_sd.h"
#else
#include "gamepak_container.h"
#endif
#if GPSP_ESP32S31_DYNAREC
#include "common.h"
#include "jit_platform.h"
#if GPSP_ESP32S31_PSRAM_FAULT_TRACE
#include "psram_fault_trace.h"
#endif
#include "riscv/riscv_emit.h"
#include "uart_debug.h"
#endif
#include "korvo1_lcd.h"
#include "korvo1_usb_gamepad.h"
#include "rgb565_scale3x.h"

#if CONFIG_FREERTOS_NUMBER_OF_CORES != 1
#error "ESP32-S31 gpSP firmware requires CONFIG_FREERTOS_UNICORE=y"
#endif

#define GAMEPAK_PARTITION "gamepak"
#define GAMEPAK_PAGE_BYTES 0x8000u
#define FPS_WINDOW_US INT64_C(1000000)
#define STATUS_PERIOD_US INT64_C(5000000)

#if GPSP_ESP32S31_DYNAREC
#define GPSP_ESP32S31_BACKEND_NAME "dynarec"
#else
#define GPSP_ESP32S31_BACKEND_NAME "interp"
#endif
#if GPSP_ESP32S31_MENU_BOOT
#define GPSP_ESP32S31_BOOT_NAME "menu"
#else
#define GPSP_ESP32S31_BOOT_NAME "flash"
#endif

static const char *TAG = "gpsp-esp32s31";
static const char *g_base_dir = ".";

/* gpSP software render target; LCD presentation reads it after each frame. */
extern uint16_t *gba_screen_pixels;

static size_t g_rom_size;
static size_t g_rom_partition_size;
static const char *g_rom_format = "unmapped";
static const char *g_rom_source = "unmapped";
static uint32_t g_rom_stored_pages;
static uint32_t g_rom_image_bytes;
static uint32_t g_rom_unique_pages;
static uint32_t g_rom_duplicate_pages;
static uint32_t g_rom_psram_pages;
static uint32_t g_rom_flash_pages;
static uint32_t g_rom_flash_skip_blocks;
static uint32_t g_rom_flash_write_blocks;
static uint32_t g_rom_flash_erased_sectors;
#if !GPSP_ESP32S31_MENU_BOOT
static const void *g_rom_data;
static esp_partition_mmap_handle_t g_rom_mmap_handle;
static esp32s31_gamepak_container_t g_rom_container;
static bool g_rom_container_active;
#endif

static bool g_lcd_ready;
static uint32_t g_emulated_frames;
static uint32_t g_video_frames;
#if GPSP_ESP32S31_PROFILE
static uint32_t g_frame_hash;
#endif
static uint32_t g_fps_x10;
static uint32_t g_fps_window_frames;
static int64_t g_fps_window_start;
static uint16_t g_joypad_mask;
static unsigned g_video_error_logs;
#if GPSP_ESP32S31_PROFILE
static uint32_t g_profile_last_frames;
#if GPSP_ESP32S31_PC_PROFILE
static bool g_profile_sampler_started;
#endif
#endif

static const char *lookup_variable(const char *key)
{
  if (strcmp(key, "gpsp_drc") == 0)
#if GPSP_ESP32S31_DYNAREC
    return "enabled";
#else
    return "disabled";
#endif
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

  g_video_frames++;
  update_fps();
  if (g_lcd_ready)
  {
    (void)esp32s31_korvo1_lcd_present_rgb565(
        data, width, height, pitch);
  }
}

#if GPSP_ESP32S31_PROFILE
static uint32_t hash_current_frame(void)
{
  if (gba_screen_pixels == NULL)
    return 0u;

  const uint8_t *pixels = (const uint8_t *)gba_screen_pixels;
  const size_t row_bytes = ESP32S31_GBA_WIDTH * sizeof(uint16_t);
  uint32_t hash = UINT32_C(2166136261);
  for (unsigned y = 0; y < ESP32S31_GBA_HEIGHT; y++)
  {
    const uint8_t *row = pixels + (size_t)y * row_bytes;
    for (size_t x = 0; x < row_bytes; x++)
    {
      hash ^= row[x];
      hash *= UINT32_C(16777619);
    }
  }
  return hash;
}

#endif

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
  g_joypad_mask = esp32s31_korvo1_usb_gamepad_mask();
#if GPSP_ESP32S31_DYNAREC
  g_joypad_mask = esp32s31_uart_debug_apply_joypad(g_joypad_mask);
  esp32s31_uart_debug_record_joypad(g_joypad_mask);
#endif
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

#if !GPSP_ESP32S31_MENU_BOOT
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
#endif

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

#if !GPSP_ESP32S31_MENU_BOOT
static const uint8_t *resolve_gamepak_page(
    void *context, uint32_t logical_page)
{
  (void)context;
  if (g_rom_container_active)
    return esp32s31_gamepak_container_page(&g_rom_container, logical_page);

  const size_t page_offset = (size_t)logical_page * GAMEPAK_PAGE_BYTES;
  if (g_rom_data == NULL || page_offset >= g_rom_size)
    return NULL;
  return (const uint8_t *)g_rom_data + page_offset;
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
  g_rom_source = "flash_mmap";
  const uint8_t *first_page = NULL;
  if (esp32s31_gamepak_container_probe(g_rom_data, g_rom_partition_size))
  {
    char container_error[128];
    if (!esp32s31_gamepak_container_open(
            g_rom_data, g_rom_partition_size, &g_rom_container,
            container_error, sizeof(container_error)))
    {
      ESP_LOGE(TAG, "invalid gamepak page container: %s", container_error);
      esp_partition_munmap(g_rom_mmap_handle);
      g_rom_data = NULL;
      g_rom_mmap_handle = 0;
      return false;
    }
    g_rom_container_active = true;
    g_rom_format = "gpsp-page-v1";
    g_rom_size = g_rom_container.rom_bytes;
    g_rom_stored_pages = g_rom_container.stored_page_count;
    g_rom_image_bytes = g_rom_container.image_bytes;
    first_page = esp32s31_gamepak_container_page(&g_rom_container, 0u);
  }
  else
  {
    memset(&g_rom_container, 0, sizeof(g_rom_container));
    g_rom_container_active = false;
    g_rom_format = "raw";
    g_rom_size = infer_raw_rom_size((const uint8_t *)g_rom_data,
                                    g_rom_partition_size);
    g_rom_stored_pages =
        (uint32_t)((g_rom_size + GAMEPAK_PAGE_BYTES - 1u) /
                   GAMEPAK_PAGE_BYTES);
    g_rom_image_bytes = (uint32_t)g_rom_size;
    first_page = (const uint8_t *)g_rom_data;
  }

  const uint32_t logical_pages =
      (uint32_t)((g_rom_size + GAMEPAK_PAGE_BYTES - 1u) /
                 GAMEPAK_PAGE_BYTES);
  g_rom_unique_pages = g_rom_stored_pages;
  g_rom_duplicate_pages = logical_pages >= g_rom_stored_pages ?
      logical_pages - g_rom_stored_pages : 0u;

  if (g_rom_size == 0u || first_page == NULL ||
      !log_cartridge_header(first_page, g_rom_size))
  {
    ESP_LOGE(TAG,
             "gamepak partition is blank or invalid; flash a .gba first");
    esp_partition_munmap(g_rom_mmap_handle);
    g_rom_data = NULL;
    g_rom_mmap_handle = 0;
    return false;
  }

  info->path = NULL;
  info->data = first_page;
  info->size = g_rom_size;
  info->meta = NULL;
  gamepak_set_direct_mmap_source(
      resolve_gamepak_page, NULL, (uint32_t)g_rom_size);

  ESP_LOGI(TAG,
           "mapped gamepak direct-mmap: format=%s flash=0x%08" PRIx32
           " partition_bytes=%zu rom_bytes=%zu image_bytes=%" PRIu32
           " logical_pages=%zu stored_pages=%" PRIu32
           " rom_cache_bytes=0",
           g_rom_format, partition->address, g_rom_partition_size, g_rom_size,
           g_rom_image_bytes,
           (g_rom_size + GAMEPAK_PAGE_BYTES - 1u) / GAMEPAK_PAGE_BYTES,
           g_rom_stored_pages);
  return true;
}
#else
static bool select_runtime_gamepak(struct retro_game_info *info)
{
  char selected_path[ESP32S31_GAME_MENU_PATH_BYTES];
  char error[160];

  for (;;)
  {
    const esp_err_t mount_error = esp32s31_korvo1_sd_mount();
    if (mount_error != ESP_OK)
    {
      esp32s31_game_menu_show_error("INSERT OR CHECK TF CARD",
                                    esp_err_to_name(mount_error));
      printf("result=FAIL command=tf_mount error=%s\n",
             esp_err_to_name(mount_error));
      fflush(stdout);
      vTaskDelay(pdMS_TO_TICKS(1500));
      continue;
    }

    if (!esp32s31_game_menu_select_rom(
            selected_path, sizeof(selected_path)))
    {
      esp32s31_game_menu_show_error("TF DIRECTORY READ FAILED", "RETRYING");
      (void)esp32s31_korvo1_sd_unmount();
      vTaskDelay(pdMS_TO_TICKS(1500));
      continue;
    }

    esp32s31_runtime_gamepak_stats_t stats;
    if (!esp32s31_runtime_gamepak_load(
            selected_path, info, &stats, error, sizeof(error)))
    {
      ESP_LOGE(TAG, "load '%s' failed: %s", selected_path, error);
      esp32s31_game_menu_show_error("ROM LOAD FAILED", error);
      printf("result=FAIL command=runtime_gamepak_load path=%s error=%s\n",
             selected_path, error);
      fflush(stdout);
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    if (!log_cartridge_header((const uint8_t *)info->data, info->size))
    {
      esp32s31_game_menu_show_error("INVALID GBA HEADER", selected_path);
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    g_rom_size = stats.rom_bytes;
    g_rom_partition_size =
        (size_t)stats.flash_capacity_pages * GAMEPAK_PAGE_BYTES;
    g_rom_format = "runtime-pages";
    g_rom_source = stats.flash_pages == 0u ? "static_psram" :
                                             "static_psram+flash_mmap";
    g_rom_stored_pages = stats.psram_pages + stats.flash_pages;
    g_rom_image_bytes =
        g_rom_stored_pages * GAMEPAK_PAGE_BYTES;
    g_rom_unique_pages = stats.unique_pages;
    g_rom_duplicate_pages = stats.duplicate_pages;
    g_rom_psram_pages = stats.psram_pages;
    g_rom_flash_pages = stats.flash_pages;
    g_rom_flash_skip_blocks = stats.flash_blocks_skipped;
    g_rom_flash_write_blocks = stats.flash_blocks_written;
    g_rom_flash_erased_sectors = stats.flash_sectors_erased;

    printf("result=PASS command=runtime_gamepak_load"
           " rom_bytes=%" PRIu32 " pages=%" PRIu32
           " unique_pages=%" PRIu32 " duplicate_pages=%" PRIu32
           " psram_pages=%" PRIu32
           " psram_capacity=%" PRIu32 " flash_pages=%" PRIu32
           " flash_skip_blocks=%" PRIu32
           " flash_write_blocks=%" PRIu32
           " flash_erased_4k=%" PRIu32 "\n",
           stats.rom_bytes, stats.logical_pages,
           stats.unique_pages, stats.duplicate_pages,
           stats.psram_pages, stats.psram_capacity_pages,
           stats.flash_pages, stats.flash_blocks_skipped,
           stats.flash_blocks_written, stats.flash_sectors_erased);
    fflush(stdout);
    (void)esp32s31_korvo1_sd_unmount();
    return true;
  }
}
#endif

#if GPSP_ESP32S31_PROFILE
static void print_status(void)
{
  const uint32_t profile_hash = gpsp_profile_begin();
  g_frame_hash = hash_current_frame();
  gpsp_profile_end(GPSP_PROFILE_FRAME_HASH, profile_hash);

  esp32s31_lcd_stats_t lcd = {0};
  esp32s31_usb_gamepad_stats_t usb_gamepad = {0};
  esp32s31_korvo1_lcd_get_stats(&lcd);
  esp32s31_korvo1_usb_gamepad_get_stats(&usb_gamepad);
  const bool status_touch_ready = false;
  const uint32_t touch_reports = 0u;
  const uint32_t touch_i2c_errors = 0u;
  const uint32_t touch_checksum_errors = 0u;

  printf("result=PASS command=gpsp_status backend=%s "
         "emulated_frames=%" PRIu32 " video_frames=%" PRIu32
         " fps_x10=%" PRIu32 " fb_hash=0x%08" PRIx32
         " lcd_ready=%u lcd_submitted=%" PRIu32
         " lcd_completed=%" PRIu32 " lcd_dropped=%" PRIu32
         " lcd_timeouts=%" PRIu32 " lcd_vsync=%" PRIu32
         " scaler=%s render_mem=%s ewram_mem=psram ewram_bytes=262144"
         " iwram_mem=%s iwram_bytes=32768"
         " bios_mem=%s bios_bytes=16384"
         " cold_state_mem=%s"
         " vram_mem=%s vram_bytes=98304"
         " arm_code_mem=%s"
         " video_hot_code_mem=%s"
         " read_map_mem=%s"
         " sound_mem=%s sound_bytes=%u"
         " obj_links_mem=%s obj_links_bytes=20480"
         " hot_helpers_mem=%s"
         " rom_format=%s rom_source=%s rom_cache_bytes=0"
         " rom_image_bytes=%" PRIu32 " rom_stored_pages=%" PRIu32
         " rom_unique_pages=%" PRIu32
         " rom_duplicate_pages=%" PRIu32
         " rom_psram_pages=%" PRIu32
         " rom_flash_pages=%" PRIu32
         " flash_skip_blocks=%" PRIu32
         " flash_write_blocks=%" PRIu32
         " flash_erased_4k=%" PRIu32
         " bounce_rows=%u"
         " scale_us=%" PRIu32 " scale_max_us=%" PRIu32
         " scale_prepare_us=%" PRIu32 " scale_transfer_us=%" PRIu32
         " bounce_callbacks=%" PRIu32
         " bounce_discontinuities=%" PRIu32
         " bounce_fill_max_us=%" PRIu32
         " snapshot_copy_us=%" PRIu32
         " snapshot_copy_max_us=%" PRIu32
         " snapshot_copy_interrupts=%" PRIu32
         " wait_us=%" PRIu32 " wait_max_us=%" PRIu32
         " touch_ready=%u touch_reports=%" PRIu32
         " touch_i2c_errors=%" PRIu32 " touch_crc_errors=%" PRIu32
         " usb_ready=%u usb_connected=%u usb_xinput=%u"
         " usb_vid=0x%04x usb_pid=0x%04x"
         " usb_mask=0x%04x usb_reports=%" PRIu32
         " usb_changed=%" PRIu32 " usb_errors=%" PRIu32
         " internal_free=%u internal_largest=%u"
         " dma_free=%u dma_largest=%u"
         " psram_free=%u psram_largest=%u stack_free_bytes=%u\n",
         GPSP_ESP32S31_BACKEND_NAME,
         g_emulated_frames, g_video_frames, g_fps_x10, g_frame_hash,
         (unsigned)esp32s31_korvo1_lcd_ready(), lcd.submitted_frames,
         lcd.completed_frames, lcd.dropped_frames, lcd.wait_timeouts,
         lcd.vsync_count, esp32s31_korvo1_lcd_scaler_name(),
         esp32s31_korvo1_lcd_render_memory_name(),
         ESP32S31_IWRAM_INTERNAL ? "sram" : "psram",
         ESP32S31_BIOS_INTERNAL ? "sram" : "psram",
         ESP32S31_COLD_STATE_INTERNAL ? "sram" : "psram",
         ESP32S31_VRAM_INTERNAL ? "sram" : "psram",
         ESP32S31_EXECUTE_ARM_INTERNAL ? "sram" : "psram",
         ESP32S31_VIDEO_HOT_INTERNAL ? "sram" : "psram",
         ESP32S31_MEMORY_MAP_INTERNAL ? "sram" : "psram",
         ESP32S31_SOUND_BUFFER_INTERNAL ? "sram" : "psram",
         (unsigned)ESP32S31_SOUND_BUFFER_BYTES,
         ESP32S31_OBJ_LIST_INTERNAL ? "sram" : "psram",
         ESP32S31_HOT_HELPERS_INTERNAL ? "sram" : "psram",
         g_rom_format, g_rom_source, g_rom_image_bytes, g_rom_stored_pages,
         g_rom_unique_pages, g_rom_duplicate_pages,
         g_rom_psram_pages, g_rom_flash_pages,
         g_rom_flash_skip_blocks, g_rom_flash_write_blocks,
         g_rom_flash_erased_sectors,
         esp32s31_korvo1_lcd_bounce_source_rows(),
         lcd.last_scale_us, lcd.max_scale_us,
         lcd.last_scale_prepare_us, lcd.last_scale_transfer_us,
         lcd.bounce_callbacks, lcd.bounce_discontinuities,
         lcd.bounce_fill_max_us,
         lcd.last_snapshot_copy_us, lcd.max_snapshot_copy_us,
         lcd.snapshot_copy_interrupts,
         lcd.last_wait_us, lcd.max_wait_us,
         (unsigned)status_touch_ready, touch_reports,
         touch_i2c_errors, touch_checksum_errors,
         (unsigned)usb_gamepad.ready, (unsigned)usb_gamepad.connected,
         (unsigned)usb_gamepad.xinput,
         usb_gamepad.vid, usb_gamepad.pid, usb_gamepad.joypad_mask,
         usb_gamepad.input_reports, usb_gamepad.changed_reports,
         usb_gamepad.transfer_errors,
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
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM |
                                           MALLOC_CAP_8BIT),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                    MALLOC_CAP_8BIT),
         (unsigned)uxTaskGetStackHighWaterMark(NULL));
#if GPSP_ESP32S31_DYNAREC
  riscv_runtime_stats jit = {0};
  riscv_get_runtime_stats(&jit);
  const uint32_t native_ops =
      jit.native_data_proc_insns + jit.native_branch_insns +
      jit.native_load_insns + jit.native_store_insns +
      jit.native_psr_insns;
  printf("result=PASS command=jit_status backend=dynarec"
         " blocks_emitted=%u native_blocks=%u"
         " bios_blocks_emitted=%u bios_native_blocks=%u"
         " bios_fallbacks=%u"
         " fallbacks=%u initial_fallbacks=%u"
         " relookup_fallbacks=%u unsupported_fallbacks=%u"
         " native_ops=%u native_data_proc=%u"
         " native_branch=%u native_load=%u"
         " native_store=%u native_psr=%u thumb_helpers=%u"
         " rom_code_bytes=%zu ram_code_bytes=%zu"
         " rom_cache_bytes=%u ram_cache_bytes=%u\n",
         (unsigned)jit.blocks_emitted, (unsigned)jit.blocks_executed,
         (unsigned)jit.bios_native_blocks_emitted,
         (unsigned)jit.bios_native_blocks_executed,
         (unsigned)jit.bios_interpreter_fallbacks,
         (unsigned)jit.interpreter_fallbacks,
         (unsigned)jit.initial_lookup_fallbacks,
         (unsigned)jit.relookup_fallbacks,
         (unsigned)jit.unsupported_fallbacks,
         (unsigned)native_ops, (unsigned)jit.native_data_proc_insns,
         (unsigned)jit.native_branch_insns,
         (unsigned)jit.native_load_insns,
         (unsigned)jit.native_store_insns,
         (unsigned)jit.native_psr_insns,
         (unsigned)jit.thumb_helper_insns,
         (size_t)(rom_translation_ptr - rom_translation_cache),
         (size_t)(ram_translation_ptr - ram_translation_cache),
         (unsigned)ROM_TRANSLATION_CACHE_SIZE,
         (unsigned)RAM_TRANSLATION_CACHE_SIZE);
#endif
  fflush(stdout);
}
#endif

static void stay_available_after_failure(const char *stage)
{
  for (;;)
  {
    printf("result=FAIL command=gpsp_boot backend=%s stage=%s\n",
           GPSP_ESP32S31_BACKEND_NAME, stage);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void app_main(void)
{
  ESP_LOGI(TAG,
           "ESP32-S31 Korvo-1 gpSP %s boot; cpu=%dMHz cores=%d",
           GPSP_ESP32S31_BACKEND_NAME,
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
           CONFIG_FREERTOS_NUMBER_OF_CORES);
  printf("result=PASS command=runtime_config cpu_mhz=%d cores=%d "
         "unicore=1 boot_mode=%s\n",
         CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
         CONFIG_FREERTOS_NUMBER_OF_CORES, GPSP_ESP32S31_BOOT_NAME);

#if !GPSP_ESP32S31_MENU_BOOT
  size_t layout_probe_pad_bytes = 0u;
  void *const layout_probe_pad =
      esp32s31_gamepak_layout_probe_pad(&layout_probe_pad_bytes);
  if (layout_probe_pad_bytes != 0u)
    printf("result=PASS command=layout_probe pad=0x%08" PRIxPTR
           " bytes=%zu\n",
           (uintptr_t)layout_probe_pad, layout_probe_pad_bytes);
#endif

#if GPSP_ESP32S31_DYNAREC && GPSP_ESP32S31_PSRAM_FAULT_TRACE
  const bool psram_fault_trace_ready =
      esp32s31_psram_fault_trace_init();
  printf("result=%s command=psram_fault_trace ready=%u "
         "capture=axi_addr+jit_lookup+cache_state\n",
         psram_fault_trace_ready ? "PASS" : "FAIL",
         (unsigned)psram_fault_trace_ready);
  fflush(stdout);
#endif

  g_lcd_ready = esp32s31_korvo1_lcd_init();
  printf("result=%s command=lcd_init ready=%u\n",
         g_lcd_ready ? "PASS" : "FAIL", (unsigned)g_lcd_ready);

  const bool touch_ready = false;
  printf("result=PASS command=touch_init ready=0 skipped=xinput_only\n");

  const esp_err_t usb_error = esp32s31_korvo1_usb_gamepad_init();
  printf("result=%s command=usb_gamepad_init ready=%u error=%s\n",
         usb_error == ESP_OK ? "PASS" : "FAIL",
         (unsigned)(usb_error == ESP_OK), esp_err_to_name(usb_error));

  uint16_t *initial_render_buffer =
      esp32s31_korvo1_lcd_render_buffer();
  if (initial_render_buffer != NULL)
    gba_screen_pixels = initial_render_buffer;
#if GPSP_ESP32S31_MENU_BOOT
  if (!g_lcd_ready)
    stay_available_after_failure("menu_lcd");
  if (usb_error != ESP_OK)
    stay_available_after_failure("menu_usb_gamepad");
#endif

#if GPSP_ESP32S31_DYNAREC
  esp32s31_jit_selftest_result_t jit_selftest = {0};
  const bool jit_selftest_ok =
      esp32s31_jit_cache_selftest(&jit_selftest);
  printf("result=%s command=jit_cache_selftest backend=dynarec"
         " rom_cache=0x%08" PRIxPTR " ram_cache=0x%08" PRIxPTR
         " rom_external=%u ram_external=%u return_value=%" PRIu32
         " patched_return_value=%" PRIu32
         " rom_cache_bytes=%u ram_cache_bytes=%u\n",
         jit_selftest_ok ? "PASS" : "FAIL",
         jit_selftest.rom_cache_address, jit_selftest.ram_cache_address,
         (unsigned)jit_selftest.rom_cache_external,
         (unsigned)jit_selftest.ram_cache_external,
         jit_selftest.return_value, jit_selftest.patched_return_value,
         (unsigned)ROM_TRANSLATION_CACHE_SIZE,
         (unsigned)RAM_TRANSLATION_CACHE_SIZE);
  fflush(stdout);
  if (!jit_selftest_ok)
    stay_available_after_failure("jit_cache_selftest");
#endif

  struct retro_game_info game_info;
  memset(&game_info, 0, sizeof(game_info));
#if GPSP_ESP32S31_MENU_BOOT
  if (!select_runtime_gamepak(&game_info))
    stay_available_after_failure("select_runtime_gamepak");
#else
  if (!map_gamepak_partition(&game_info))
    stay_available_after_failure("map_gamepak");
#endif

  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);
  retro_init();

  if (!retro_load_game(&game_info))
    stay_available_after_failure("retro_load_game");

  printf("result=PASS command=gpsp_boot backend=%s rom_bytes=%zu "
         "boot_mode=%s partition_bytes=%zu rom_format=%s rom_source=%s "
         "rom_cache_bytes=0 rom_image_bytes=%" PRIu32
         " rom_stored_pages=%" PRIu32
         " unique_pages=%" PRIu32 " duplicate_pages=%" PRIu32
         " psram_pages=%" PRIu32 " flash_pages=%" PRIu32
         " flash_skip_blocks=%" PRIu32
         " flash_write_blocks=%" PRIu32
         " flash_erased_4k=%" PRIu32
         " lcd_ready=%u touch_ready=%u\n",
         GPSP_ESP32S31_BACKEND_NAME,
         g_rom_size, GPSP_ESP32S31_BOOT_NAME, g_rom_partition_size,
         g_rom_format, g_rom_source, g_rom_image_bytes,
         g_rom_stored_pages, g_rom_unique_pages, g_rom_duplicate_pages,
         g_rom_psram_pages, g_rom_flash_pages, g_rom_flash_skip_blocks,
         g_rom_flash_write_blocks, g_rom_flash_erased_sectors,
         (unsigned)g_lcd_ready, (unsigned)touch_ready);
  fflush(stdout);

#if GPSP_ESP32S31_DYNAREC
  esp32s31_uart_debug_init();
#endif

#if GPSP_ESP32S31_PROFILE
  gpsp_profile_reset();
  g_profile_last_frames = g_emulated_frames;
  int64_t next_status = esp_timer_get_time() + STATUS_PERIOD_US;
#endif
  for (;;)
  {
#if GPSP_ESP32S31_DYNAREC
    esp32s31_uart_debug_poll();
    if (esp32s31_uart_debug_should_run_frame())
#endif
    {
      retro_run();
      g_emulated_frames++;
#if GPSP_ESP32S31_DYNAREC
      esp32s31_uart_debug_frame_complete();
#endif
    }

#if GPSP_ESP32S31_PROFILE
    const int64_t now = esp_timer_get_time();
    if (now >= next_status)
    {
      print_status();
      const uint32_t profile_frames =
          g_emulated_frames - g_profile_last_frames;
      g_profile_last_frames = g_emulated_frames;
      gpsp_profile_print_window(profile_frames);
#if GPSP_ESP32S31_PC_PROFILE
      (void)gpsp_profile_sampler_dump_if_ready();
      if (!g_profile_sampler_started)
        g_profile_sampler_started = gpsp_profile_sampler_start();
#endif
      next_status = now + STATUS_PERIOD_US;
    }
#endif

    const uint32_t profile_delay = gpsp_profile_begin();
    vTaskDelay(1);
    gpsp_profile_end(GPSP_PROFILE_FRAME_DELAY, profile_delay);
  }
}
