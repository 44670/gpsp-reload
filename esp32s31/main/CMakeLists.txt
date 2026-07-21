set(GPSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(GPSP_COMMON "${GPSP_ROOT}/libretro/libretro-common")

set(GPSP_CORE_SOURCES
    "${GPSP_ROOT}/main.c"
    "${GPSP_ROOT}/cpu_backend.c"
    "${GPSP_ROOT}/jsjit_backend.c"
    "${GPSP_ROOT}/gba_memory.c"
    "${GPSP_ROOT}/savestate.c"
    "${GPSP_ROOT}/input.c"
    "${GPSP_ROOT}/sound.c"
    "${GPSP_ROOT}/cheats.c"
    "${GPSP_ROOT}/memmap.c"
    "${GPSP_ROOT}/serial.c"
    "${GPSP_ROOT}/gbp.c"
    "${GPSP_ROOT}/rfu.c"
    "${GPSP_ROOT}/serial_proto.c"
    "${GPSP_ROOT}/libretro/libretro.c"
    "${GPSP_ROOT}/gba_cc_lut.c"
    "${GPSP_ROOT}/video.cc"
    "${GPSP_ROOT}/cpu.cc"
)

if(GPSP_ESP32S31_DYNAREC)
    if(CONFIG_SPIRAM_XIP_FROM_PSRAM OR
       CONFIG_SPIRAM_FETCH_INSTRUCTIONS OR
       CONFIG_SPIRAM_RODATA)
        message(FATAL_ERROR
            "ESP32-S31 JIT uses the native RWX PSRAM window; disable IDF's global flash-to-PSRAM relocation so gpSP .text/.rodata stay in SPI flash or SRAM")
    endif()
    list(APPEND GPSP_CORE_SOURCES
        "${GPSP_ROOT}/cpu_threaded.c"
        "${GPSP_ROOT}/riscv/riscv_runtime.c"
    )
    set(GPSP_ESP32S31_DATA_ONLY 0)
else()
    set(GPSP_ESP32S31_DATA_ONLY 1)
endif()

set(GPSP_SOURCES
    "${GPSP_ROOT}/bios_data.S"
    ${GPSP_CORE_SOURCES}
    "${GPSP_COMMON}/compat/compat_posix_string.c"
    "${GPSP_COMMON}/compat/compat_strl.c"
    "${GPSP_COMMON}/compat/fopen_utf8.c"
    "${GPSP_COMMON}/encodings/encoding_utf.c"
    "${GPSP_COMMON}/file/file_path.c"
    "${GPSP_COMMON}/file/file_path_io.c"
    "${GPSP_COMMON}/streams/file_stream.c"
    "${GPSP_COMMON}/string/stdstring.c"
    "${GPSP_COMMON}/time/rtime.c"
    "${GPSP_COMMON}/vfs/vfs_implementation.c"
    "${GPSP_ROOT}/esp32s31/korvo1_lcd.c"
    "${GPSP_ROOT}/esp32s31/korvo1_scaler.c"
    "${GPSP_ROOT}/esp32s31/korvo1_usb_gamepad.c"
    "${GPSP_ROOT}/esp32s31/korvo1_usb_xinput.c"
    "${GPSP_ROOT}/esp32s31/rgb565_scale3x.c"
    "app_main.c"
)

if(GPSP_ESP32S31_MENU_BOOT)
    list(APPEND GPSP_SOURCES
        "${GPSP_ROOT}/esp32s31/game_menu.c"
        "${GPSP_ROOT}/esp32s31/gamepak_runtime.c"
        "${GPSP_ROOT}/esp32s31/gamepak_runtime_plan.c"
        "${GPSP_ROOT}/esp32s31/korvo1_sd.c"
        "${GPSP_ROOT}/esp32s31/menu_ui.c"
    )
else()
    list(APPEND GPSP_SOURCES
        "${GPSP_ROOT}/esp32s31/gamepak_container.c"
    )
endif()

if(GPSP_ESP32S31_DYNAREC)
    list(APPEND GPSP_SOURCES
        "${GPSP_ROOT}/esp32s31/jit_platform.c"
        "${GPSP_ROOT}/esp32s31/uart_debug.c"
    )
    if(GPSP_ESP32S31_PSRAM_FAULT_TRACE)
        list(APPEND GPSP_SOURCES
            "${GPSP_ROOT}/esp32s31/psram_fault_trace.c"
        )
    endif()
endif()

if(GPSP_ESP32S31_PROFILE)
    list(APPEND GPSP_SOURCES
        "${GPSP_ROOT}/esp32s31/gpsp_profile.c"
    )
endif()

set(GPSP_COMPONENT_REQUIRES
    esp_partition
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_uart
    esp_driver_dma
    esp_driver_ppa
    esp_lcd
    esp_mm
    esp_psram
    esp_hw_support
    esp_timer
    heap
    fatfs
    esp_driver_sdmmc
    spi_flash
    usb_host_hid
    vfs
)

if(GPSP_ESP32S31_PSRAM_FAULT_TRACE)
    list(APPEND GPSP_COMPONENT_REQUIRES esp_hal_mspi)
endif()

idf_component_register(
    SRCS ${GPSP_SOURCES}
    INCLUDE_DIRS
      "."
      "${GPSP_ROOT}"
      "${GPSP_ROOT}/libretro"
      "${GPSP_ROOT}/esp32s31"
      "${GPSP_COMMON}/include"
    REQUIRES ${GPSP_COMPONENT_REQUIRES}
)

if(GPSP_ESP32S31_LTO)
    if(NOT CONFIG_COMPILER_LTO_LINKTIME)
        message(FATAL_ERROR
            "GPSP_ESP32S31_LTO requires CONFIG_COMPILER_LTO_LINKTIME")
    endif()
    if(CONFIG_COMPILER_LTO_COMPILETIME)
        message(FATAL_ERROR
            "Global compile-time LTO is disabled; only gpSP may use LTO")
    endif()
    set_source_files_properties(${GPSP_CORE_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-flto=auto")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    RISCV_ARCH
    HAVE_STRINGS_H
    HAVE_STDINT_H
    HAVE_INTTYPES_H
    __LIBRETRO__
    INLINE=inline
    NDEBUG=1
    FRONTEND_SUPPORTS_RGB565
    SMALL_TRANSLATION_CACHE
    GPSP_DIRECT_MMAP_GAMEPAK=1
    GPSP_ESP32S31_DYNAREC=$<BOOL:${GPSP_ESP32S31_DYNAREC}>
    GPSP_ESP32S31_MENU_BOOT=${GPSP_ESP32S31_MENU_BOOT}
    GPSP_ESP32S31_FLASH_SPILL=$<BOOL:${GPSP_ESP32S31_FLASH_SPILL}>
    GPSP_ESP32S31_MENU_AUTOROM_NAME="${GPSP_ESP32S31_MENU_AUTOROM_NAME}"
    ESP32S31_GAMEPAK_STATIC_BYTES=${ESP32S31_GAMEPAK_STATIC_BYTES}
    ESP32S31_LAYOUT_PROBE_PAD_BYTES=${ESP32S31_LAYOUT_PROBE_PAD_BYTES}
    ROM_TRANSLATION_CACHE_SIZE=${ESP32S31_ROM_JIT_CACHE_BYTES}
    RAM_TRANSLATION_CACHE_SIZE=${ESP32S31_RAM_JIT_CACHE_BYTES}
    GPSP_ESP32S31_PROFILE=$<BOOL:${GPSP_ESP32S31_PROFILE}>
    GPSP_ESP32S31_PC_PROFILE=$<BOOL:${GPSP_ESP32S31_PC_PROFILE}>
    GPSP_ESP32S31_PSRAM_FAULT_TRACE=$<BOOL:${GPSP_ESP32S31_PSRAM_FAULT_TRACE}>
    ESP32S31_LCD_BOUNCE_MODE=${ESP32S31_LCD_BOUNCE_MODE}
    ESP32S31_LCD_BOUNCE_SOURCE_ROWS=${ESP32S31_LCD_BOUNCE_SOURCE_ROWS}
    ESP32S31_LCD_RENDER_INTERNAL=${ESP32S31_LCD_RENDER_INTERNAL}
    ESP32S31_IWRAM_INTERNAL=${ESP32S31_IWRAM_INTERNAL}
    ESP32S31_BIOS_INTERNAL=${ESP32S31_BIOS_INTERNAL}
    ESP32S31_COLD_STATE_INTERNAL=${ESP32S31_COLD_STATE_INTERNAL}
    ESP32S31_VRAM_INTERNAL=${ESP32S31_VRAM_INTERNAL}
    ESP32S31_EXECUTE_ARM_INTERNAL=${ESP32S31_EXECUTE_ARM_INTERNAL}
    ESP32S31_VIDEO_HOT_INTERNAL=${ESP32S31_VIDEO_HOT_INTERNAL}
    ESP32S31_MEMORY_MAP_INTERNAL=${ESP32S31_MEMORY_MAP_INTERNAL}
    ESP32S31_SOUND_BUFFER_INTERNAL=${ESP32S31_SOUND_BUFFER_INTERNAL}
    GPSP_SOUND_BUFFER_BITS=${ESP32S31_SOUND_BUFFER_BITS}
    ESP32S31_SOUND_BUFFER_BYTES=${ESP32S31_SOUND_BUFFER_BYTES}
    GPSP_OBJ_PRIORITY_LINKED=1
    ESP32S31_OBJ_LIST_INTERNAL=${ESP32S31_OBJ_LIST_INTERNAL}
    ESP32S31_HOT_HELPERS_INTERNAL=${ESP32S31_HOT_HELPERS_INTERNAL}
    GPSP_EWRAM_DATA_ONLY=${GPSP_ESP32S31_DATA_ONLY}
    GPSP_IWRAM_DATA_ONLY=${GPSP_ESP32S31_DATA_ONLY}
    GPSP_FIXED_CORE_OPTIONS=1
    ESP32S31_SCALER_MODE=${ESP32S31_SCALER_MODE}
    ESP32S31_KORVO1_LCD_COMPAT_18MHZ=$<BOOL:${KORVO1_LCD_COMPAT_18MHZ}>
    ESP32S31_KORVO1_LCD_USE_GPIO38_DISP=$<BOOL:${KORVO1_LCD_GPIO38_DISP}>
)

if(GPSP_ESP32S31_DYNAREC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        HAVE_DYNAREC
        ESP32S31_JIT
        GPSP_GAMEPAK_WRITE_TRACE=1
    )
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-label
    -Wno-unused-parameter
    -Wno-unused-variable
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:ASM>:-Wa,-I,${GPSP_ROOT}>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)
