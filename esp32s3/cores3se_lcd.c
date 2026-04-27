/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "esp32s3/cores3se_lcd.h"

#include <stdint.h>

#include "common.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CORES3SE_LCD_WIDTH 320
#define CORES3SE_LCD_HEIGHT 240
#define CORES3SE_GBA_WINDOW_X ((CORES3SE_LCD_WIDTH - GBA_SCREEN_WIDTH) / 2)
#define CORES3SE_GBA_WINDOW_Y ((CORES3SE_LCD_HEIGHT - GBA_SCREEN_HEIGHT) / 2)

#define CORES3SE_I2C_SPEED_HZ 400000u
#define CORES3SE_I2C_TIMEOUT_MS 1000

#define CORES3SE_PIN_I2C_SCL GPIO_NUM_11
#define CORES3SE_PIN_I2C_SDA GPIO_NUM_12

#define CORES3SE_PIN_LCD_SCLK GPIO_NUM_36
#define CORES3SE_PIN_LCD_MOSI GPIO_NUM_37
#define CORES3SE_PIN_LCD_DC GPIO_NUM_35
#define CORES3SE_PIN_LCD_CS GPIO_NUM_3

#define CORES3SE_AW9523_ADDR 0x58u
#define CORES3SE_AW9523_REG_INPUT0 0x00u
#define CORES3SE_AW9523_REG_INPUT1 0x01u
#define CORES3SE_AW9523_REG_OUTPUT0 0x02u
#define CORES3SE_AW9523_REG_OUTPUT1 0x03u
#define CORES3SE_AW9523_REG_CONFIG0 0x04u
#define CORES3SE_AW9523_REG_CONFIG1 0x05u
#define CORES3SE_AW9523_REG_ID 0x10u
#define CORES3SE_AW9523_REG_GLOBAL_CONTROL 0x11u
#define CORES3SE_AW9523_REG_LED_MODE0 0x12u
#define CORES3SE_AW9523_REG_LED_MODE1 0x13u
#define CORES3SE_AW9523_EXPECTED_ID 0x23u

#define CORES3SE_AW9523_BUS_ENABLE_MASK 0x02u
#define CORES3SE_AW9523_SPEAKER_ENABLE_MASK 0x04u
#define CORES3SE_AW9523_LCD_RESET_MASK 0x20u
#define CORES3SE_AW9523_BOOST_ENABLE_MASK 0x80u

#define CORES3SE_AXP2101_ADDR 0x34u
#define CORES3SE_AXP2101_REG_ENABLE0 0x90u
#define CORES3SE_AXP2101_REG_ALDO1 0x92u
#define CORES3SE_AXP2101_REG_ALDO2 0x93u
#define CORES3SE_AXP2101_REG_ALDO3 0x94u
#define CORES3SE_AXP2101_REG_ALDO4 0x95u
#define CORES3SE_AXP2101_REG_DLDO1 0x99u
#define CORES3SE_AXP2101_REG_POWER_KEY 0x27u
#define CORES3SE_AXP2101_REG_CHARGE_LED 0x69u
#define CORES3SE_AXP2101_REG_COMMON_CONFIG 0x10u
#define CORES3SE_AXP2101_REG_ADC_ENABLE 0x30u

#define CORES3SE_LCD_HOST SPI3_HOST
#define CORES3SE_LCD_PIXEL_CLOCK_HZ 40000000u
#define CORES3SE_BACKLIGHT_BRIGHTNESS 60u

#define CORES3SE_LCD_MADCTL LCD_CMD_BGR_BIT
#define CORES3SE_LCD_COLMOD 0x55u

#define CORES3SE_LCD_CMD_SETEXTC 0xC8u
#define CORES3SE_LCD_CMD_PWCTR1 0xC0u
#define CORES3SE_LCD_CMD_PWCTR2 0xC1u
#define CORES3SE_LCD_CMD_VMCTR1 0xC5u
#define CORES3SE_LCD_CMD_DFUNCTR 0xB6u

static const char *TAG = "cores3se-lcd";

