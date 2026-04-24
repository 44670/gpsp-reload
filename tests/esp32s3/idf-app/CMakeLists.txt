cmake_minimum_required(VERSION 3.22)

set(GPSP_TEST_BACKEND "dynarec" CACHE STRING "gpSP backend for ESP32-S3 test app")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(gpsp_esp32s3_dhrystone)
