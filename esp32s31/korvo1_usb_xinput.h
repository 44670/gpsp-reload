#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct
{
  bool ready;
  bool connected;
  uint16_t vid;
  uint16_t pid;
  uint16_t joypad_mask;
  uint16_t raw_buttons;
  uint32_t input_reports;
  uint32_t changed_reports;
  uint32_t transfer_errors;
  uint32_t connect_count;
  uint32_t disconnect_count;
} esp32s31_xinput_stats_t;

/* USB Host Library must already be installed. */
esp_err_t esp32s31_korvo1_xinput_init(void);
uint16_t esp32s31_korvo1_xinput_mask(void);
void esp32s31_korvo1_xinput_get_stats(esp32s31_xinput_stats_t *stats);