static const uint8_t s_lcd_setextc[] = { 0xFF, 0x93, 0x42 };
static const uint8_t s_lcd_pwctr1[] = { 0x12, 0x12 };
static const uint8_t s_lcd_pwctr2[] = { 0x03 };
static const uint8_t s_lcd_vmctr1[] = { 0xF2 };
static const uint8_t s_lcd_b0[] = { 0xE0 };
static const uint8_t s_lcd_f6[] = { 0x01, 0x00, 0x00 };
static const uint8_t s_lcd_colmod[] = { CORES3SE_LCD_COLMOD };
static const uint8_t s_lcd_madctl[] = { CORES3SE_LCD_MADCTL };
static const uint8_t s_lcd_gamma_pos[] = {
  0x00, 0x0C, 0x11, 0x04, 0x11, 0x08, 0x37, 0x89,
  0x4C, 0x06, 0x0C, 0x0A, 0x2E, 0x34, 0x0F,
};
static const uint8_t s_lcd_gamma_neg[] = {
  0x00, 0x0B, 0x11, 0x05, 0x13, 0x09, 0x33, 0x67,
  0x48, 0x07, 0x0E, 0x0B, 0x2E, 0x33, 0x0F,
};
static const uint8_t s_lcd_dfunctr[] = { 0x08, 0x82, 0x1D, 0x04 };

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_aw9523_dev;
static i2c_master_dev_handle_t s_axp2101_dev;
static esp_lcd_panel_io_handle_t s_lcd_io;
static bool s_lcd_ready;

static bool log_if_error(esp_err_t err, const char *what)
{
  if (err == ESP_OK)
    return true;

  ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
  return false;
}

static bool add_i2c_device(uint8_t address,
                           i2c_master_dev_handle_t *handle,
                           const char *what)
{
  i2c_device_config_t dev_cfg = { 0 };

  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = address;
  dev_cfg.scl_speed_hz = CORES3SE_I2C_SPEED_HZ;

  if (!log_if_error(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, handle),
                    what))
  {
    *handle = NULL;
    return false;
  }

  return true;
}

static bool write_reg8(i2c_master_dev_handle_t dev,
                       uint8_t reg,
                       uint8_t value,
                       const char *what)
{
  const uint8_t data[] = { reg, value };

  if (!dev)
    return false;

  return log_if_error(i2c_master_transmit(dev, data, sizeof(data),
                                          CORES3SE_I2C_TIMEOUT_MS),
                      what);
}

static bool read_reg8(i2c_master_dev_handle_t dev,
                      uint8_t reg,
                      uint8_t *value,
                      const char *what)
{
  if (!dev || !value)
    return false;

  return log_if_error(i2c_master_transmit_receive(dev, &reg, 1, value, 1,
                                                  CORES3SE_I2C_TIMEOUT_MS),
                      what);
}

static bool aw9523_write_reg(uint8_t reg, uint8_t value)
{
  return write_reg8(s_aw9523_dev, reg, value, "write AW9523 register");
}

static bool aw9523_read_reg(uint8_t reg, uint8_t *value)
{
  return read_reg8(s_aw9523_dev, reg, value, "read AW9523 register");
}

static bool aw9523_update_bits(uint8_t reg, uint8_t set_mask, uint8_t clear_mask)
{
  uint8_t value = 0;

  if (!aw9523_read_reg(reg, &value))
    return false;

  value = (uint8_t)((value | set_mask) & (uint8_t)~clear_mask);
  return aw9523_write_reg(reg, value);
}

static bool axp_write_reg(uint8_t reg, uint8_t value)
{
  return write_reg8(s_axp2101_dev, reg, value, "write AXP2101 register");
}

static bool axp_read_reg(uint8_t reg, uint8_t *value)
{
  return read_reg8(s_axp2101_dev, reg, value, "read AXP2101 register");
}

static bool axp_write_pairs(const uint8_t *pairs, size_t size)
{
  size_t i;

  if (!pairs || (size & 1u) != 0)
    return false;

  for (i = 0; i < size; i += 2)
  {
    if (!axp_write_reg(pairs[i], pairs[i + 1]))
      return false;
  }

  return true;
}

static bool set_backlight(uint8_t brightness)
{
  uint8_t enable0 = 0;
  uint8_t raw = 0;

  if (!axp_read_reg(CORES3SE_AXP2101_REG_ENABLE0, &enable0))
    return false;

  if (brightness != 0)
  {
    raw = (uint8_t)(((unsigned)brightness + 641u) >> 5);
    if (raw > 0x1F)
      raw = 0x1F;
    enable0 = (uint8_t)(enable0 | 0x80u);
  }
  else
  {
    enable0 = (uint8_t)(enable0 & (uint8_t)~0x80u);
  }

  if (!axp_write_reg(CORES3SE_AXP2101_REG_ENABLE0, enable0))
    return false;

  return axp_write_reg(CORES3SE_AXP2101_REG_DLDO1, raw);
}

