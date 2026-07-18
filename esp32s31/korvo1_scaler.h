#ifndef GPSP_ESP32S31_KORVO1_SCALER_H
#define GPSP_ESP32S31_KORVO1_SCALER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t prepare_us;
  uint32_t transfer_us;
  bool benchmarked_call;
} esp32s31_scaler_stats_t;

bool esp32s31_korvo1_scaler_init(void);
bool esp32s31_korvo1_scaler_scale(void *output, size_t output_pitch,
                                  const void *input, unsigned width,
                                  unsigned height, size_t input_pitch);
const char *esp32s31_korvo1_scaler_name(void);
void esp32s31_korvo1_scaler_get_stats(esp32s31_scaler_stats_t *out);

#endif
