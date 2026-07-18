#include "korvo1_scaler.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "driver/ppa.h"
#include "esp_async_memcpy.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rgb565_scale3x.h"
#include "soc/soc_caps.h"

#define SCALER_MODE_AUTO 0
#define SCALER_MODE_CPU 1
#define SCALER_MODE_SRAM_GDMA 2
#define SCALER_MODE_PPA 3

#ifndef ESP32S31_SCALER_MODE
#define ESP32S31_SCALER_MODE SCALER_MODE_AUTO
#endif

#define SRAM_SOURCE_ROWS 8u
#define SRAM_OUTPUT_ROWS (SRAM_SOURCE_ROWS * ESP32S31_SCALE_FACTOR)
#define OUTPUT_PITCH_BYTES (ESP32S31_LCD_WIDTH * sizeof(uint16_t))
#define SRAM_WORK_BYTES (SRAM_OUTPUT_ROWS * OUTPUT_PITCH_BYTES)
#define SRAM_SLOT_COUNT 2u
#define SCALE_BENCH_ITERATIONS 3u
#define SCALE_TIMEOUT_MS 100u
#define FRAME_BYTES \
  (ESP32S31_LCD_WIDTH * ESP32S31_LCD_HEIGHT * sizeof(uint16_t))

static const char *TAG = "korvo1_scaler";

typedef struct {
  uint8_t *buffer;
  SemaphoreHandle_t done;
  bool pending;
} sram_slot_t;

typedef struct {
  async_memcpy_handle_t memcpy;
  ppa_client_handle_t ppa;
  sram_slot_t slots[SRAM_SLOT_COUNT];
  esp32s31_scaler_stats_t stats;
  uint8_t active_mode;
  bool gdma_ready;
  bool ppa_ready;
  bool benchmark_pending;
} scaler_state_t;

static scaler_state_t s_scaler;

static const char *mode_name(unsigned mode)
{
  switch (mode)
  {
    case SCALER_MODE_CPU:
      return "cpu";
    case SCALER_MODE_SRAM_GDMA:
      return "sram_gdma";
    case SCALER_MODE_PPA:
      return "ppa";
    default:
      return "auto";
  }
}

static bool IRAM_ATTR sram_copy_done(
    async_memcpy_handle_t handle, async_memcpy_event_t *event, void *arg)
{
  (void)handle;
  (void)event;
  sram_slot_t *slot = (sram_slot_t *)arg;
  BaseType_t task_woken = pdFALSE;
  xSemaphoreGiveFromISR(slot->done, &task_woken);
  return task_woken == pdTRUE;
}

static bool wait_sram_slot(sram_slot_t *slot)
{
  if (!slot->pending)
    return true;
  if (xSemaphoreTake(slot->done, pdMS_TO_TICKS(SCALE_TIMEOUT_MS)) != pdTRUE)
  {
    ESP_LOGE(TAG, "SRAM/GDMA copy timeout");
    return false;
  }
  slot->pending = false;
  return true;
}

static void deinit_sram_gdma(void)
{
  for (unsigned i = 0; i < SRAM_SLOT_COUNT; i++)
    (void)wait_sram_slot(&s_scaler.slots[i]);
  if (s_scaler.memcpy != NULL)
  {
    (void)esp_async_memcpy_uninstall(s_scaler.memcpy);
    s_scaler.memcpy = NULL;
  }
  for (unsigned i = 0; i < SRAM_SLOT_COUNT; i++)
  {
    if (s_scaler.slots[i].done != NULL)
      vSemaphoreDelete(s_scaler.slots[i].done);
    free(s_scaler.slots[i].buffer);
    memset(&s_scaler.slots[i], 0, sizeof(s_scaler.slots[i]));
  }
  s_scaler.gdma_ready = false;
}

static void deinit_ppa(void)
{
  if (s_scaler.ppa != NULL)
  {
    (void)ppa_unregister_client(s_scaler.ppa);
    s_scaler.ppa = NULL;
  }
  s_scaler.ppa_ready = false;
}

