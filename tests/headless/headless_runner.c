#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../cpu_backend.h"
#include "../../cpu.h"
#include "../../gba_memory.h"
#include "../../jsjit_backend.h"
#include "../../libretro/libretro-common/include/libretro.h"
#include "../../main.h"

static char g_base_dir[1024] = ".";
static enum retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_RGB565;
static uint8_t *g_frame = NULL;
static size_t g_frame_size = 0;
static unsigned g_frame_width = 0;
static unsigned g_frame_height = 0;
static size_t g_frame_pitch = 0;
static unsigned g_video_frames = 0;
static cpu_backend_type g_backend = CPU_BACKEND_INTERP;

static bool ensure_frame_capacity(size_t size)
{
  if (size <= g_frame_size)
    return true;

  {
    uint8_t *next = (uint8_t *)realloc(g_frame, size);
    if (!next)
      return false;

    g_frame = next;
    g_frame_size = size;
  }

  return true;
}

static const char *lookup_variable(const char *key)
{
  if (strcmp(key, "gpsp_bios") == 0)
    return "builtin";
  if (strcmp(key, "gpsp_boot_mode") == 0)
    return "game";
  if (strcmp(key, "gpsp_frameskip") == 0)
    return "disabled";
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

static bool env_cb(unsigned cmd, void *data)
{
  switch (cmd)
  {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      g_pixel_format = *(const enum retro_pixel_format *)data;
      return g_pixel_format == RETRO_PIXEL_FORMAT_RGB565 ||
             g_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888;

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = g_base_dir;
      return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *var = (struct retro_variable *)data;
      const char *value = lookup_variable(var->key);
      if (!value)
        return false;
      var->value = value;
      return true;
    }

    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      return true;

    default:
      return false;
  }
}

static void video_cb(const void *data, unsigned width, unsigned height,
                     size_t pitch)
{
  size_t size;

  if (!data)
    return;

  size = pitch * height;
  if (!ensure_frame_capacity(size))
    return;

  memcpy(g_frame, data, size);
  g_frame_width = width;
  g_frame_height = height;
  g_frame_pitch = pitch;
  g_video_frames++;
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
}

static int16_t input_state_cb(unsigned port, unsigned device,
                              unsigned index, unsigned id)
{
  (void)port;
  (void)device;
  (void)index;
  (void)id;
  return 0;
}

static void rgb565_to_rgb888(uint16_t pixel, uint8_t *dst)
{
  unsigned r = (pixel >> 11) & 0x1f;
  unsigned g = (pixel >> 5) & 0x3f;
  unsigned b = pixel & 0x1f;

  dst[0] = (uint8_t)((r << 3) | (r >> 2));
  dst[1] = (uint8_t)((g << 2) | (g >> 4));
  dst[2] = (uint8_t)((b << 3) | (b >> 2));
}

static bool write_ppm(const char *path)
{
  FILE *fp;
  unsigned y;

  if (!g_frame || !g_frame_width || !g_frame_height)
    return false;

  fp = fopen(path, "wb");
  if (!fp)
    return false;

  fprintf(fp, "P6\n%u %u\n255\n", g_frame_width, g_frame_height);

  for (y = 0; y < g_frame_height; y++)
  {
    const uint8_t *row = g_frame + y * g_frame_pitch;
    unsigned x;

    for (x = 0; x < g_frame_width; x++)
    {
      uint8_t rgb[3];

      if (g_pixel_format == RETRO_PIXEL_FORMAT_RGB565)
      {
        uint16_t pixel = (uint16_t)(row[x * 2] | (row[x * 2 + 1] << 8));
        rgb565_to_rgb888(pixel, rgb);
      }
      else
      {
        const uint8_t *src = row + x * 4;
        rgb[0] = src[2];
        rgb[1] = src[1];
        rgb[2] = src[0];
      }

      fwrite(rgb, sizeof(rgb), 1, fp);
    }
  }

  fclose(fp);
  return true;
}

