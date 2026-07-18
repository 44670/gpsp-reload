cmake_minimum_required(VERSION 3.22)

set(KORVO1_LCD_COMPAT_18MHZ "0" CACHE BOOL
    "Use the older 18 MHz Korvo-1 RGB timing profile")
set(KORVO1_LCD_GPIO38_DISP "0" CACHE BOOL
    "Experimentally drive GPIO38 as the RGB DISP signal")
set(GPSP_ESP32S31_PROFILE "0" CACHE BOOL
    "Enable detailed gpSP phase and interrupted-PC profiling")
set(ESP32S31_LCD_MODE "bounce" CACHE STRING
    "LCD scanout: framebuffer or bounce")
set_property(CACHE ESP32S31_LCD_MODE PROPERTY STRINGS
    framebuffer bounce)
set(ESP32S31_SCALER "sram_gdma" CACHE STRING
    "RGB565 scaler: auto, cpu, sram_gdma, or ppa")
set_property(CACHE ESP32S31_SCALER PROPERTY STRINGS
    auto cpu sram_gdma ppa)

if(ESP32S31_SCALER STREQUAL "auto")
    set(ESP32S31_SCALER_MODE 0)
elseif(ESP32S31_SCALER STREQUAL "cpu")
    set(ESP32S31_SCALER_MODE 1)
elseif(ESP32S31_SCALER STREQUAL "sram_gdma")
    set(ESP32S31_SCALER_MODE 2)
elseif(ESP32S31_SCALER STREQUAL "ppa")
    set(ESP32S31_SCALER_MODE 3)
else()
    message(FATAL_ERROR
        "ESP32S31_SCALER must be auto, cpu, sram_gdma, or ppa")
endif()

if(ESP32S31_LCD_MODE STREQUAL "framebuffer")
    set(ESP32S31_LCD_BOUNCE_MODE 0)
elseif(ESP32S31_LCD_MODE STREQUAL "bounce")
    set(ESP32S31_LCD_BOUNCE_MODE 1)
else()
    message(FATAL_ERROR
        "ESP32S31_LCD_MODE must be framebuffer or bounce")
endif()

# Keep generated configuration inside esp32s31/build/.
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(gpsp_esp32s31)
