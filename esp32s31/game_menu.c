#include "game_menu.h"
#include "gamepak_runtime.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <libretro.h>

#include "korvo1_lcd.h"
#include "korvo1_sd.h"
#include "korvo1_usb_gamepad.h"
#include "menu_ui.h"

#define MENU_MAX_ENTRIES 128u
#define MENU_NAME_BYTES 256u
#define MENU_VISIBLE_ROWS 13u
#define MENU_ROW_HEIGHT 9
#define MENU_REPEAT_DELAY_US INT64_C(350000)
#define MENU_REPEAT_PERIOD_US INT64_C(90000)

/*
 * The emulator maps Xbox face buttons to GBA/Nintendo geometry: physical
 * Xbox A becomes RETRO B and physical Xbox B becomes RETRO A. Menus should
 * retain the conventional XInput meaning, so physical A confirms and
 * physical B goes back.
 */
#define MENU_CONFIRM_BUTTON RETRO_DEVICE_ID_JOYPAD_B
#define MENU_BACK_BUTTON RETRO_DEVICE_ID_JOYPAD_A

#ifndef GPSP_ESP32S31_MENU_AUTOROM_NAME
#define GPSP_ESP32S31_MENU_AUTOROM_NAME ""
#endif

#if GPSP_ESP32S31_DYNAREC
#define MENU_BACKEND_STATUS "RV32 JIT"
#else
#define MENU_BACKEND_STATUS "INTERPRETER"
#endif

#if CONFIG_SPIRAM_XIP_FROM_PSRAM
#define MENU_XIP_STATUS " + PSRAM XIP"
#else
#define MENU_XIP_STATUS " + FLASH XIP"
#endif

#if GPSP_ESP32S31_PROFILE
#define MENU_PROFILE_STATUS " + PROFILE"
#else
#define MENU_PROFILE_STATUS ""
#endif

#define MENU_BUILD_STATUS \
  MENU_BACKEND_STATUS MENU_XIP_STATUS MENU_PROFILE_STATUS

#define COLOR_BACKGROUND UINT16_C(0x0841)
#define COLOR_PANEL UINT16_C(0x1082)
#define COLOR_HEADER UINT16_C(0x0015)
#define COLOR_TEXT UINT16_C(0xffff)
#define COLOR_DIM UINT16_C(0xad55)
#define COLOR_SELECTED UINT16_C(0x045f)
#define COLOR_PROGRESS UINT16_C(0x07e0)
#define COLOR_ERROR UINT16_C(0xf800)

typedef struct
{
  char name[MENU_NAME_BYTES];
  uint64_t size;
  bool is_directory;
  bool is_gba;
} menu_entry_t;

static const char *TAG = "gba-menu";
static menu_entry_t *s_entries;
static size_t s_entry_count;
static bool s_directory_truncated;

static uint16_t button_bit(unsigned id)
{
  return (uint16_t)(UINT16_C(1) << id);
}

static bool has_gba_extension(const char *name)
{
  const size_t length = strlen(name);
  return length >= 4u && strcasecmp(name + length - 4u, ".gba") == 0;
}

static int compare_entries(const void *left_value, const void *right_value)
{
  const menu_entry_t *left = (const menu_entry_t *)left_value;
  const menu_entry_t *right = (const menu_entry_t *)right_value;
  if (left->is_directory != right->is_directory)
    return left->is_directory ? -1 : 1;
  return strcasecmp(left->name, right->name);
}

static bool join_path(char *output, size_t output_bytes,
                      const char *directory, const char *name)
{
  const int written = snprintf(output, output_bytes, "%s/%s",
                               directory, name);
  return written >= 0 && (size_t)written < output_bytes;
}