static bool init_sram_gdma(void)
{
  for (unsigned i = 0; i < SRAM_SLOT_COUNT; i++)
  {
    s_scaler.slots[i].buffer = heap_caps_aligned_calloc(
        64, 1, SRAM_WORK_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_scaler.slots[i].done = xSemaphoreCreateBinary();
    if (s_scaler.slots[i].buffer == NULL ||
        s_scaler.slots[i].done == NULL)
    {
      ESP_LOGE(TAG, "cannot allocate %u-byte SRAM work buffer %u",
               (unsigned)SRAM_WORK_BYTES, i);
      deinit_sram_gdma();
      return false;
    }
  }

  const async_memcpy_config_t config = {
      .backlog = SRAM_SLOT_COUNT,
      .weight = 0,
      .dma_burst_size = 32,
      .flags = 0,
  };
#if SOC_HAS(AXI_GDMA)
  const esp_err_t error =
      esp_async_memcpy_install_gdma_axi(&config, &s_scaler.memcpy);
#else
  const esp_err_t error =
      esp_async_memcpy_install(&config, &s_scaler.memcpy);
#endif
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "install async GDMA memcpy failed: %s",
             esp_err_to_name(error));
    deinit_sram_gdma();
    return false;
  }
  ESP_LOGI(TAG, "SRAM/GDMA scaler: %u x %u-byte internal work buffers",
           SRAM_SLOT_COUNT, (unsigned)SRAM_WORK_BYTES);
  return true;
}

static bool init_ppa(void)
{
  const ppa_client_config_t config = {
      .oper_type = PPA_OPERATION_SRM,
      .max_pending_trans_num = 1,
      .data_burst_length = PPA_DATA_BURST_LENGTH_128,
  };
  const esp_err_t error = ppa_register_client(&config, &s_scaler.ppa);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "register PPA SRM client failed: %s",
             esp_err_to_name(error));
    return false;
  }
  ESP_LOGI(TAG, "PPA RGB565 scaler ready");
  return true;
}

bool esp32s31_korvo1_scaler_init(void)
{
  memset(&s_scaler, 0, sizeof(s_scaler));
  s_scaler.active_mode = ESP32S31_SCALER_MODE;
  s_scaler.benchmark_pending = ESP32S31_SCALER_MODE == SCALER_MODE_AUTO;

  if (ESP32S31_SCALER_MODE == SCALER_MODE_AUTO ||
      ESP32S31_SCALER_MODE == SCALER_MODE_SRAM_GDMA)
    s_scaler.gdma_ready = init_sram_gdma();
  if (ESP32S31_SCALER_MODE == SCALER_MODE_AUTO ||
      ESP32S31_SCALER_MODE == SCALER_MODE_PPA)
    s_scaler.ppa_ready = init_ppa();

  if (s_scaler.active_mode == SCALER_MODE_SRAM_GDMA &&
      !s_scaler.gdma_ready)
    s_scaler.active_mode = SCALER_MODE_CPU;
  if (s_scaler.active_mode == SCALER_MODE_PPA && !s_scaler.ppa_ready)
    s_scaler.active_mode = SCALER_MODE_CPU;

  if (s_scaler.benchmark_pending)
    s_scaler.active_mode = SCALER_MODE_CPU;
  ESP_LOGI(TAG, "requested=%s active=%s internal_free=%u",
           mode_name(ESP32S31_SCALER_MODE), mode_name(s_scaler.active_mode),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                             MALLOC_CAP_8BIT));
  return s_scaler.active_mode == ESP32S31_SCALER_MODE ||
         ESP32S31_SCALER_MODE == SCALER_MODE_AUTO;
}

static bool scale_cpu(void *output, size_t output_pitch,
                      const void *input, unsigned width,
                      unsigned height, size_t input_pitch)
{
  const int64_t start = esp_timer_get_time();
  const bool result = esp32s31_rgb565_scale3x(
      output, output_pitch, input, width, height, input_pitch);
  s_scaler.stats.prepare_us = (uint32_t)(esp_timer_get_time() - start);
  s_scaler.stats.transfer_us = 0;
  return result;
}

