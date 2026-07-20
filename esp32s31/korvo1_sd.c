#include "korvo1_sd.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "korvo1_pins.h"

static const char *TAG = "korvo1-sd";
static sdmmc_card_t *s_card;
static bool s_power_gpio_configured;

static esp_err_t set_card_power(bool enabled)
{
  if (!s_power_gpio_configured)
  {
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << KORVO1_SD_ENABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t config_error = gpio_config(&config);
    if (config_error != ESP_OK)
      return config_error;
    s_power_gpio_configured = true;
  }

  /* The Korvo-1 TF-card power enable is active-low. */
  const esp_err_t error =
      gpio_set_level(KORVO1_SD_ENABLE, enabled ? 0 : 1);
  if (error == ESP_OK && enabled)
    vTaskDelay(pdMS_TO_TICKS(20));
  return error;
}

esp_err_t esp32s31_korvo1_sd_mount(void)
{
  if (s_card != NULL)
    return ESP_OK;

  esp_err_t error = set_card_power(true);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "TF card power enable failed: %s",
             esp_err_to_name(error));
    return error;
  }

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  host.unaligned_multi_block_rw_max_chunk_size = 8;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = KORVO1_SD_CLK;
  slot.cmd = KORVO1_SD_CMD;
  slot.d0 = KORVO1_SD_D0;
  slot.d1 = KORVO1_SD_D1;
  slot.d2 = KORVO1_SD_D2;
  slot.d3 = KORVO1_SD_D3;
  slot.cd = SDMMC_SLOT_NO_CD;
  slot.wp = SDMMC_SLOT_NO_WP;
  slot.width = 4;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  const esp_vfs_fat_sdmmc_mount_config_t mount = {
      .format_if_mount_failed = false,
      .max_files = 8,
      .allocation_unit_size = 32u * 1024u,
  };
  error = esp_vfs_fat_sdmmc_mount(
      ESP32S31_KORVO1_SD_MOUNT_POINT, &host, &slot, &mount, &s_card);
  if (error != ESP_OK)
  {
    s_card = NULL;
    (void)set_card_power(false);
    ESP_LOGE(TAG, "TF card mount failed: %s", esp_err_to_name(error));
    return error;
  }

  ESP_LOGI(TAG, "TF card mounted at %s, capacity=%" PRIu64 " MiB",
           ESP32S31_KORVO1_SD_MOUNT_POINT,
           ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20);
  return ESP_OK;
}

esp_err_t esp32s31_korvo1_sd_unmount(void)
{
  esp_err_t error = ESP_OK;
  if (s_card != NULL)
  {
    error = esp_vfs_fat_sdcard_unmount(
        ESP32S31_KORVO1_SD_MOUNT_POINT, s_card);
    s_card = NULL;
  }
  const esp_err_t power_error = set_card_power(false);
  return error != ESP_OK ? error : power_error;
}

bool esp32s31_korvo1_sd_mounted(void)
{
  return s_card != NULL;
}
