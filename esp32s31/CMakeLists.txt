cmake_minimum_required(VERSION 3.22)

set(KORVO1_LCD_COMPAT_18MHZ "0" CACHE BOOL
    "Use the older 18 MHz Korvo-1 RGB timing profile")
set(KORVO1_LCD_GPIO38_DISP "0" CACHE BOOL
    "Experimentally drive GPIO38 as the RGB DISP signal")

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