static bool scale_sram_gdma(void *output, size_t output_pitch,
                            const void *input, unsigned width,
                            unsigned height, size_t input_pitch)
{
  if (!s_scaler.gdma_ready || output == NULL || input == NULL ||
      width != ESP32S31_GBA_WIDTH || height != ESP32S31_GBA_HEIGHT ||
      output_pitch != OUTPUT_PITCH_BYTES ||
      input_pitch < ESP32S31_GBA_WIDTH * sizeof(uint16_t))
    return false;

  const int64_t total_start = esp_timer_get_time();
  uint32_t prepare_us = 0;
  bool success = true;
  const uint8_t *source = (const uint8_t *)input;
  uint8_t *destination = (uint8_t *)output;

  for (unsigned source_y = 0, chunk = 0;
       source_y < ESP32S31_GBA_HEIGHT;
       source_y += SRAM_SOURCE_ROWS, chunk++)
  {
    sram_slot_t *slot = &s_scaler.slots[chunk % SRAM_SLOT_COUNT];
    if (!wait_sram_slot(slot))
    {
      success = false;
      break;
    }

    const int64_t prepare_start = esp_timer_get_time();
    if (!esp32s31_rgb565_scale3x_rows(
            slot->buffer, OUTPUT_PITCH_BYTES,
            source + (size_t)source_y * input_pitch,
            SRAM_SOURCE_ROWS, input_pitch))
    {
      success = false;
      break;
    }
    prepare_us += (uint32_t)(esp_timer_get_time() - prepare_start);

    const esp_err_t error = esp_async_memcpy(
        s_scaler.memcpy,
        destination + (size_t)source_y * ESP32S31_SCALE_FACTOR *
                          OUTPUT_PITCH_BYTES,
        slot->buffer, SRAM_WORK_BYTES, sram_copy_done, slot);
    if (error != ESP_OK)
    {
      ESP_LOGE(TAG, "queue SRAM/GDMA copy failed: %s",
               esp_err_to_name(error));
      success = false;
      break;
    }
    slot->pending = true;
  }

  for (unsigned i = 0; i < SRAM_SLOT_COUNT; i++)
    success = wait_sram_slot(&s_scaler.slots[i]) && success;

  const uint32_t total_us =
      (uint32_t)(esp_timer_get_time() - total_start);
  s_scaler.stats.prepare_us = prepare_us;
  s_scaler.stats.transfer_us =
      total_us > prepare_us ? total_us - prepare_us : 0u;
  return success;
}

static bool scale_ppa(void *output, size_t output_pitch,
                      const void *input, unsigned width,
                      unsigned height, size_t input_pitch)
{
  if (!s_scaler.ppa_ready || output == NULL || input == NULL ||
      width != ESP32S31_GBA_WIDTH || height != ESP32S31_GBA_HEIGHT ||
      output_pitch != OUTPUT_PITCH_BYTES ||
      input_pitch != ESP32S31_GBA_WIDTH * sizeof(uint16_t))
    return false;

  const ppa_srm_oper_config_t operation = {
      .in = {
          .buffer = input,
          .pic_w = ESP32S31_GBA_WIDTH,
          .pic_h = ESP32S31_GBA_HEIGHT,
          .block_w = ESP32S31_GBA_WIDTH,
          .block_h = ESP32S31_GBA_HEIGHT,
          .block_offset_x = 0,
          .block_offset_y = 0,
          .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
      },
      .out = {
          .buffer = output,
          .buffer_size = FRAME_BYTES,
          .pic_w = ESP32S31_LCD_WIDTH,
          .pic_h = ESP32S31_LCD_HEIGHT,
          .block_offset_x = ESP32S31_LCD_BAR_WIDTH,
          .block_offset_y = 0,
          .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
      },
      .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
      .scale_x = 3.0f,
      .scale_y = 3.0f,
      .mirror_x = false,
      .mirror_y = false,
      .rgb_swap = false,
      .byte_swap = false,
      .mode = PPA_TRANS_MODE_BLOCKING,
  };

  const int64_t start = esp_timer_get_time();
  const esp_err_t error =
      ppa_do_scale_rotate_mirror(s_scaler.ppa, &operation);
  const uint32_t elapsed = (uint32_t)(esp_timer_get_time() - start);
  s_scaler.stats.prepare_us = 0;
  s_scaler.stats.transfer_us = elapsed;
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "PPA scale failed: %s", esp_err_to_name(error));
    return false;
  }
  return true;
}

static bool scale_mode(unsigned mode, void *output, size_t output_pitch,
                       const void *input, unsigned width,
                       unsigned height, size_t input_pitch)
{
  switch (mode)
  {
    case SCALER_MODE_SRAM_GDMA:
      return scale_sram_gdma(output, output_pitch, input, width, height,
                             input_pitch);
    case SCALER_MODE_PPA:
      return scale_ppa(output, output_pitch, input, width, height,
                       input_pitch);
    default:
      return scale_cpu(output, output_pitch, input, width, height,
                       input_pitch);
  }
}

