#ifndef GPSP_ESP32S31_KORVO1_SD_H
#define GPSP_ESP32S31_KORVO1_SD_H

#include <stdbool.h>

#include "esp_err.h"

#define ESP32S31_KORVO1_SD_MOUNT_POINT "/sdcard"

esp_err_t esp32s31_korvo1_sd_mount(void);
esp_err_t esp32s31_korvo1_sd_unmount(void);
bool esp32s31_korvo1_sd_mounted(void);

#endif
