cmake_minimum_required(VERSION 3.22)

set(KORVO1_LCD_COMPAT_18MHZ "0" CACHE BOOL
    "Use the older 18 MHz Korvo-1 RGB timing profile")
set(KORVO1_LCD_GPIO38_DISP "0" CACHE BOOL
    "Experimentally drive GPIO38 as the RGB DISP signal")
set(GPSP_ESP32S31_PROFILE "0" CACHE BOOL
    "Enable detailed gpSP phase profiling")
set(GPSP_ESP32S31_PC_PROFILE "0" CACHE BOOL
    "Enable the 1 kHz interrupted-PC sampler during phase profiling")
set(GPSP_ESP32S31_PSRAM_FAULT_TRACE "0" CACHE BOOL
    "Capture JIT and AXI state before the ESP32-S31 PSRAM error ISR clears it")
set(GPSP_ESP32S31_LTO "0" CACHE BOOL
    "Enable link-time optimization for gpSP core sources only")
set(GPSP_ESP32S31_DYNAREC "1" CACHE BOOL
    "Enable the native RV32IM gpSP dynarec")
if(GPSP_ESP32S31_PSRAM_FAULT_TRACE AND NOT GPSP_ESP32S31_DYNAREC)
    message(FATAL_ERROR
        "GPSP_ESP32S31_PSRAM_FAULT_TRACE requires GPSP_ESP32S31_DYNAREC")
endif()
set(GPSP_ESP32S31_BOOT_MODE "menu" CACHE STRING
    "ROM boot source: menu (TF card) or flash (direct gamepak mmap)")
set_property(CACHE GPSP_ESP32S31_BOOT_MODE PROPERTY STRINGS
    menu flash)
set(GPSP_ESP32S31_FLASH_SPILL "1" CACHE BOOL
    "Allow unique TF ROM pages that do not fit PSRAM to use gamepak flash")
set(GPSP_ESP32S31_MENU_AUTOROM_NAME "" CACHE STRING
    "Hardware-test-only root TF .gba filename to select without XInput")
set(ESP32S31_GAMEPAK_STATIC_MB "12" CACHE STRING
    "Static PSRAM gamepak container size in MiB, including two work pages")
set(ESP32S31_ROM_JIT_CACHE_KB "1536" CACHE STRING
    "ESP32-S31 ROM dynarec cache size in KiB")
set(ESP32S31_RAM_JIT_CACHE_KB "384" CACHE STRING
    "ESP32-S31 RAM dynarec cache size in KiB")
set(ESP32S31_LCD_MODE "bounce" CACHE STRING
    "LCD scanout: framebuffer or bounce")
set_property(CACHE ESP32S31_LCD_MODE PROPERTY STRINGS
    framebuffer bounce)
set(ESP32S31_LCD_BOUNCE_SOURCE_ROWS "10" CACHE STRING
    "GBA source rows expanded per RGB bounce-buffer refill")
set(ESP32S31_LCD_RENDER_MEMORY "sram" CACHE STRING
    "Native GBA render-buffer placement: sram or psram")
set_property(CACHE ESP32S31_LCD_RENDER_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_IWRAM_MEMORY "sram" CACHE STRING
    "GBA 32 KiB IWRAM placement: sram or psram")
set_property(CACHE ESP32S31_IWRAM_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_BIOS_MEMORY "psram" CACHE STRING
    "Writable GBA 16 KiB BIOS placement: sram or psram")
set_property(CACHE ESP32S31_BIOS_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_COLD_STATE_MEMORY "psram" CACHE STRING
    "Paging, cheat, and disabled-link state placement: sram or psram")
set_property(CACHE ESP32S31_COLD_STATE_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_VRAM_MEMORY "psram" CACHE STRING
    "GBA 96 KiB VRAM placement: sram or psram")
set_property(CACHE ESP32S31_VRAM_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_EXECUTE_ARM_MEMORY "sram" CACHE STRING
    "ARM interpreter hot-loop code placement: sram or psram")
set_property(CACHE ESP32S31_EXECUTE_ARM_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_VIDEO_HOT_MEMORY "sram" CACHE STRING
    "Common RGB565 tile-renderer code placement: sram or psram")