static void clear_touch_interrupt(void)
{
  uint8_t ignored = 0;

  if (!s_aw9523_dev)
    return;

  (void)aw9523_read_reg(CORES3SE_AW9523_REG_INPUT0, &ignored);
  (void)aw9523_read_reg(CORES3SE_AW9523_REG_INPUT1, &ignored);
}

static bool init_i2c_bus(void)
{
  i2c_master_bus_config_t bus_cfg = { 0 };

  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = CORES3SE_PIN_I2C_SDA;
  bus_cfg.scl_io_num = CORES3SE_PIN_I2C_SCL;
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;

  return log_if_error(i2c_new_master_bus(&bus_cfg, &s_i2c_bus),
                      "create I2C bus");
}

static bool init_io_expander(void)
{
  gpio_config_t spi_probe_cfg = { 0 };
  uint8_t id = 0;
  uint8_t output0_mask;
  bool enable_bus_5v;

  if (!add_i2c_device(CORES3SE_AW9523_ADDR, &s_aw9523_dev, "add AW9523"))
    return false;

  if (!aw9523_read_reg(CORES3SE_AW9523_REG_ID, &id))
    return false;

  if (id != CORES3SE_AW9523_EXPECTED_ID)
  {
    ESP_LOGE(TAG, "unexpected AW9523 id: 0x%02X", id);
    return false;
  }

  spi_probe_cfg.pin_bit_mask = (1ULL << CORES3SE_PIN_LCD_DC) |
                               (1ULL << CORES3SE_PIN_LCD_SCLK) |
                               (1ULL << CORES3SE_PIN_LCD_MOSI);
  spi_probe_cfg.mode = GPIO_MODE_INPUT;
  spi_probe_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  spi_probe_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  spi_probe_cfg.intr_type = GPIO_INTR_DISABLE;
  if (!log_if_error(gpio_config(&spi_probe_cfg), "probe LCD SPI pullups"))
    return false;

  enable_bus_5v = gpio_get_level(CORES3SE_PIN_LCD_DC) == 0 &&
                  gpio_get_level(CORES3SE_PIN_LCD_SCLK) == 0 &&
                  gpio_get_level(CORES3SE_PIN_LCD_MOSI) == 0;

  output0_mask = (uint8_t)(0x01u | CORES3SE_AW9523_SPEAKER_ENABLE_MASK);
  if (enable_bus_5v)
    output0_mask = (uint8_t)(output0_mask | CORES3SE_AW9523_BUS_ENABLE_MASK);

  if (!aw9523_update_bits(CORES3SE_AW9523_REG_OUTPUT0, output0_mask, 0) ||
      !aw9523_update_bits(CORES3SE_AW9523_REG_OUTPUT1,
                          (uint8_t)(0x03u |
                                    CORES3SE_AW9523_BOOST_ENABLE_MASK),
                          0) ||
      !aw9523_write_reg(CORES3SE_AW9523_REG_CONFIG0, 0x18) ||
      !aw9523_write_reg(CORES3SE_AW9523_REG_CONFIG1, 0x0C) ||
      !aw9523_write_reg(CORES3SE_AW9523_REG_GLOBAL_CONTROL, 0x10) ||
      !aw9523_write_reg(CORES3SE_AW9523_REG_LED_MODE0, 0xFF) ||
      !aw9523_write_reg(CORES3SE_AW9523_REG_LED_MODE1, 0xFF))
  {
    return false;
  }

  ESP_LOGI(TAG, "AW9523 ready: bus_5v=%s boost=on",
           enable_bus_5v ? "enabled" : "off");
  clear_touch_interrupt();
  return true;
}

static bool init_pmu(void)
{
  uint8_t status = 0;
  static const uint8_t init_pairs[] = {
    CORES3SE_AXP2101_REG_ENABLE0, 0xBF,
    CORES3SE_AXP2101_REG_ALDO1, 18 - 5,
    CORES3SE_AXP2101_REG_ALDO2, 33 - 5,
    CORES3SE_AXP2101_REG_ALDO3, 33 - 5,
    CORES3SE_AXP2101_REG_ALDO4, 33 - 5,
    CORES3SE_AXP2101_REG_POWER_KEY, 0x00,
    CORES3SE_AXP2101_REG_CHARGE_LED, 0x11,
    CORES3SE_AXP2101_REG_COMMON_CONFIG, 0x30,
    CORES3SE_AXP2101_REG_ADC_ENABLE, 0x0F,
  };

  if (!add_i2c_device(CORES3SE_AXP2101_ADDR, &s_axp2101_dev, "add AXP2101"))
    return false;

  if (!axp_read_reg(0x03, &status))
    return false;

  if (!axp_write_pairs(init_pairs, sizeof(init_pairs)))
    return false;

  return set_backlight(CORES3SE_BACKLIGHT_BRIGHTNESS);
}

