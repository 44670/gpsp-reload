#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt1151_protocol.h"
#include "rgb565_scale3x.h"

#define SOURCE_PITCH_PIXELS 256u
#define OUTPUT_PITCH_PIXELS 808u
#define GUARD_BYTES 64u

static uint64_t fnv1a64(const void *data, size_t size)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < size; i++)
  {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static void test_scaler(void)
{
  const size_t source_bytes =
      SOURCE_PITCH_PIXELS * ESP32S31_GBA_HEIGHT * sizeof(uint16_t);
  const size_t output_bytes =
      OUTPUT_PITCH_PIXELS * ESP32S31_LCD_HEIGHT * sizeof(uint16_t);
  uint8_t *source_allocation = malloc(source_bytes + GUARD_BYTES * 2u);
  uint8_t *output_allocation = malloc(output_bytes + GUARD_BYTES * 2u);
  assert(source_allocation != NULL && output_allocation != NULL);
  memset(source_allocation, 0xa5, source_bytes + GUARD_BYTES * 2u);
  memset(output_allocation, 0x5a, output_bytes + GUARD_BYTES * 2u);

  uint16_t *source = (uint16_t *)(source_allocation + GUARD_BYTES);
  uint16_t *output = (uint16_t *)(output_allocation + GUARD_BYTES);
  for (unsigned y = 0; y < ESP32S31_GBA_HEIGHT; y++)
  {
    uint16_t *row = source + y * SOURCE_PITCH_PIXELS;
    for (unsigned x = 0; x < ESP32S31_GBA_WIDTH; x++)
      row[x] = (uint16_t)(((x * 31u / 239u) << 11) |
                          ((y * 63u / 159u) << 5) | ((x ^ y) & 31u));
  }
  source[0] = 0xf800u;
  source[ESP32S31_GBA_WIDTH - 1u] = 0x07e0u;
  source[(ESP32S31_GBA_HEIGHT - 1u) * SOURCE_PITCH_PIXELS] = 0x001fu;
  source[(ESP32S31_GBA_HEIGHT - 1u) * SOURCE_PITCH_PIXELS +
         ESP32S31_GBA_WIDTH - 1u] = 0xffffu;

  assert(esp32s31_rgb565_clear_output(
      output, OUTPUT_PITCH_PIXELS * sizeof(uint16_t)));
  assert(esp32s31_rgb565_scale3x(
      output, OUTPUT_PITCH_PIXELS * sizeof(uint16_t), source,
      ESP32S31_GBA_WIDTH, ESP32S31_GBA_HEIGHT,
      SOURCE_PITCH_PIXELS * sizeof(uint16_t)));

  for (unsigned y = 0; y < ESP32S31_LCD_HEIGHT; y++)
  {
    const uint16_t *row = output + y * OUTPUT_PITCH_PIXELS;
    for (unsigned x = 0; x < ESP32S31_LCD_BAR_WIDTH; x++)
      assert(row[x] == 0u);
    for (unsigned x = ESP32S31_LCD_WIDTH - ESP32S31_LCD_BAR_WIDTH;
         x < ESP32S31_LCD_WIDTH; x++)
      assert(row[x] == 0u);
    for (unsigned x = ESP32S31_LCD_WIDTH;
         x < OUTPUT_PITCH_PIXELS; x++)
      assert(row[x] == 0x5a5au);
  }

  for (unsigned y = 0; y < ESP32S31_GBA_HEIGHT; y++)
  {
    for (unsigned x = 0; x < ESP32S31_GBA_WIDTH; x++)
    {
      const uint16_t expected = source[y * SOURCE_PITCH_PIXELS + x];
      for (unsigned duplicate_y = 0; duplicate_y < 3u; duplicate_y++)
      {
        const uint16_t *row = output +
            (y * 3u + duplicate_y) * OUTPUT_PITCH_PIXELS;
        for (unsigned duplicate_x = 0; duplicate_x < 3u; duplicate_x++)
          assert(row[ESP32S31_LCD_BAR_WIDTH + x * 3u + duplicate_x] ==
                 expected);
      }
    }
  }

  for (unsigned i = 0; i < GUARD_BYTES; i++)
  {
    assert(source_allocation[i] == 0xa5u);
    assert(source_allocation[GUARD_BYTES + source_bytes + i] == 0xa5u);
    assert(output_allocation[i] == 0x5au);
    assert(output_allocation[GUARD_BYTES + output_bytes + i] == 0x5au);
  }

  const uint64_t hash = fnv1a64(output, output_bytes);
  printf("scaler_hash=%016llx\n", (unsigned long long)hash);
  assert(hash == UINT64_C(0x927e24f1a62e068d));

  assert(!esp32s31_rgb565_scale3x(
      output, OUTPUT_PITCH_PIXELS * sizeof(uint16_t), source, 239,
      ESP32S31_GBA_HEIGHT, SOURCE_PITCH_PIXELS * sizeof(uint16_t)));
  assert(!esp32s31_rgb565_scale3x(
      output, ESP32S31_LCD_WIDTH * sizeof(uint16_t) - 1u, source,
      ESP32S31_GBA_WIDTH, ESP32S31_GBA_HEIGHT,
      SOURCE_PITCH_PIXELS * sizeof(uint16_t)));

  free(output_allocation);
  free(source_allocation);
}

static void test_fps_overlay(void)
{
  const size_t output_bytes =
      OUTPUT_PITCH_PIXELS * ESP32S31_LCD_HEIGHT * sizeof(uint16_t);
  uint8_t *allocation = malloc(output_bytes + GUARD_BYTES * 2u);
  assert(allocation != NULL);
  memset(allocation, 0xa5, output_bytes + GUARD_BYTES * 2u);

  uint16_t *output = (uint16_t *)(allocation + GUARD_BYTES);
  for (unsigned y = 0; y < ESP32S31_LCD_HEIGHT; y++)
  {
    uint16_t *row = output + y * OUTPUT_PITCH_PIXELS;
    for (unsigned x = 0; x < OUTPUT_PITCH_PIXELS; x++)
      row[x] = 0x1234u;
  }

  assert(esp32s31_rgb565_draw_fps(
      output, OUTPUT_PITCH_PIXELS * sizeof(uint16_t), 597u));

  unsigned white_pixels = 0;
  unsigned black_pixels = 0;
  for (unsigned y = 2; y < 20u; y++)
  {
    const uint16_t *row = output + y * OUTPUT_PITCH_PIXELS;
    for (unsigned x = ESP32S31_LCD_BAR_WIDTH + 2u;
         x < ESP32S31_LCD_BAR_WIDTH + 98u; x++)
    {
      assert(row[x] == 0u || row[x] == 0xffffu);
      white_pixels += row[x] == 0xffffu;
      black_pixels += row[x] == 0u;
    }
  }
  assert(white_pixels > 200u);
  assert(black_pixels > white_pixels);
  assert(output[0] == 0x1234u);
  assert(output[1u * OUTPUT_PITCH_PIXELS + ESP32S31_LCD_BAR_WIDTH + 2u] ==
         0x1234u);
  assert(output[20u * OUTPUT_PITCH_PIXELS + ESP32S31_LCD_BAR_WIDTH + 2u] ==
         0x1234u);
  assert(output[2u * OUTPUT_PITCH_PIXELS + ESP32S31_LCD_BAR_WIDTH + 1u] ==
         0x1234u);
  assert(output[2u * OUTPUT_PITCH_PIXELS + ESP32S31_LCD_BAR_WIDTH + 98u] ==
         0x1234u);

  for (unsigned i = 0; i < GUARD_BYTES; i++)
  {
    assert(allocation[i] == 0xa5u);
    assert(allocation[GUARD_BYTES + output_bytes + i] == 0xa5u);
  }

  assert(!esp32s31_rgb565_draw_fps(
      output, ESP32S31_LCD_WIDTH * sizeof(uint16_t) - 1u, 600u));
  free(allocation);
}

static void test_fused_fps_overlay(void)
{
  const size_t source_bytes =
      SOURCE_PITCH_PIXELS * ESP32S31_GBA_HEIGHT * sizeof(uint16_t);
  const size_t output_bytes =
      OUTPUT_PITCH_PIXELS * ESP32S31_LCD_HEIGHT * sizeof(uint16_t);
  uint8_t *source_allocation = malloc(source_bytes + GUARD_BYTES * 2u);
  uint8_t *output_allocation = malloc(output_bytes + GUARD_BYTES * 2u);
  assert(source_allocation != NULL && output_allocation != NULL);
  memset(source_allocation, 0xa5, source_bytes + GUARD_BYTES * 2u);
  memset(output_allocation, 0x5a, output_bytes + GUARD_BYTES * 2u);
  uint16_t *source = (uint16_t *)(source_allocation + GUARD_BYTES);
  uint16_t *output = (uint16_t *)(output_allocation + GUARD_BYTES);
  for (unsigned y = 0; y < ESP32S31_GBA_HEIGHT; y++)
  {
    for (unsigned x = 0; x < SOURCE_PITCH_PIXELS; x++)
      source[y * SOURCE_PITCH_PIXELS + x] = 0x1234u;
  }

  esp32s31_rgb565_fps_osd_t osd;
  assert(!esp32s31_rgb565_prepare_fps_osd(NULL, 597u));
  assert(esp32s31_rgb565_prepare_fps_osd(&osd, 597u));
  assert(esp32s31_rgb565_clear_output(
      output, OUTPUT_PITCH_PIXELS * sizeof(uint16_t)));

  /* Exercise the same global-row contract used by independent bounce strips. */
  assert(esp32s31_rgb565_scale3x_rows_osd(
      output, OUTPUT_PITCH_PIXELS * sizeof(uint16_t), source,
      0u, 4u, SOURCE_PITCH_PIXELS * sizeof(uint16_t), &osd));
  assert(esp32s31_rgb565_scale3x_rows_osd(
      output + 4u * ESP32S31_SCALE_FACTOR * OUTPUT_PITCH_PIXELS,
      OUTPUT_PITCH_PIXELS * sizeof(uint16_t),
      source + 4u * SOURCE_PITCH_PIXELS,
      4u, ESP32S31_GBA_HEIGHT - 4u,
      SOURCE_PITCH_PIXELS * sizeof(uint16_t), &osd));

  unsigned white_pixels = 0;
  unsigned black_pixels = 0;
  for (unsigned y = 0; y < ESP32S31_GBA_FPS_OSD_HEIGHT; y++)
  {
    for (unsigned x = 0; x < ESP32S31_GBA_FPS_OSD_WIDTH; x++)
    {
      const uint16_t expected =
          (osd.white_rows[y][x / 32u] &
           (UINT32_C(1) << (x % 32u))) != 0u
              ? UINT16_C(0xffff)
              : UINT16_C(0x0000);
      white_pixels += expected == 0xffffu;
      black_pixels += expected == 0u;
      for (unsigned duplicate_y = 0; duplicate_y < 3u; duplicate_y++)
      {
        const uint16_t *row = output +
            (y * 3u + duplicate_y) * OUTPUT_PITCH_PIXELS;
        for (unsigned duplicate_x = 0; duplicate_x < 3u; duplicate_x++)
          assert(row[ESP32S31_LCD_BAR_WIDTH + x * 3u + duplicate_x] ==
                 expected);
      }
    }
    assert(output[(y * 3u) * OUTPUT_PITCH_PIXELS +
                  ESP32S31_LCD_BAR_WIDTH +
                  ESP32S31_GBA_FPS_OSD_WIDTH * 3u] == 0x1234u);
  }
  assert(white_pixels > 40u);
  assert(black_pixels > white_pixels);
  assert(output[(ESP32S31_GBA_FPS_OSD_HEIGHT * 3u) *
                    OUTPUT_PITCH_PIXELS +
                ESP32S31_LCD_BAR_WIDTH] == 0x1234u);

  /* Fused composition must leave the emulator-owned source untouched. */
  for (unsigned y = 0; y < ESP32S31_GBA_HEIGHT; y++)
  {
    for (unsigned x = 0; x < SOURCE_PITCH_PIXELS; x++)
      assert(source[y * SOURCE_PITCH_PIXELS + x] == 0x1234u);
  }

  for (unsigned i = 0; i < GUARD_BYTES; i++)
  {
    assert(source_allocation[i] == 0xa5u);
    assert(source_allocation[GUARD_BYTES + source_bytes + i] == 0xa5u);
    assert(output_allocation[i] == 0x5au);
    assert(output_allocation[GUARD_BYTES + output_bytes + i] == 0x5au);
  }
  assert(!esp32s31_rgb565_scale3x_rows_osd(
      output, OUTPUT_PITCH_PIXELS * sizeof(uint16_t), source,
      ESP32S31_GBA_HEIGHT - 1u, 2u,
      SOURCE_PITCH_PIXELS * sizeof(uint16_t), &osd));

  free(output_allocation);
  free(source_allocation);
}

static void finish_gt1151_checksum(uint8_t *report, size_t size)
{
  uint8_t sum = 0;
  for (size_t i = 0; i + 1u < size; i++)
    sum = (uint8_t)(sum + report[i]);
  report[size - 1u] = (uint8_t)(0u - sum);
}

static void test_gt1151_decoder(void)
{
  uint8_t report[ESP32S31_GT1151_REPORT_SIZE(2)] = {0};
  report[0] = 0x82u;
  report[1] = 0x03u;
  report[2] = 0x34u;
  report[3] = 0x12u;
  report[4] = 0x78u;
  report[5] = 0x01u;
  report[6] = 0x55u;
  report[7] = 0x00u;
  report[9] = 0x07u;
  report[10] = 0x20u;
  report[11] = 0x03u;
  report[12] = 0xdfu;
  report[13] = 0x01u;
  report[14] = 0xaau;
  report[15] = 0x00u;
  finish_gt1151_checksum(report, sizeof(report));

  esp32s31_touch_point_t points[2] = {0};
  size_t count = 0;
  assert(esp32s31_gt1151_decode_report(
             report, sizeof(report), points, 2, &count) ==
         ESP32S31_GT1151_DECODE_OK);
  assert(count == 2u);
  assert(points[0].track_id == 3u && points[0].x == 0x1234u &&
         points[0].y == 0x0178u && points[0].strength == 0x0055u);
  assert(points[1].track_id == 7u && points[1].x == 0x0320u &&
         points[1].y == 0x01dfu && points[1].strength == 0x00aau);

  report[sizeof(report) - 1u]++;
  assert(esp32s31_gt1151_decode_report(
             report, sizeof(report), points, 2, &count) ==
         ESP32S31_GT1151_DECODE_INVALID_CHECKSUM);
}

int main(void)
{
  test_scaler();
  test_fps_overlay();
  test_fused_fps_overlay();
  test_gt1151_decoder();
  puts("result=PASS command=esp32s31_board_driver_host_test");
  return 0;
}