set_property(CACHE ESP32S31_VIDEO_HOT_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_MEMORY_MAP_MEMORY "psram" CACHE STRING
    "32 KiB interpreter read-map placement: sram or psram")
set_property(CACHE ESP32S31_MEMORY_MAP_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_SOUND_BUFFER_MEMORY "sram" CACHE STRING
    "gpSP mixer ring-buffer placement: sram or psram")
set_property(CACHE ESP32S31_SOUND_BUFFER_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_SOUND_BUFFER_BITS "12" CACHE STRING
    "log2 of int16 samples in the gpSP mixer ring (12 through 16)")
set(ESP32S31_OBJ_LIST_MEMORY "psram" CACHE STRING
    "Compacted per-scanline OBJ link placement: sram or psram")
set_property(CACHE ESP32S31_OBJ_LIST_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_HOT_HELPERS_MEMORY "psram" CACHE STRING
    "Profile-selected interpreter helper code placement: sram or psram")
set_property(CACHE ESP32S31_HOT_HELPERS_MEMORY PROPERTY STRINGS
    sram psram)
set(ESP32S31_SCALER "cpu" CACHE STRING
    "RGB565 scaler: auto, cpu, sram_gdma, or ppa")
set_property(CACHE ESP32S31_SCALER PROPERTY STRINGS
    auto cpu sram_gdma ppa)

if(GPSP_ESP32S31_BOOT_MODE STREQUAL "menu")
    set(GPSP_ESP32S31_MENU_BOOT 1)
elseif(GPSP_ESP32S31_BOOT_MODE STREQUAL "flash")
    set(GPSP_ESP32S31_MENU_BOOT 0)
else()
    message(FATAL_ERROR
        "GPSP_ESP32S31_BOOT_MODE must be menu or flash")
endif()

if(NOT ESP32S31_GAMEPAK_STATIC_MB MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "ESP32S31_GAMEPAK_STATIC_MB must be a positive integer")
endif()
math(EXPR ESP32S31_GAMEPAK_STATIC_BYTES
    "${ESP32S31_GAMEPAK_STATIC_MB} * 1024 * 1024")
if(ESP32S31_GAMEPAK_STATIC_BYTES LESS 131072)
    message(FATAL_ERROR
        "ESP32S31_GAMEPAK_STATIC_MB must leave at least one ROM page")
endif()

foreach(cache_name ROM RAM)
    if(NOT ESP32S31_${cache_name}_JIT_CACHE_KB MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "ESP32S31_${cache_name}_JIT_CACHE_KB must be a positive integer")
    endif()
    math(EXPR ESP32S31_${cache_name}_JIT_CACHE_BYTES
        "${ESP32S31_${cache_name}_JIT_CACHE_KB} * 1024")
    if(ESP32S31_${cache_name}_JIT_CACHE_BYTES LESS 65536)
        message(FATAL_ERROR
            "ESP32S31_${cache_name}_JIT_CACHE_KB must be at least 64")
    endif()
endforeach()

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

if(NOT ESP32S31_LCD_BOUNCE_SOURCE_ROWS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "ESP32S31_LCD_BOUNCE_SOURCE_ROWS must be a positive integer")
endif()

if(ESP32S31_LCD_RENDER_MEMORY STREQUAL "sram")
    set(ESP32S31_LCD_RENDER_INTERNAL 1)