static int run_rom(const char *rom_path, const char *screenshot_path,
                   unsigned frame_count)
{
  struct retro_game_info info;
  struct retro_system_av_info av_info;
  unsigned frame;

  memset(&info, 0, sizeof(info));
  memset(&av_info, 0, sizeof(av_info));
  info.path = rom_path;

  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);
  retro_init();

  if (!retro_load_game(&info))
  {
    fprintf(stderr, "failed to load ROM: %s\n", rom_path);
    retro_deinit();
    return 1;
  }

  retro_get_system_av_info(&av_info);

  for (frame = 0; frame < frame_count; frame++)
    retro_run();

  if (!write_ppm(screenshot_path))
  {
    fprintf(stderr, "failed to write screenshot: %s\n", screenshot_path);
    retro_unload_game();
    retro_deinit();
    return 1;
  }

  printf("fps=%.6f frames=%u video_frames=%u width=%u height=%u pitch=%lu screenshot=%s\n",
         av_info.timing.fps, frame_count, g_video_frames,
         g_frame_width, g_frame_height, (unsigned long)g_frame_pitch,
         screenshot_path);
  printf("backend=%s jsjit_exec=%u jsjit_fallback_exec=%u jsjit_fallback_cycles=%u\n",
         cpu_backend_name(cpu_backend_current()),
         jsjit_backend_stat_executions(),
         jsjit_backend_stat_fallback_executions(),
         jsjit_backend_stat_fallback_cycles());
  printf("cpu pc=%08x cpsr=%08x mode=%08x halt=%08x frame_counter=%u cpu_ticks=%u execute_cycles=%u\n",
         reg[REG_PC], reg[REG_CPSR], reg[CPU_MODE], reg[CPU_HALT_STATE],
         frame_counter, cpu_ticks, execute_cycles);

  retro_unload_game();
  retro_deinit();
  return 0;
}

int main(int argc, char **argv)
{
  const char *rom_path = NULL;
  const char *screenshot_path = "capture.ppm";
  const char *backend_name = "interp";
  double seconds = 3.0;
  unsigned frames = 0;
  int i;

  for (i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc)
    {
      rom_path = argv[++i];
    }
    else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
    {
      screenshot_path = argv[++i];
    }
    else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
    {
      backend_name = argv[++i];
    }
    else if (strcmp(argv[i], "--base-dir") == 0 && i + 1 < argc)
    {
      strncpy(g_base_dir, argv[++i], sizeof(g_base_dir) - 1);
      g_base_dir[sizeof(g_base_dir) - 1] = '\0';
    }
    else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
    {
      seconds = strtod(argv[++i], NULL);
    }
    else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
    {
      frames = (unsigned)strtoul(argv[++i], NULL, 0);
    }
    else
    {
      fprintf(stderr,
              "usage: %s --rom path [--seconds n | --frames n] "
              "[--screenshot path] [--base-dir dir] [--backend interp|jsjit]\n",
              argv[0]);
      return 2;
    }
  }

  if (!rom_path)
  {
    fprintf(stderr, "--rom is required\n");
    return 2;
  }

  if (!frames)
  {
    const double fps = 59.727500569606;
    if (seconds < 0.0)
      seconds = 0.0;
    frames = (unsigned)(seconds * fps + 0.999999);
    if (!frames)
      frames = 1;
  }

  if (strcmp(backend_name, "jsjit") == 0)
    g_backend = CPU_BACKEND_JSJIT;
  else if (strcmp(backend_name, "interp") == 0)
    g_backend = CPU_BACKEND_INTERP;
  else
  {
    fprintf(stderr, "unsupported backend: %s\n", backend_name);
    return 2;
  }

  cpu_backend_set_jsjit(g_backend == CPU_BACKEND_JSJIT);
  return run_rom(rom_path, screenshot_path, frames);
}
