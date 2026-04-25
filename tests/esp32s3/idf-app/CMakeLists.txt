cmake_minimum_required(VERSION 3.22)

set(GPSP_TEST_FRAMES "300" CACHE STRING "Number of frames to run in frames mode")
set(GPSP_TEST_EXPECT_FB_HASH "0" CACHE STRING "Expected FNV-1a framebuffer stream hash, or 0 to skip")
set(GPSP_TEST_DUMP_FRAME "0" CACHE STRING "Emit the last RGB565 frame over UART for host PNG capture")
set(GPSP_CORES3SE_LCD "1" CACHE BOOL "Enable CoreS3 SE hardware init and LCD output")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(gpsp_esp32s3_dhrystone)
