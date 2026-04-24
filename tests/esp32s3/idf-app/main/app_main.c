#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include <libretro.h>

#include "../../../dhrystone/dhry_result.h"

uint32_t xtensa_jit_get_blocks_emitted(void);
uint32_t xtensa_jit_get_blocks_executed(void);
uint32_t xtensa_jit_get_generic_fallbacks(void);
uint32_t xtensa_jit_get_unsupported_opcodes(void);
uint32_t xtensa_jit_get_thumb_blocks(void);

extern const uint8_t dhrystone_arm_gba_start[] asm("_binary_dhrystone_arm_gba_start");
extern const uint8_t dhrystone_arm_gba_end[] asm("_binary_dhrystone_arm_gba_end");

static const char *TAG = "gpsp-esp32s3";
static const char *g_base_dir = ".";
static unsigned g_video_frames;

static const char *lookup_variable(const char *key)
{
  if (strcmp(key, "gpsp_drc") == 0)
    return strcmp(GPSP_TEST_BACKEND, "dynarec") == 0 ? "enabled" : "disabled";
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
      return true;

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

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
  (void)data;
  (void)width;
  (void)height;
  (void)pitch;
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

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
  (void)port;
  (void)device;
  (void)index;
  (void)id;
  return 0;
}

static int run_test(void)
{
  struct retro_game_info info;
  const dhry_result_t *result;
  const uint8_t *system_ram;
  size_t system_ram_size;
  unsigned max_frames = 300;
  unsigned frame;
  uint32_t jit_blocks_emitted;
  uint32_t jit_blocks_executed;
  uint32_t jit_generic_fallbacks;
  uint32_t jit_unsupported_opcodes;
  uint32_t jit_thumb_blocks;

  memset(&info, 0, sizeof(info));
  info.data = dhrystone_arm_gba_start;
  info.size = (size_t)(dhrystone_arm_gba_end - dhrystone_arm_gba_start);

  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);
  retro_init();

  if (!retro_load_game(&info))
  {
    ESP_LOGE(TAG, "retro_load_game failed");
    retro_deinit();
    return 1;
  }

  system_ram = (const uint8_t *)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
  system_ram_size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
  if (!system_ram || system_ram_size < sizeof(dhry_result_t))
  {
    ESP_LOGE(TAG, "system RAM mapping unavailable");
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

  printf("backend=%s frames=%u video_frames=%u magic=0x%08" PRIx32 " status=%" PRIu32
         " iterations=%" PRIu32 " int_glob=%" PRId32 " bool_glob=%" PRId32
         " ch1=%c ch2=%c arr1_8=%" PRId32 " arr2_8_7=%" PRId32
         " ptr=%" PRId32 " next=%" PRId32 "\n",
         GPSP_TEST_BACKEND, frame + 1, g_video_frames, result->magic,
         result->status, result->iterations, result->int_glob,
         result->bool_glob, (char)result->ch_1_glob, (char)result->ch_2_glob,
         result->arr_1_8, result->arr_2_8_7, result->ptr_int_comp,
         result->next_ptr_int_comp);

  jit_blocks_emitted = xtensa_jit_get_blocks_emitted();
  jit_blocks_executed = xtensa_jit_get_blocks_executed();
  jit_generic_fallbacks = xtensa_jit_get_generic_fallbacks();
  jit_unsupported_opcodes = xtensa_jit_get_unsupported_opcodes();
  jit_thumb_blocks = xtensa_jit_get_thumb_blocks();

  printf("jit blocks_emitted=%" PRIu32 " blocks_executed=%" PRIu32
         " generic_fallbacks=%" PRIu32 " unsupported=%" PRIu32
         " thumb_blocks=%" PRIu32 "\n",
         jit_blocks_emitted, jit_blocks_executed, jit_generic_fallbacks,
         jit_unsupported_opcodes, jit_thumb_blocks);

  retro_unload_game();
  retro_deinit();

  if (result->magic != DHRY_RESULT_MAGIC || result->status != DHRY_STATUS_PASS)
  {
    printf("result=FAIL backend=%s\n", GPSP_TEST_BACKEND);
    return 1;
  }

  if (strcmp(GPSP_TEST_BACKEND, "dynarec") == 0)
  {
    if (jit_blocks_executed == 0 || jit_generic_fallbacks != 0 ||
        jit_unsupported_opcodes != 0 || jit_thumb_blocks != 0)
    {
      printf("result=FAIL backend=%s jit_blocks=%" PRIu32
             " fallbacks=%" PRIu32 " unsupported=%" PRIu32
             " thumb=%" PRIu32 "\n",
             GPSP_TEST_BACKEND, jit_blocks_executed, jit_generic_fallbacks,
             jit_unsupported_opcodes, jit_thumb_blocks);
      return 1;
    }
  }

  printf("result=PASS backend=%s\n", GPSP_TEST_BACKEND);
  return 0;
}

void app_main(void)
{
  int rc = run_test();
  fflush(stdout);
  ESP_LOGI(TAG, "run_test rc=%d", rc);
}