static uint32_t verify_output(const void *output, const void *input,
                              size_t input_pitch, uint32_t *hash_out)
{
  const uint16_t *frame = (const uint16_t *)output;
  const uint8_t *source_bytes = (const uint8_t *)input;
  uint32_t mismatches = 0;
  uint32_t hash = UINT32_C(2166136261);

  for (unsigned y = 0; y < ESP32S31_LCD_HEIGHT; y++)
  {
    const uint16_t *source = (const uint16_t *)(
        source_bytes + (size_t)(y / ESP32S31_SCALE_FACTOR) * input_pitch);
    for (unsigned x = 0; x < ESP32S31_LCD_WIDTH; x++)
    {
      uint16_t expected = 0;
      if (x >= ESP32S31_LCD_BAR_WIDTH &&
          x < ESP32S31_LCD_WIDTH - ESP32S31_LCD_BAR_WIDTH)
        expected = source[(x - ESP32S31_LCD_BAR_WIDTH) /
                          ESP32S31_SCALE_FACTOR];
      const uint16_t actual = frame[(size_t)y * ESP32S31_LCD_WIDTH + x];
      if (actual != expected)
        mismatches++;
      hash ^= actual & 0xffu;
      hash *= UINT32_C(16777619);
      hash ^= actual >> 8;
      hash *= UINT32_C(16777619);
    }
  }
  *hash_out = hash;
  return mismatches;
}

static uint32_t benchmark_mode(unsigned mode, void *output,
                               size_t output_pitch, const void *input,
                               unsigned width, unsigned height,
                               size_t input_pitch)
{
  if ((mode == SCALER_MODE_SRAM_GDMA && !s_scaler.gdma_ready) ||
      (mode == SCALER_MODE_PPA && !s_scaler.ppa_ready))
    return UINT32_MAX;

  if (!scale_mode(mode, output, output_pitch, input, width, height,
                  input_pitch))
    return UINT32_MAX;

  uint32_t hash = 0;
  const uint32_t mismatches =
      verify_output(output, input, input_pitch, &hash);
  uint64_t total_us = 0;
  uint32_t minimum_us = UINT32_MAX;
  for (unsigned i = 0; i < SCALE_BENCH_ITERATIONS; i++)
  {
    const int64_t start = esp_timer_get_time();
    if (!scale_mode(mode, output, output_pitch, input, width, height,
                    input_pitch))
      return UINT32_MAX;
    const uint32_t elapsed = (uint32_t)(esp_timer_get_time() - start);
    total_us += elapsed;
    if (elapsed < minimum_us)
      minimum_us = elapsed;
  }
  const uint32_t average_us =
      (uint32_t)((total_us + SCALE_BENCH_ITERATIONS / 2u) /
                 SCALE_BENCH_ITERATIONS);
  printf("result=%s command=scaler_bench mode=%s iterations=%u "
         "avg_us=%" PRIu32 " min_us=%" PRIu32
         " mismatches=%" PRIu32 " hash=0x%08" PRIx32 "\n",
         mismatches == 0u ? "PASS" : "FAIL", mode_name(mode),
         SCALE_BENCH_ITERATIONS, average_us, minimum_us, mismatches, hash);
  return mismatches == 0u ? average_us : UINT32_MAX;
}

static bool benchmark_and_select(void *output, size_t output_pitch,
                                 const void *input, unsigned width,
                                 unsigned height, size_t input_pitch)
{
  uint32_t best_time = UINT32_MAX;
  unsigned best_mode = SCALER_MODE_CPU;
  for (unsigned mode = SCALER_MODE_CPU; mode <= SCALER_MODE_PPA; mode++)
  {
    const uint32_t time = benchmark_mode(
        mode, output, output_pitch, input, width, height, input_pitch);
    if (time < best_time)
    {
      best_time = time;
      best_mode = mode;
    }
  }
  s_scaler.active_mode = best_mode;
  s_scaler.benchmark_pending = false;
  if (best_mode != SCALER_MODE_SRAM_GDMA)
    deinit_sram_gdma();
  if (best_mode != SCALER_MODE_PPA)
    deinit_ppa();
  printf("result=PASS command=scaler_select mode=%s avg_us=%" PRIu32
         " internal_free=%u\n", mode_name(best_mode), best_time,
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                           MALLOC_CAP_8BIT));
  const bool result = scale_mode(best_mode, output, output_pitch, input,
                                 width, height, input_pitch);
  s_scaler.stats.benchmarked_call = true;
  return result;
}

bool esp32s31_korvo1_scaler_scale(void *output, size_t output_pitch,
                                  const void *input, unsigned width,
                                  unsigned height, size_t input_pitch)
{
  if (s_scaler.benchmark_pending)
    return benchmark_and_select(output, output_pitch, input, width, height,
                                input_pitch);
  s_scaler.stats.benchmarked_call = false;
  return scale_mode(s_scaler.active_mode, output, output_pitch,
                    input, width, height, input_pitch);
}

const char *esp32s31_korvo1_scaler_name(void)
{
  return mode_name(s_scaler.active_mode);
}

void esp32s31_korvo1_scaler_get_stats(esp32s31_scaler_stats_t *out)
{
  if (out != NULL)
    *out = s_scaler.stats;
}
