set(GPSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(GPSP_COMMON "${GPSP_ROOT}/libretro/libretro-common")

if(NOT DEFINED GPSP_TEST_FRAMES OR GPSP_TEST_FRAMES STREQUAL "")
    set(GPSP_TEST_FRAMES "300")
endif()

if(NOT DEFINED GPSP_TEST_EXPECT_FB_HASH OR GPSP_TEST_EXPECT_FB_HASH STREQUAL "")
    set(GPSP_TEST_EXPECT_FB_HASH "0")
endif()

if(NOT DEFINED GPSP_TEST_DUMP_FRAME OR GPSP_TEST_DUMP_FRAME STREQUAL "")
    set(GPSP_TEST_DUMP_FRAME "0")
endif()

if(GPSP_CORES3SE_LCD)
    set(GPSP_CORES3SE_LCD_VALUE 1)
else()
    set(GPSP_CORES3SE_LCD_VALUE 0)
endif()

set(GPSP_SOURCES
    "${GPSP_ROOT}/bios_data.S"
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
    "${GPSP_ROOT}/esp32s3/cores3se_lcd.c"
    "${GPSP_ROOT}/esp32s3/psram_static.c"
    "${GPSP_ROOT}/video.cc"
    "${GPSP_ROOT}/cpu.cc"
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
    "app_main.c"
)

list(APPEND GPSP_SOURCES
    "${GPSP_ROOT}/cpu_threaded.c"
    "${GPSP_ROOT}/esp32s3/xtensa_runtime.c"
)

idf_component_register(
    SRCS ${GPSP_SOURCES}
    REQUIRES
      esp_partition
      esp_driver_gpio
      esp_driver_i2c
      esp_driver_spi
      esp_lcd
      esp_mm
      esp_psram
    INCLUDE_DIRS
      "."
      "${GPSP_ROOT}"
      "${GPSP_ROOT}/tests/dhrystone"
      "${GPSP_ROOT}/libretro"
      "${GPSP_COMMON}/include"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    XTENSA_ARCH
    HAVE_STRINGS_H
    HAVE_STDINT_H
    HAVE_INTTYPES_H
    __LIBRETRO__
    INLINE=inline
    NDEBUG=1
    FRONTEND_SUPPORTS_RGB565
    SMALL_TRANSLATION_CACHE
    ROM_BUFFER_SIZE=1
    HAVE_DYNAREC
    GPSP_TEST_FRAMES=${GPSP_TEST_FRAMES}
    GPSP_TEST_EXPECT_FB_HASH=${GPSP_TEST_EXPECT_FB_HASH}
    GPSP_TEST_DUMP_FRAME=${GPSP_TEST_DUMP_FRAME}
    GPSP_CORES3SE_LCD=${GPSP_CORES3SE_LCD_VALUE}
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-variable
    -Wno-unused-label
    -Wno-unused-parameter
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:ASM>:-Wa,-I,${GPSP_ROOT}>
)

target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
