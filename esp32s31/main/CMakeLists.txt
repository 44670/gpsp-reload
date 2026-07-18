idf_component_register(
    SRCS
      "app_main.c"
      "../korvo1_lcd.c"
      "../korvo1_touch.c"
      "../rgb565_scale3x.c"
      "../gt1151_protocol.c"
    INCLUDE_DIRS
      "."
      ".."
    REQUIRES
      esp_driver_i2c
      esp_lcd
      esp_mm
      esp_psram
      esp_timer
      heap
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP32S31_KORVO1_LCD_COMPAT_18MHZ=$<BOOL:${KORVO1_LCD_COMPAT_18MHZ}>
    ESP32S31_KORVO1_LCD_USE_GPIO38_DISP=$<BOOL:${KORVO1_LCD_GPIO38_DISP}>
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)
