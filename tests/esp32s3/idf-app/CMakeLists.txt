cmake_minimum_required(VERSION 3.22)

set(GPSP_TEST_BACKEND "dynarec" CACHE STRING "gpSP backend for ESP32-S3 test app")
set(GPSP_TEST_ROM "${CMAKE_CURRENT_LIST_DIR}/../../dhrystone/dhrystone_arm.gba"
    CACHE FILEPATH "GBA ROM to embed in the ESP32-S3 test app")
set(GPSP_TEST_MODE "dhrystone" CACHE STRING "ESP32-S3 test mode: dhrystone, frames, or debug")
set(GPSP_TEST_FRAMES "300" CACHE STRING "Number of frames to run in frames mode")
set(GPSP_TEST_EXPECT_FB_HASH "0" CACHE STRING "Expected FNV-1a framebuffer stream hash, or 0 to skip")
set(GPSP_TEST_DUMP_FRAME "0" CACHE STRING "Emit the last RGB565 frame over UART for host PNG capture")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(gpsp_esp32s3_dhrystone)
