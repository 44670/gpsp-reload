#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dhry_result.h"
#include "../../libretro/libretro-common/include/libretro.h"

static const char *g_base_dir = ".";

static bool env_cb(unsigned cmd, void *data)
{
  switch (cmd)
  {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return true;

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = g_base_dir;
      return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;

    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
      return true;

    default:
      return false;
  }
}

static void video_cb(const void *data, unsigned width, unsigned height,
                     size_t pitch)
{
  (void)data;
  (void)width;
  (void)height;
  (void)pitch;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
  (void)data;
  return frames;
}

static void input_poll_cb(void)
{
}

static void print_result(const dhry_result_t *result)
{
  printf("magic=0x%08x status=%u iterations=%u int_glob=%d bool_glob=%d "
         "ch1=%c ch2=%c arr1[8]=%d arr2[8][7]=%d ptr=%d next=%d\n",
         result->magic, result->status, result->iterations,
         result->int_glob, result->bool_glob,
         (char)result->ch_1_glob, (char)result->ch_2_glob,
         result->arr_1_8, result->arr_2_8_7,
         result->ptr_int_comp, result->next_ptr_int_comp);
}

static int run_rom(const char *rom_path, unsigned max_frames)
{
  struct retro_game_info info;
  const dhry_result_t *result;
  const uint8_t *system_ram;
  size_t system_ram_size;
  unsigned frame;

  memset(&info, 0, sizeof(info));
  info.path = rom_path;

  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_init();

  if (!retro_load_game(&info))
  {
    fprintf(stderr, "failed to load ROM: %s\n", rom_path);
    retro_deinit();
    return 1;
  }

  system_ram = (const uint8_t *)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
  system_ram_size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
  if (!system_ram || system_ram_size < sizeof(dhry_result_t))
  {
    fprintf(stderr, "system RAM mapping unavailable\n");
    retro_unload_game();
    retro_deinit();
    return 1;
  }

  result = (const dhry_result_t *)system_ram;
  for (frame = 0; frame < max_frames; frame++)
  {
    retro_run();
    if (result->magic == DHRY_RESULT_MAGIC)
      break;
  }

  if (result->magic != DHRY_RESULT_MAGIC)
  {
    fprintf(stderr, "benchmark did not finish within %u frames\n", max_frames);
    retro_unload_game();
    retro_deinit();
    return 1;
  }

  print_result(result);

  retro_unload_game();
  retro_deinit();

  if (result->status != DHRY_STATUS_PASS)
  {
    fprintf(stderr, "benchmark result validation failed\n");
    return 1;
  }

  return 0;
}

int main(int argc, char **argv)
{
  const char *rom_path = "dhrystone.gba";
  unsigned max_frames = 300;
  int i;

  for (i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc)
    {
      rom_path = argv[++i];
    }
    else if (strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc)
    {
      max_frames = (unsigned)strtoul(argv[++i], NULL, 0);
    }
    else
    {
      fprintf(stderr, "usage: %s [--rom path] [--max-frames n]\n", argv[0]);
      return 2;
    }
  }

  return run_rom(rom_path, max_frames);
}
