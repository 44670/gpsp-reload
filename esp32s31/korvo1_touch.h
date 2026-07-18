#ifndef GPSP_ESP32S31_KORVO1_TOUCH_H
#define GPSP_ESP32S31_KORVO1_TOUCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "gt1151_protocol.h"

typedef struct {
  uint32_t polls;
  uint32_t reports;
  uint32_t points;
  uint32_t truncated_points;
  uint32_t i2c_errors;
  uint32_t checksum_errors;
  uint32_t protocol_errors;
} esp32s31_touch_stats_t;

bool esp32s31_korvo1_touch_init(void);
bool esp32s31_korvo1_touch_ready(void);
esp_err_t esp32s31_korvo1_touch_read(esp32s31_touch_point_t *points,
                                     size_t point_capacity,
                                     size_t *point_count);
void esp32s31_korvo1_touch_get_stats(esp32s31_touch_stats_t *out);

#endif