static bool read_directory(const char *path)
{
  DIR *directory = opendir(path);
  if (directory == NULL)
  {
    ESP_LOGE(TAG, "open directory '%s' failed: errno=%d", path, errno);
    return false;
  }

  s_entry_count = 0u;
  s_directory_truncated = false;
  struct dirent *item;
  while ((item = readdir(directory)) != NULL)
  {
    if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
      continue;
    if (strlen(item->d_name) >= MENU_NAME_BYTES)
    {
      s_directory_truncated = true;
      continue;
    }

    char full_path[ESP32S31_GAME_MENU_PATH_BYTES];
    if (!join_path(full_path, sizeof(full_path), path, item->d_name))
    {
      s_directory_truncated = true;
      continue;
    }

    struct stat status;
    if (stat(full_path, &status) != 0 ||
        (!S_ISDIR(status.st_mode) && !S_ISREG(status.st_mode)))
      continue;
    if (s_entry_count == MENU_MAX_ENTRIES)
    {
      s_directory_truncated = true;
      continue;
    }

    menu_entry_t *entry = &s_entries[s_entry_count++];
    strlcpy(entry->name, item->d_name, sizeof(entry->name));
    entry->is_directory = S_ISDIR(status.st_mode);
    entry->is_gba = !entry->is_directory && has_gba_extension(entry->name);
    entry->size = entry->is_directory ? 0u : (uint64_t)status.st_size;
  }
  closedir(directory);
  qsort(s_entries, s_entry_count, sizeof(s_entries[0]), compare_entries);
  printf("result=PASS command=menu_directory path='%s' entries=%u "
         "truncated=%u\n",
         path, (unsigned)s_entry_count, (unsigned)s_directory_truncated);
  for (size_t index = 0u; index < s_entry_count; index++)
  {
    printf("result=PASS command=menu_entry index=%u type=%s gba=%u "
           "bytes=%" PRIu64 " name='%s'\n",
           (unsigned)index,
           s_entries[index].is_directory ? "directory" : "file",
           (unsigned)s_entries[index].is_gba,
           s_entries[index].size, s_entries[index].name);
  }
  fflush(stdout);
  return true;
}

static const char *display_path(const char *path)
{
  const size_t max_characters = 38u;
  const size_t length = strlen(path);
  return length > max_characters ? path + length - max_characters : path;
}

static void present(uint16_t *pixels)
{
  if (pixels == NULL || !esp32s31_korvo1_lcd_ready())
    return;
  (void)esp32s31_korvo1_lcd_present_rgb565(
      pixels, ESP32S31_MENU_WIDTH, ESP32S31_MENU_HEIGHT,
      ESP32S31_MENU_PITCH_BYTES);
}

static void render_browser(const char *path, size_t selected)
{
  uint16_t *pixels = esp32s31_korvo1_lcd_render_buffer();
  if (pixels == NULL)
    return;

  esp32s31_menu_fill(pixels, COLOR_BACKGROUND);
  esp32s31_menu_fill_rect(pixels, 0, 0, 240, 27, COLOR_HEADER);
  esp32s31_menu_draw_text(
      pixels, 4, 1, 232, "GBA ROM MENU", COLOR_TEXT, COLOR_HEADER);
  esp32s31_menu_draw_text(
      pixels, 4, 10, 232, MENU_BUILD_STATUS,
      COLOR_PROGRESS, COLOR_HEADER);
  esp32s31_menu_draw_text(
      pixels, 4, 19, 232, display_path(path), COLOR_DIM, COLOR_HEADER);

  size_t first = 0u;
  if (selected >= MENU_VISIBLE_ROWS)
    first = selected - MENU_VISIBLE_ROWS + 1u;
  if (first + MENU_VISIBLE_ROWS > s_entry_count &&
      s_entry_count > MENU_VISIBLE_ROWS)
    first = s_entry_count - MENU_VISIBLE_ROWS;

  for (size_t row = 0u; row < MENU_VISIBLE_ROWS; row++)
  {
    const size_t index = first + row;
    const int y = 29 + (int)row * MENU_ROW_HEIGHT;
    if (index >= s_entry_count)
      break;

    const bool highlighted = index == selected;
    const uint16_t background =
        highlighted ? COLOR_SELECTED : COLOR_BACKGROUND;
    esp32s31_menu_fill_rect(pixels, 2, y - 1, 236, MENU_ROW_HEIGHT,
                            background);
    char line[MENU_NAME_BYTES + 8u];
    snprintf(line, sizeof(line), "%s%s",
             s_entries[index].is_directory ? "[D] " : "    ",
             s_entries[index].name);
    const uint16_t foreground =
        s_entries[index].is_directory || s_entries[index].is_gba ?
        COLOR_TEXT : COLOR_DIM;
    esp32s31_menu_draw_text(
        pixels, 5, y, 230, line, foreground, background);
  }

  esp32s31_menu_fill_rect(pixels, 0, 147, 240, 13, COLOR_PANEL);
  esp32s31_menu_draw_text(
      pixels, 4, 150, 232,
      s_directory_truncated ? "A OPEN B BACK  LIST FULL" :
                              "A OPEN  B BACK",
      s_directory_truncated ? COLOR_ERROR : COLOR_TEXT, COLOR_PANEL);
  present(pixels);
}

