#ifndef GPSP_ESP32S31_GT1151_PROTOCOL_H
#define GPSP_ESP32S31_GT1151_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define ESP32S31_GT1151_MAX_POINTS 10u
#define ESP32S31_GT1151_REPORT_SIZE(point_count) \
  (1u + 8u * (point_count) + 2u)

typedef struct {
  uint16_t x;
  uint16_t y;
  uint16_t strength;
  uint8_t track_id;
} esp32s31_touch_point_t;

typedef enum {
  ESP32S31_GT1151_DECODE_OK = 0,
  ESP32S31_GT1151_DECODE_INVALID_ARGUMENT,
  ESP32S31_GT1151_DECODE_INVALID_COUNT,
  ESP32S31_GT1151_DECODE_INVALID_CHECKSUM,
} esp32s31_gt1151_decode_result_t;

esp32s31_gt1151_decode_result_t esp32s31_gt1151_decode_report(
    const uint8_t *report, size_t report_size,
    esp32s31_touch_point_t *points, size_t point_capacity,
    size_t *point_count);

#endif
