#ifndef GPSP_ESP32S31_PROFILE_H
#define GPSP_ESP32S31_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  GPSP_PROFILE_RETRO_RUN = 0,
  GPSP_PROFILE_INPUT,
  GPSP_PROFILE_CPU_BACKEND,
  GPSP_PROFILE_AUDIO_RUN,
  GPSP_PROFILE_AUDIO_DRAIN,
  GPSP_PROFILE_VIDEO_RUN,
  GPSP_PROFILE_TOUCH_POLL,
  GPSP_PROFILE_FRAME_HASH,
  GPSP_PROFILE_LCD_WAIT,
  GPSP_PROFILE_LCD_SCALE,
  GPSP_PROFILE_LCD_OVERLAY,
  GPSP_PROFILE_LCD_SUBMIT,
  GPSP_PROFILE_LCD_SNAPSHOT,
  GPSP_PROFILE_UPDATE_GBA,
  GPSP_PROFILE_UPDATE_TIMERS,
  GPSP_PROFILE_UPDATE_SERIAL,
  GPSP_PROFILE_UPDATE_SCANLINE,
  GPSP_PROFILE_DMA,
  GPSP_PROFILE_GBC_SOUND,
  GPSP_PROFILE_GAMEPAK_PAGE,
  GPSP_PROFILE_FRAME_DELAY,
  GPSP_PROFILE_COUNTER_COUNT,
} gpsp_profile_counter_id_t;

#if GPSP_ESP32S31_PROFILE

#include "esp_cpu.h"

typedef struct {
  uint64_t cycles;
  uint32_t calls;
} gpsp_profile_counter_t;

extern gpsp_profile_counter_t
    g_gpsp_profile_counters[GPSP_PROFILE_COUNTER_COUNT];

static inline uint32_t gpsp_profile_begin(void)
{
  return (uint32_t)esp_cpu_get_cycle_count();
}

static inline void gpsp_profile_end(gpsp_profile_counter_id_t counter,
                                    uint32_t start)
{
  const uint32_t elapsed =
      (uint32_t)esp_cpu_get_cycle_count() - start;
  g_gpsp_profile_counters[counter].cycles += elapsed;
  g_gpsp_profile_counters[counter].calls++;
}

void gpsp_profile_print_window(uint32_t frames);
void gpsp_profile_reset(void);
bool gpsp_profile_sampler_start(void);
bool gpsp_profile_sampler_dump_if_ready(void);

#else

static inline uint32_t gpsp_profile_begin(void)
{
  return 0u;
}

static inline void gpsp_profile_end(gpsp_profile_counter_id_t counter,
                                    uint32_t start)
{
  (void)counter;
  (void)start;
}

#endif

#endif
