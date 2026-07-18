#include "gt1151_protocol.h"

esp32s31_gt1151_decode_result_t esp32s31_gt1151_decode_report(
    const uint8_t *report, size_t report_size,
    esp32s31_touch_point_t *points, size_t point_capacity,
    size_t *point_count)
{
  if (report == NULL || point_count == NULL ||
      (point_capacity != 0u && points == NULL))
    return ESP32S31_GT1151_DECODE_INVALID_ARGUMENT;

  const size_t reported_points = report[0] & 0x0fu;
  if (reported_points == 0u || reported_points > ESP32S31_GT1151_MAX_POINTS ||
      report_size != ESP32S31_GT1151_REPORT_SIZE(reported_points))
    return ESP32S31_GT1151_DECODE_INVALID_COUNT;

  uint8_t checksum = 0;
  for (size_t i = 0; i < report_size; i++)
    checksum = (uint8_t)(checksum + report[i]);
  if (checksum != 0u)
    return ESP32S31_GT1151_DECODE_INVALID_CHECKSUM;

  const size_t copied_points =
      reported_points < point_capacity ? reported_points : point_capacity;
  for (size_t i = 0; i < copied_points; i++)
  {
    const size_t offset = 1u + i * 8u;
    points[i].track_id = report[offset] & 0x0fu;
    points[i].x = (uint16_t)(report[offset + 1u] |
                             ((uint16_t)report[offset + 2u] << 8));
    points[i].y = (uint16_t)(report[offset + 3u] |
                             ((uint16_t)report[offset + 4u] << 8));
    points[i].strength = (uint16_t)(report[offset + 5u] |
                                    ((uint16_t)report[offset + 6u] << 8));
  }

  *point_count = copied_points;
  return ESP32S31_GT1151_DECODE_OK;
}
