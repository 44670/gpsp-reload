#include "gpsp_profile.h"

#if GPSP_ESP32S31_PROFILE

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "riscv/csr.h"
#include "sdkconfig.h"

#define GPSP_PROFILE_PC_SAMPLES 4096u
#define GPSP_PROFILE_PC_PERIOD_US 1000u
#define GPSP_PROFILE_PC_LINE_COUNT 16u

gpsp_profile_counter_t
    g_gpsp_profile_counters[GPSP_PROFILE_COUNTER_COUNT];

static const char *const s_counter_names[GPSP_PROFILE_COUNTER_COUNT] = {
    [GPSP_PROFILE_RETRO_RUN] = "retro_run",
    [GPSP_PROFILE_INPUT] = "input",
    [GPSP_PROFILE_CPU_BACKEND] = "cpu_backend",
    [GPSP_PROFILE_AUDIO_RUN] = "audio_run",
    [GPSP_PROFILE_AUDIO_DRAIN] = "audio_drain",
    [GPSP_PROFILE_VIDEO_RUN] = "video_run",
    [GPSP_PROFILE_TOUCH_POLL] = "touch_poll",
    [GPSP_PROFILE_FRAME_HASH] = "frame_hash",
    [GPSP_PROFILE_LCD_WAIT] = "lcd_wait",
    [GPSP_PROFILE_LCD_SCALE] = "lcd_scale",
    [GPSP_PROFILE_LCD_OVERLAY] = "lcd_overlay",
    [GPSP_PROFILE_LCD_SUBMIT] = "lcd_submit",
    [GPSP_PROFILE_LCD_SNAPSHOT] = "lcd_snapshot",
    [GPSP_PROFILE_UPDATE_GBA] = "update_gba",
    [GPSP_PROFILE_UPDATE_TIMERS] = "update_timers",
    [GPSP_PROFILE_UPDATE_SERIAL] = "update_serial",
    [GPSP_PROFILE_UPDATE_SCANLINE] = "update_scanline",
    [GPSP_PROFILE_DMA] = "dma",
    [GPSP_PROFILE_GBC_SOUND] = "gbc_sound",
    [GPSP_PROFILE_GAMEPAK_PAGE] = "gamepak_page",
    [GPSP_PROFILE_FRAME_DELAY] = "frame_delay",
};

static esp_timer_handle_t s_pc_timer;
static volatile uint32_t s_pc_sample_count;
/* Keep profiling storage out of the 500 KB HP SRAM being benchmarked. */
static RTC_NOINIT_ATTR uint32_t s_pc_samples[GPSP_PROFILE_PC_SAMPLES];
static bool s_pc_dumped;

static void IRAM_ATTR profile_pc_timer_cb(void *arg)
{
  (void)arg;
  const uint32_t index = s_pc_sample_count;
  if (index >= GPSP_PROFILE_PC_SAMPLES)
    return;

  /* During an interrupt, mepc is the interrupted instruction address. */
  s_pc_samples[index] = RV_READ_CSR(mepc);
  s_pc_sample_count = index + 1u;
}

static uint64_t subtract_saturating(uint64_t total, uint64_t part)
{
  return total > part ? total - part : 0u;
}

