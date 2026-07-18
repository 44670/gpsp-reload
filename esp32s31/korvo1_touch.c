#include "korvo1_touch.h"

#include <inttypes.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "korvo1_pins.h"
#include "sdkconfig.h"

#define GT1151_PRODUCT_ID_REG 0x8140u
#define GT1151_READ_XY_REG 0x814eu
#define GT1151_I2C_SPEED_HZ 400000u
#define GT1151_I2C_TIMEOUT_MS 20

static const char *TAG = "korvo1_touch";

typedef struct {
  i2c_master_bus_handle_t bus;
  i2c_master_dev_handle_t device;
  esp32s31_touch_stats_t stats;
  bool ready;
} korvo1_touch_state_t;

static korvo1_touch_state_t s_touch;

static bool product_id_character(uint8_t value)
{
  return (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9');
}

static esp_err_t gt1151_read(uint16_t reg, void *data, size_t size)
{
  const uint8_t address[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
  const esp_err_t error = i2c_master_transmit_receive(
      s_touch.device, address, sizeof(address), data, size,
      GT1151_I2C_TIMEOUT_MS);
  if (error != ESP_OK)
    s_touch.stats.i2c_errors++;
  return error;
}

static esp_err_t gt1151_write_byte(uint16_t reg, uint8_t value)
{
  const uint8_t message[] = {
      (uint8_t)(reg >> 8), (uint8_t)reg, value,
  };
  const esp_err_t error = i2c_master_transmit(
      s_touch.device, message, sizeof(message), GT1151_I2C_TIMEOUT_MS);
  if (error != ESP_OK)
    s_touch.stats.i2c_errors++;
  return error;
}

static void touch_cleanup(void)
{
  s_touch.ready = false;
  if (s_touch.device != NULL)
  {
    (void)i2c_master_bus_rm_device(s_touch.device);
    s_touch.device = NULL;
  }
  if (s_touch.bus != NULL)
  {
    (void)i2c_del_master_bus(s_touch.bus);
    s_touch.bus = NULL;
  }
}

bool esp32s31_korvo1_touch_init(void)
{
  if (s_touch.ready)
    return true;

#ifndef CONFIG_IDF_TARGET_ESP32S31
  ESP_LOGE(TAG, "driver requires CONFIG_IDF_TARGET_ESP32S31");
  return false;
#else
  memset(&s_touch, 0, sizeof(s_touch));

  const i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = KORVO1_I2C_SDA,
      .scl_io_num = KORVO1_I2C_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  esp_err_t error = i2c_new_master_bus(&bus_config, &s_touch.bus);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "create I2C bus failed: %s", esp_err_to_name(error));
    touch_cleanup();
    return false;
  }

  const i2c_device_config_t device_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = KORVO1_TOUCH_I2C_ADDRESS,
      .scl_speed_hz = GT1151_I2C_SPEED_HZ,
  };
  error = i2c_master_bus_add_device(s_touch.bus, &device_config,
                                    &s_touch.device);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "add GT1151 I2C device failed: %s",
             esp_err_to_name(error));
    touch_cleanup();
    return false;
  }

  uint8_t product[11] = {0};
  error = gt1151_read(GT1151_PRODUCT_ID_REG, product, sizeof(product));
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "read GT1151 product ID failed: %s",
             esp_err_to_name(error));
    touch_cleanup();
    ESP_LOGW(TAG, "touch unavailable; LCD/UART operation continues");
    return false;
  }

  uint8_t product_sum = 0;
  for (size_t i = 0; i < sizeof(product); i++)
    product_sum = (uint8_t)(product_sum + product[i]);
  if (product_sum == 0u || !product_id_character(product[0]) ||
      !product_id_character(product[1]) ||
      !product_id_character(product[2]) || product[10] == 0xffu)
  {
    ESP_LOGE(TAG, "invalid GT1151 product ID response");
    touch_cleanup();
    return false;
  }

  const uint32_t patch_id = ((uint32_t)product[4] << 16) |
                            ((uint32_t)product[5] << 8) | product[6];
  const uint32_t mask_id = ((uint32_t)product[7] << 16) |
                           ((uint32_t)product[8] << 8) | product[9];
  s_touch.ready = true;
  ESP_LOGI(TAG,
           "GT1151 ready addr=0x%02x id=GT%c%c%c%c patch=%06" PRIx32
           " mask=%04" PRIx32 " sensor=%02x",
           KORVO1_TOUCH_I2C_ADDRESS, product[0], product[1], product[2],
           product[3], patch_id, mask_id >> 8, product[10] & 0x0fu);
  return true;
#endif
}

bool esp32s31_korvo1_touch_ready(void)
{
  return s_touch.ready;
}

esp_err_t esp32s31_korvo1_touch_read(esp32s31_touch_point_t *points,
                                     size_t point_capacity,
                                     size_t *point_count)
{
  if (!s_touch.ready)
    return ESP_ERR_INVALID_STATE;
  if (point_count == NULL || (point_capacity != 0u && points == NULL))
    return ESP_ERR_INVALID_ARG;

  *point_count = 0;
  s_touch.stats.polls++;

  uint8_t status = 0;
  esp_err_t error = gt1151_read(GT1151_READ_XY_REG, &status, sizeof(status));
  if (error != ESP_OK)
    return error;

  const size_t reported_points = status & 0x0fu;
  if (reported_points == 0u)
    return gt1151_write_byte(GT1151_READ_XY_REG, 0);
  if (reported_points > ESP32S31_GT1151_MAX_POINTS)
  {
    s_touch.stats.protocol_errors++;
    (void)gt1151_write_byte(GT1151_READ_XY_REG, 0);
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint8_t report[ESP32S31_GT1151_REPORT_SIZE(
      ESP32S31_GT1151_MAX_POINTS)];
  const size_t report_size = ESP32S31_GT1151_REPORT_SIZE(reported_points);
  error = gt1151_read(GT1151_READ_XY_REG, report, report_size);
  if (error != ESP_OK)
    return error;

  const esp_err_t clear_error =
      gt1151_write_byte(GT1151_READ_XY_REG, 0);
  size_t decoded_points = 0;
  const esp32s31_gt1151_decode_result_t decode =
      esp32s31_gt1151_decode_report(report, report_size, points,
                                    point_capacity, &decoded_points);
  if (decode == ESP32S31_GT1151_DECODE_INVALID_CHECKSUM)
  {
    s_touch.stats.checksum_errors++;
    return ESP_ERR_INVALID_CRC;
  }
  if (decode != ESP32S31_GT1151_DECODE_OK)
  {
    s_touch.stats.protocol_errors++;
    return ESP_ERR_INVALID_RESPONSE;
  }
  if (clear_error != ESP_OK)
    return clear_error;

  *point_count = decoded_points;
  s_touch.stats.reports++;
  s_touch.stats.points += (uint32_t)decoded_points;
  if (reported_points > decoded_points)
    s_touch.stats.truncated_points +=
        (uint32_t)(reported_points - decoded_points);
  return ESP_OK;
}

void esp32s31_korvo1_touch_get_stats(esp32s31_touch_stats_t *out)
{
  if (out != NULL)
    *out = s_touch.stats;
}
