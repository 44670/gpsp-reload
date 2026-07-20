#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  bool ready;
  bool connected;
  bool xinput;
  uint16_t vid;
  uint16_t pid;
  uint16_t joypad_mask;
  uint32_t connect_count;
  uint32_t disconnect_count;
  uint32_t input_reports;
  uint32_t changed_reports;
  uint32_t transfer_errors;
  uint32_t dropped_events;
  uint16_t report_descriptor_bytes;
  uint8_t last_report_bytes;
} esp32s31_usb_gamepad_stats_t;

/* Starts the ESP32-S31 USB OTG host and the HID class driver. */
esp_err_t esp32s31_korvo1_usb_gamepad_init(void);

/* Returns a libretro joypad bit mask. Safe to call from the emulator task. */
uint16_t esp32s31_korvo1_usb_gamepad_mask(void);

void esp32s31_korvo1_usb_gamepad_get_stats(
    esp32s31_usb_gamepad_stats_t *stats);

#ifdef __cplusplus
}
#endif