static void parent_path(char *path)
{
  const size_t root_length = strlen(ESP32S31_KORVO1_SD_MOUNT_POINT);
  if (strlen(path) <= root_length)
    return;
  char *slash = strrchr(path, '/');
  if (slash == NULL || slash < path + root_length)
    return;
  if (slash == path + root_length)
    slash[0] = '\0';
  else
    *slash = '\0';
}

static uint16_t wait_menu_event(uint16_t *previous,
                                int64_t *next_repeat)
{
  const uint16_t directional =
      button_bit(RETRO_DEVICE_ID_JOYPAD_UP) |
      button_bit(RETRO_DEVICE_ID_JOYPAD_DOWN) |
      button_bit(RETRO_DEVICE_ID_JOYPAD_LEFT) |
      button_bit(RETRO_DEVICE_ID_JOYPAD_RIGHT);
  for (;;)
  {
    const uint16_t current = esp32s31_korvo1_usb_gamepad_mask();
    const uint16_t pressed = (uint16_t)(current & ~*previous);
    const int64_t now = esp_timer_get_time();
    uint16_t event = pressed;

    if ((pressed & directional) != 0u)
      *next_repeat = now + MENU_REPEAT_DELAY_US;
    else if ((current & directional) != 0u && now >= *next_repeat)
    {
      event |= current & directional;
      *next_repeat = now + MENU_REPEAT_PERIOD_US;
    }
    *previous = current;
    if (event != 0u)
      return event;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool esp32s31_game_menu_select_rom(char *output, size_t output_bytes)
{
  if (output == NULL || output_bytes == 0u ||
      !esp32s31_korvo1_sd_mounted())
    return false;

  size_t workspace_bytes = 0u;
  s_entries = (menu_entry_t *)esp32s31_runtime_gamepak_menu_workspace(
      &workspace_bytes);
  if (s_entries == NULL ||
      workspace_bytes < sizeof(menu_entry_t) * MENU_MAX_ENTRIES)
    return false;

  char path[ESP32S31_GAME_MENU_PATH_BYTES];
  strlcpy(path, ESP32S31_KORVO1_SD_MOUNT_POINT, sizeof(path));
  if (!read_directory(path))
    return false;

  size_t selected = 0u;
  uint16_t previous = esp32s31_korvo1_usb_gamepad_mask();
  int64_t next_repeat = 0;
  render_browser(path, selected);

  if (GPSP_ESP32S31_MENU_AUTOROM_NAME[0] != '\0')
  {
    for (size_t index = 0u; index < s_entry_count; index++)
    {
      if (!s_entries[index].is_gba ||
          strcmp(s_entries[index].name,
                 GPSP_ESP32S31_MENU_AUTOROM_NAME) != 0)
        continue;
      if (!join_path(output, output_bytes, path, s_entries[index].name))
        return false;
      printf("result=PASS command=menu_autorom index=%u path='%s'\n",
             (unsigned)index, output);
      fflush(stdout);
      render_browser(path, index);
      vTaskDelay(pdMS_TO_TICKS(250));
      return true;
    }
    ESP_LOGE(TAG, "autorom '%s' not found in TF root",
             GPSP_ESP32S31_MENU_AUTOROM_NAME);
    return false;
  }

  for (;;)
  {
    const uint16_t event = wait_menu_event(&previous, &next_repeat);
    if ((event & button_bit(RETRO_DEVICE_ID_JOYPAD_UP)) != 0u &&
        selected > 0u)
      selected--;
    else if ((event & button_bit(RETRO_DEVICE_ID_JOYPAD_DOWN)) != 0u &&
             selected + 1u < s_entry_count)
      selected++;
    else if ((event & button_bit(RETRO_DEVICE_ID_JOYPAD_LEFT)) != 0u)
      selected = selected > MENU_VISIBLE_ROWS ?
          selected - MENU_VISIBLE_ROWS : 0u;
    else if ((event & button_bit(RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0u &&
             s_entry_count != 0u)
    {
      selected += MENU_VISIBLE_ROWS;
      if (selected >= s_entry_count)
        selected = s_entry_count - 1u;
    }
    else if ((event & button_bit(MENU_BACK_BUTTON)) != 0u)
    {
      if (strcmp(path, ESP32S31_KORVO1_SD_MOUNT_POINT) != 0)
      {
        parent_path(path);
        if (!read_directory(path))
          return false;
        selected = 0u;
      }
    }
    else if ((event & button_bit(MENU_CONFIRM_BUTTON)) != 0u &&
             selected < s_entry_count)
    {
      char selected_path[ESP32S31_GAME_MENU_PATH_BYTES];
      if (!join_path(selected_path, sizeof(selected_path), path,
                     s_entries[selected].name))
      {
        esp32s31_game_menu_show_error("PATH IS TOO LONG",
                                      s_entries[selected].name);
        continue;
      }
      if (s_entries[selected].is_directory)
      {
        if (!read_directory(selected_path))
        {
          esp32s31_game_menu_show_error("OPEN DIRECTORY FAILED",
                                        s_entries[selected].name);
          continue;
        }
        strlcpy(path, selected_path, sizeof(path));
        selected = 0u;
      }
      else if (s_entries[selected].is_gba)
      {
        if (strlen(selected_path) + 1u > output_bytes)
        {
          esp32s31_game_menu_show_error("PATH IS TOO LONG",
                                        s_entries[selected].name);
          continue;
        }
        strlcpy(output, selected_path, output_bytes);
        return true;
      }
    }
    render_browser(path, selected);
  }
}

static const char *base_name(const char *path)
{
  if (path == NULL)
    return "";
  const char *slash = strrchr(path, '/');
  return slash == NULL ? path : slash + 1;
}

void esp32s31_game_menu_show_progress(const char *stage,
                                      const char *path,
                                      uint32_t completed,
                                      uint32_t total,
                                      const char *detail)
{
  uint16_t *pixels = esp32s31_korvo1_lcd_render_buffer();
  if (pixels == NULL)
    return;
  if (stage == NULL)
    stage = "LOADING ROM";
  if (detail == NULL)
    detail = "";

  esp32s31_menu_fill(pixels, COLOR_BACKGROUND);
  esp32s31_menu_fill_rect(pixels, 0, 0, 240, 24, COLOR_HEADER);
  esp32s31_menu_draw_text(
      pixels, 4, 7, 232, stage, COLOR_TEXT, COLOR_HEADER);
  esp32s31_menu_draw_text(
      pixels, 8, 43, 224, base_name(path), COLOR_TEXT, COLOR_BACKGROUND);
  esp32s31_menu_draw_progress(
      pixels, 8, 69, 224, 12, completed, total,
      COLOR_PROGRESS, COLOR_PANEL, COLOR_TEXT);

  char amount[48];
  const unsigned percent = total == 0u ? 0u :
      (unsigned)(((uint64_t)completed * 100u) / total);
  snprintf(amount, sizeof(amount), "%u / %u  %u%%",
           (unsigned)completed, (unsigned)total, percent);
  esp32s31_menu_draw_text(
      pixels, 8, 89, 224, amount, COLOR_TEXT, COLOR_BACKGROUND);
  esp32s31_menu_draw_text(
      pixels, 8, 105, 224, detail, COLOR_DIM, COLOR_BACKGROUND);
  present(pixels);
}

void esp32s31_game_menu_show_error(const char *message,
                                   const char *detail)
{
  uint16_t *pixels = esp32s31_korvo1_lcd_render_buffer();
  if (pixels == NULL)
    return;
  if (message == NULL)
    message = "ERROR";
  if (detail == NULL)
    detail = "";

  esp32s31_menu_fill(pixels, COLOR_BACKGROUND);
  esp32s31_menu_fill_rect(pixels, 0, 0, 240, 24, COLOR_ERROR);
  esp32s31_menu_draw_text(
      pixels, 4, 7, 232, "ROM LOAD ERROR", COLOR_TEXT, COLOR_ERROR);
  esp32s31_menu_draw_text(
      pixels, 8, 48, 224, message, COLOR_TEXT, COLOR_BACKGROUND);
  esp32s31_menu_draw_text(
      pixels, 8, 66, 224, detail, COLOR_DIM, COLOR_BACKGROUND);
  esp32s31_menu_draw_text(
      pixels, 8, 145, 224, "RETURNING TO MENU", COLOR_TEXT,
      COLOR_BACKGROUND);
  present(pixels);
}