elseif(ESP32S31_LCD_RENDER_MEMORY STREQUAL "psram")
    set(ESP32S31_LCD_RENDER_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_LCD_RENDER_MEMORY must be sram or psram")
endif()

if(ESP32S31_IWRAM_MEMORY STREQUAL "sram")
    set(ESP32S31_IWRAM_INTERNAL 1)
elseif(ESP32S31_IWRAM_MEMORY STREQUAL "psram")
    set(ESP32S31_IWRAM_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_IWRAM_MEMORY must be sram or psram")
endif()

if(ESP32S31_BIOS_MEMORY STREQUAL "sram")
    set(ESP32S31_BIOS_INTERNAL 1)
elseif(ESP32S31_BIOS_MEMORY STREQUAL "psram")
    set(ESP32S31_BIOS_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_BIOS_MEMORY must be sram or psram")
endif()

if(ESP32S31_COLD_STATE_MEMORY STREQUAL "sram")
    set(ESP32S31_COLD_STATE_INTERNAL 1)
elseif(ESP32S31_COLD_STATE_MEMORY STREQUAL "psram")
    set(ESP32S31_COLD_STATE_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_COLD_STATE_MEMORY must be sram or psram")
endif()

if(ESP32S31_VRAM_MEMORY STREQUAL "sram")
    set(ESP32S31_VRAM_INTERNAL 1)
elseif(ESP32S31_VRAM_MEMORY STREQUAL "psram")
    set(ESP32S31_VRAM_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_VRAM_MEMORY must be sram or psram")
endif()

if(ESP32S31_EXECUTE_ARM_MEMORY STREQUAL "sram")
    set(ESP32S31_EXECUTE_ARM_INTERNAL 1)
elseif(ESP32S31_EXECUTE_ARM_MEMORY STREQUAL "psram")
    set(ESP32S31_EXECUTE_ARM_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_EXECUTE_ARM_MEMORY must be sram or psram")
endif()

if(ESP32S31_VIDEO_HOT_MEMORY STREQUAL "sram")
    set(ESP32S31_VIDEO_HOT_INTERNAL 1)
elseif(ESP32S31_VIDEO_HOT_MEMORY STREQUAL "psram")
    set(ESP32S31_VIDEO_HOT_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_VIDEO_HOT_MEMORY must be sram or psram")
endif()

if(ESP32S31_MEMORY_MAP_MEMORY STREQUAL "sram")
    set(ESP32S31_MEMORY_MAP_INTERNAL 1)
elseif(ESP32S31_MEMORY_MAP_MEMORY STREQUAL "psram")
    set(ESP32S31_MEMORY_MAP_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_MEMORY_MAP_MEMORY must be sram or psram")
endif()

if(ESP32S31_SOUND_BUFFER_MEMORY STREQUAL "sram")
    set(ESP32S31_SOUND_BUFFER_INTERNAL 1)
elseif(ESP32S31_SOUND_BUFFER_MEMORY STREQUAL "psram")
    set(ESP32S31_SOUND_BUFFER_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_SOUND_BUFFER_MEMORY must be sram or psram")
endif()

if(NOT ESP32S31_SOUND_BUFFER_BITS MATCHES "^[0-9]+$")
    message(FATAL_ERROR "ESP32S31_SOUND_BUFFER_BITS must be an integer")
endif()
if(ESP32S31_SOUND_BUFFER_BITS LESS 12 OR
   ESP32S31_SOUND_BUFFER_BITS GREATER 16)
    message(FATAL_ERROR
        "ESP32S31_SOUND_BUFFER_BITS must be between 12 and 16")
endif()
math(EXPR ESP32S31_SOUND_BUFFER_BYTES
    "(1 << ${ESP32S31_SOUND_BUFFER_BITS}) * 2")

if(ESP32S31_OBJ_LIST_MEMORY STREQUAL "sram")
    set(ESP32S31_OBJ_LIST_INTERNAL 1)
elseif(ESP32S31_OBJ_LIST_MEMORY STREQUAL "psram")
    set(ESP32S31_OBJ_LIST_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_OBJ_LIST_MEMORY must be sram or psram")
endif()

if(ESP32S31_HOT_HELPERS_MEMORY STREQUAL "sram")
    set(ESP32S31_HOT_HELPERS_INTERNAL 1)
elseif(ESP32S31_HOT_HELPERS_MEMORY STREQUAL "psram")
    set(ESP32S31_HOT_HELPERS_INTERNAL 0)
else()
    message(FATAL_ERROR
        "ESP32S31_HOT_HELPERS_MEMORY must be sram or psram")
endif()
math(EXPR ESP32S31_LCD_BOUNCE_ROW_REMAINDER
    "160 % ${ESP32S31_LCD_BOUNCE_SOURCE_ROWS}")
if(NOT ESP32S31_LCD_BOUNCE_ROW_REMAINDER EQUAL 0)
    message(FATAL_ERROR
        "ESP32S31_LCD_BOUNCE_SOURCE_ROWS must divide 160 exactly")
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