static bool lcd_tx_param(int cmd, const void *data, size_t size, const char *what)
{
  return log_if_error(esp_lcd_panel_io_tx_param(s_lcd_io, cmd, data, size),
                      what);
}

static bool lcd_wait_idle(const char *what)
{
  return lcd_tx_param(-1, NULL, 0, what);
}

static bool lcd_set_window(unsigned x, unsigned y, unsigned width, unsigned height)
{
  uint16_t x1 = (uint16_t)x;
  uint16_t x2 = (uint16_t)(x + width - 1);
  uint16_t y1 = (uint16_t)y;
  uint16_t y2 = (uint16_t)(y + height - 1);
  const uint8_t col_data[] = {
    (uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2,
  };
  const uint8_t row_data[] = {
    (uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2,
  };

  return lcd_tx_param(LCD_CMD_CASET, col_data, sizeof(col_data),
                      "set LCD column") &&
         lcd_tx_param(LCD_CMD_RASET, row_data, sizeof(row_data),
                      "set LCD row");
}

static void lcd_swap_rgb565_bytes(uint16_t *pixels, unsigned width,
                                  unsigned height, size_t pitch)
{
  unsigned row;

  for (row = 0; row < height; row++)
  {
    uint16_t *line = (uint16_t *)((uint8_t *)pixels + ((size_t)row * pitch));
    unsigned col = 0;

    if ((((uintptr_t)line) & 3u) == 0)
    {
      uint32_t *pair = (uint32_t *)line;
      unsigned pairs = width / 2;
      unsigned i;

      for (i = 0; i < pairs; i++)
      {
        uint32_t value = pair[i];
        pair[i] = ((value & 0x00FF00FFu) << 8) |
                  ((value & 0xFF00FF00u) >> 8);
      }
      col = pairs * 2;
    }

    for (; col < width; col++)
    {
      uint16_t value = line[col];
      line[col] = (uint16_t)((value << 8) | (value >> 8));
    }
  }
}

static bool lcd_write_gba_window_inplace(uint16_t *pixels)
{
  bool ok;

  if (!lcd_set_window(CORES3SE_GBA_WINDOW_X, CORES3SE_GBA_WINDOW_Y,
                      GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT))
    return false;

  lcd_swap_rgb565_bytes(pixels, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT,
                        GBA_SCREEN_PITCH * sizeof(uint16_t));

  ok = log_if_error(esp_lcd_panel_io_tx_color(
                      s_lcd_io, LCD_CMD_RAMWR, pixels,
                      GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT *
                        sizeof(uint16_t)),
                    "write LCD GBA window") &&
       lcd_wait_idle("wait LCD GBA window idle");

  lcd_swap_rgb565_bytes(pixels, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT,
                        GBA_SCREEN_PITCH * sizeof(uint16_t));
  return ok;
}

static bool init_lcd(void)
{
  spi_bus_config_t bus_cfg = { 0 };
  esp_lcd_panel_io_spi_config_t io_cfg = { 0 };

  if (!aw9523_update_bits(CORES3SE_AW9523_REG_OUTPUT1, 0,
                          CORES3SE_AW9523_LCD_RESET_MASK))
    return false;
  vTaskDelay(pdMS_TO_TICKS(8));

  bus_cfg.sclk_io_num = CORES3SE_PIN_LCD_SCLK;
  bus_cfg.mosi_io_num = CORES3SE_PIN_LCD_MOSI;
  bus_cfg.miso_io_num = -1;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  bus_cfg.max_transfer_sz =
    GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT * sizeof(uint16_t);
  if (!log_if_error(spi_bus_initialize(CORES3SE_LCD_HOST, &bus_cfg,
                                       SPI_DMA_CH_AUTO),
                    "init LCD SPI"))
  {
    return false;
  }

  io_cfg.cs_gpio_num = CORES3SE_PIN_LCD_CS;
  io_cfg.dc_gpio_num = CORES3SE_PIN_LCD_DC;
  io_cfg.spi_mode = 0;
  io_cfg.pclk_hz = CORES3SE_LCD_PIXEL_CLOCK_HZ;
  io_cfg.trans_queue_depth = 1;
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;
  io_cfg.flags.sio_mode = 1;

  if (!log_if_error(esp_lcd_new_panel_io_spi(
                      (esp_lcd_spi_bus_handle_t)CORES3SE_LCD_HOST,
                      &io_cfg, &s_lcd_io),
                    "create LCD IO"))
  {
    return false;
  }

  if (!aw9523_update_bits(CORES3SE_AW9523_REG_OUTPUT1,
                          CORES3SE_AW9523_LCD_RESET_MASK, 0))
    return false;
  vTaskDelay(pdMS_TO_TICKS(64));

  if (!lcd_tx_param(LCD_CMD_SWRESET, NULL, 0, "LCD SWRESET"))
    return false;
  vTaskDelay(pdMS_TO_TICKS(20));

  if (!lcd_tx_param(CORES3SE_LCD_CMD_SETEXTC, s_lcd_setextc,
                    sizeof(s_lcd_setextc), "LCD SETEXTC") ||
      !lcd_tx_param(CORES3SE_LCD_CMD_PWCTR1, s_lcd_pwctr1,
                    sizeof(s_lcd_pwctr1), "LCD PWCTR1") ||
      !lcd_tx_param(CORES3SE_LCD_CMD_PWCTR2, s_lcd_pwctr2,
                    sizeof(s_lcd_pwctr2), "LCD PWCTR2") ||
      !lcd_tx_param(CORES3SE_LCD_CMD_VMCTR1, s_lcd_vmctr1,
                    sizeof(s_lcd_vmctr1), "LCD VMCTR1") ||
      !lcd_tx_param(0xB0, s_lcd_b0, sizeof(s_lcd_b0), "LCD B0") ||
      !lcd_tx_param(0xF6, s_lcd_f6, sizeof(s_lcd_f6), "LCD F6") ||
      !lcd_tx_param(0xE0, s_lcd_gamma_pos, sizeof(s_lcd_gamma_pos),
                    "LCD GMCTRP1") ||
      !lcd_tx_param(0xE1, s_lcd_gamma_neg, sizeof(s_lcd_gamma_neg),
                    "LCD GMCTRN1") ||
      !lcd_tx_param(CORES3SE_LCD_CMD_DFUNCTR, s_lcd_dfunctr,
                    sizeof(s_lcd_dfunctr), "LCD DFUNCTR") ||
      !lcd_tx_param(LCD_CMD_SLPOUT, NULL, 0, "LCD SLPOUT"))
  {
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(120));

  if (!lcd_tx_param(LCD_CMD_COLMOD, s_lcd_colmod, sizeof(s_lcd_colmod),
                    "LCD COLMOD") ||
      !lcd_tx_param(LCD_CMD_MADCTL, s_lcd_madctl, sizeof(s_lcd_madctl),
                    "LCD MADCTL") ||
      !lcd_tx_param(LCD_CMD_IDMOFF, NULL, 0, "LCD IDMOFF") ||
      !lcd_tx_param(LCD_CMD_INVON, NULL, 0, "LCD INVON") ||
      !lcd_tx_param(LCD_CMD_DISPON, NULL, 0, "LCD DISPON"))
  {
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(20));
  return true;
}

bool esp32s3_cores3se_lcd_init(void)
{
  if (s_lcd_ready)
    return true;

  if (!init_i2c_bus() || !init_io_expander() || !init_pmu() || !init_lcd())
  {
    ESP_LOGW(TAG, "CoreS3 SE LCD init failed; continuing without LCD output");
    return false;
  }

  s_lcd_ready = true;
  ESP_LOGI(TAG, "CoreS3 SE LCD ready");
  return true;
}

bool esp32s3_cores3se_lcd_ready(void)
{
  return s_lcd_ready;
}

bool esp32s3_cores3se_lcd_present_rgb565(const void *pixels,
                                          unsigned width,
                                          unsigned height,
                                          size_t pitch)
{
  uint16_t *mutable_pixels = (uint16_t *)pixels;

  if (!s_lcd_ready || !mutable_pixels || width == 0 || height == 0 ||
      pitch < (size_t)width * sizeof(uint16_t))
    return false;

  if (width != GBA_SCREEN_WIDTH || height != GBA_SCREEN_HEIGHT ||
      pitch != GBA_SCREEN_PITCH * sizeof(uint16_t) ||
      (((uintptr_t)mutable_pixels) & 1u) != 0)
    return false;

  return lcd_write_gba_window_inplace(mutable_pixels);
}