static void print_part(const char *name, uint64_t cycles, uint32_t calls,
                       uint32_t frames, uint64_t retro_cycles,
                       bool derived)
{
  const uint64_t denominator =
      (uint64_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * frames;
  const uint64_t avg_us_x100 =
      denominator != 0u ? (cycles * 100u + denominator / 2u) / denominator
                        : 0u;
  const uint64_t total_us =
      (cycles + CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / 2u) /
      CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  const uint64_t pct_x10 =
      retro_cycles != 0u ? (cycles * 1000u + retro_cycles / 2u) /
                               retro_cycles
                         : 0u;

  printf("result=PASS command=gpsp_profile_part name=%s derived=%u "
         "calls=%" PRIu32 " total_cycles=%" PRIu64
         " total_us=%" PRIu64 " avg_us_x100=%" PRIu64
         " pct_x10=%" PRIu64 "\n",
         name, (unsigned)derived, calls, cycles, total_us, avg_us_x100,
         pct_x10);
}

void gpsp_profile_print_window(uint32_t frames)
{
  gpsp_profile_counter_t snapshot[GPSP_PROFILE_COUNTER_COUNT];
  memcpy(snapshot, g_gpsp_profile_counters, sizeof(snapshot));
  memset(g_gpsp_profile_counters, 0, sizeof(g_gpsp_profile_counters));

  const uint64_t retro = snapshot[GPSP_PROFILE_RETRO_RUN].cycles;
  printf("result=PASS command=gpsp_profile backend=interp frames=%" PRIu32
         " cpu_mhz=%u retro_cycles=%" PRIu64 "\n",
         frames, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, retro);

  for (unsigned i = 0; i < GPSP_PROFILE_COUNTER_COUNT; i++)
    print_part(s_counter_names[i], snapshot[i].cycles, snapshot[i].calls,
               frames, retro, false);

  uint64_t accounted = snapshot[GPSP_PROFILE_INPUT].cycles +
                       snapshot[GPSP_PROFILE_CPU_BACKEND].cycles +
                       snapshot[GPSP_PROFILE_AUDIO_RUN].cycles +
                       snapshot[GPSP_PROFILE_VIDEO_RUN].cycles;
  print_part("retro_misc", subtract_saturating(retro, accounted), frames,
             frames, retro, true);

  const uint64_t interpreter = subtract_saturating(
      snapshot[GPSP_PROFILE_CPU_BACKEND].cycles,
      snapshot[GPSP_PROFILE_UPDATE_GBA].cycles);
  print_part("interpreter_dispatch", interpreter, frames, frames, retro,
             true);

  accounted = snapshot[GPSP_PROFILE_UPDATE_TIMERS].cycles +
              snapshot[GPSP_PROFILE_UPDATE_SERIAL].cycles +
              snapshot[GPSP_PROFILE_UPDATE_SCANLINE].cycles;
  print_part("update_misc",
             subtract_saturating(snapshot[GPSP_PROFILE_UPDATE_GBA].cycles,
                                 accounted),
             snapshot[GPSP_PROFILE_UPDATE_GBA].calls, frames, retro, true);

  accounted = snapshot[GPSP_PROFILE_FRAME_HASH].cycles +
              snapshot[GPSP_PROFILE_LCD_WAIT].cycles +
              snapshot[GPSP_PROFILE_LCD_SCALE].cycles +
              snapshot[GPSP_PROFILE_LCD_OVERLAY].cycles +
              snapshot[GPSP_PROFILE_LCD_SUBMIT].cycles +
              snapshot[GPSP_PROFILE_LCD_SNAPSHOT].cycles;
  print_part("video_misc",
             subtract_saturating(snapshot[GPSP_PROFILE_VIDEO_RUN].cycles,
                                 accounted),
             frames, frames, retro, true);
  fflush(stdout);
}

void gpsp_profile_reset(void)
{
  memset(g_gpsp_profile_counters, 0, sizeof(g_gpsp_profile_counters));
}

bool gpsp_profile_sampler_start(void)
{
  if (s_pc_timer != NULL)
    return true;

  const esp_timer_create_args_t args = {
      .callback = profile_pc_timer_cb,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_ISR,
      .name = "gpsp_pc_profile",
      .skip_unhandled_events = true,
  };
  esp_err_t error = esp_timer_create(&args, &s_pc_timer);
  if (error == ESP_OK)
    error = esp_timer_start_periodic(s_pc_timer, GPSP_PROFILE_PC_PERIOD_US);
  if (error != ESP_OK)
  {
    printf("result=FAIL command=gpsp_profile_pc_start error=%s\n",
           esp_err_to_name(error));
    return false;
  }

  printf("result=PASS command=gpsp_profile_pc_start period_us=%u "
         "target_samples=%u\n",
         GPSP_PROFILE_PC_PERIOD_US, GPSP_PROFILE_PC_SAMPLES);
  return true;
}

bool gpsp_profile_sampler_dump_if_ready(void)
{
  if (s_pc_dumped || s_pc_timer == NULL ||
      s_pc_sample_count < GPSP_PROFILE_PC_SAMPLES)
    return false;

  (void)esp_timer_stop(s_pc_timer);
  s_pc_dumped = true;

  for (uint32_t base = 0; base < GPSP_PROFILE_PC_SAMPLES;
       base += GPSP_PROFILE_PC_LINE_COUNT)
  {
    printf("result=PASS command=gpsp_profile_pc index=%" PRIu32 " pcs=",
           base);
    const uint32_t end = base + GPSP_PROFILE_PC_LINE_COUNT;
    for (uint32_t i = base; i < end; i++)
      printf(i == base ? "%08" PRIx32 : ",%08" PRIx32, s_pc_samples[i]);
    putchar('\n');
  }
  printf("result=PASS command=gpsp_profile_pc_done samples=%u\n",
         GPSP_PROFILE_PC_SAMPLES);
  fflush(stdout);
  return true;
}

#endif
