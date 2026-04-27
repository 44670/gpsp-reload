set(GPSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(GPSP_COMMON "${GPSP_ROOT}/libretro/libretro-common")

if(NOT DEFINED USE_QEMU OR USE_QEMU STREQUAL "")
    set(USE_QEMU 0)
endif()

if(NOT DEFINED USE_DEBUG OR USE_DEBUG STREQUAL "")
    set(USE_DEBUG 0)
endif()

if(NOT DEFINED USE_DYNAREC OR USE_DYNAREC STREQUAL "")
    set(USE_DYNAREC 1)
endif()

if(NOT DEFINED GPSP_TEST_FRAMES OR GPSP_TEST_FRAMES STREQUAL "")
    set(GPSP_TEST_FRAMES "300")
endif()

if(NOT DEFINED GPSP_TEST_EXPECT_FB_HASH OR GPSP_TEST_EXPECT_FB_HASH STREQUAL "")
    set(GPSP_TEST_EXPECT_FB_HASH "0")
endif()

if(NOT DEFINED GPSP_TEST_DUMP_FRAME OR GPSP_TEST_DUMP_FRAME STREQUAL "")
    set(GPSP_TEST_DUMP_FRAME "0")
endif()

if(NOT DEFINED GPSP_PC_SAMPLER OR GPSP_PC_SAMPLER STREQUAL "")
    set(GPSP_PC_SAMPLER 0)
endif()

if(NOT DEFINED GPSP_PC_SAMPLER_MIN_MS OR GPSP_PC_SAMPLER_MIN_MS STREQUAL "")
    set(GPSP_PC_SAMPLER_MIN_MS "50")
endif()

if(NOT DEFINED GPSP_PC_SAMPLER_MAX_MS OR GPSP_PC_SAMPLER_MAX_MS STREQUAL "")
    set(GPSP_PC_SAMPLER_MAX_MS "250")
endif()

if(NOT DEFINED GPSP_PC_SAMPLER_REPORT_MS OR GPSP_PC_SAMPLER_REPORT_MS STREQUAL "")
    set(GPSP_PC_SAMPLER_REPORT_MS "5000")
endif()

if(NOT DEFINED GPSP_PC_SAMPLER_VERBOSE OR GPSP_PC_SAMPLER_VERBOSE STREQUAL "")
    set(GPSP_PC_SAMPLER_VERBOSE 0)
endif()

if(NOT DEFINED GPSP_DISABLE_INTERP OR GPSP_DISABLE_INTERP STREQUAL "")
    set(GPSP_DISABLE_INTERP 0)
endif()

if(GPSP_CORES3SE_LCD)
    set(GPSP_CORES3SE_LCD_VALUE 1)
else()
    set(GPSP_CORES3SE_LCD_VALUE 0)
endif()

if(USE_QEMU)
    set(USE_QEMU_VALUE 1)
else()
    set(USE_QEMU_VALUE 0)
endif()

if(USE_DEBUG)
    set(USE_DEBUG_VALUE 1)
else()
    set(USE_DEBUG_VALUE 0)
endif()

if(USE_DYNAREC)
    set(USE_DYNAREC_VALUE 1)
else()
    set(USE_DYNAREC_VALUE 0)
endif()

if(GPSP_PC_SAMPLER)
    set(GPSP_PC_SAMPLER_VALUE 1)
else()
    set(GPSP_PC_SAMPLER_VALUE 0)
endif()

if(GPSP_PC_SAMPLER_VERBOSE)
    set(GPSP_PC_SAMPLER_VERBOSE_VALUE 1)
else()
    set(GPSP_PC_SAMPLER_VERBOSE_VALUE 0)
endif()

if(GPSP_DISABLE_INTERP)
    set(GPSP_DISABLE_INTERP_VALUE 1)
else()
    set(GPSP_DISABLE_INTERP_VALUE 0)
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

if(GPSP_PC_SAMPLER)
    list(APPEND GPSP_SOURCES
        "${GPSP_ROOT}/esp32s3/pc_sampler_highint5.S"
    )
endif()

if(USE_DYNAREC)
    list(APPEND GPSP_SOURCES
        "${GPSP_ROOT}/cpu_threaded.c"
        "${GPSP_ROOT}/esp32s3/xtensa_runtime.c"
    )
endif()

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
      esp_hw_support
    INCLUDE_DIRS
      "."
      "${GPSP_ROOT}"
      "${GPSP_ROOT}/tests/dhrystone"
      "${GPSP_ROOT}/libretro"
      "${GPSP_COMMON}/include"
)

if(GPSP_PC_SAMPLER)
    set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "-u ld_include_gpsp_pc_sampler_highint5")
endif()

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
    GPSP_TEST_FRAMES=${GPSP_TEST_FRAMES}
    GPSP_TEST_EXPECT_FB_HASH=${GPSP_TEST_EXPECT_FB_HASH}
    GPSP_TEST_DUMP_FRAME=${GPSP_TEST_DUMP_FRAME}
    GPSP_CORES3SE_LCD=${GPSP_CORES3SE_LCD_VALUE}
    GPSP_PC_SAMPLER=${GPSP_PC_SAMPLER_VALUE}
    GPSP_PC_SAMPLER_MIN_MS=${GPSP_PC_SAMPLER_MIN_MS}
    GPSP_PC_SAMPLER_MAX_MS=${GPSP_PC_SAMPLER_MAX_MS}
    GPSP_PC_SAMPLER_REPORT_MS=${GPSP_PC_SAMPLER_REPORT_MS}
    GPSP_PC_SAMPLER_VERBOSE=${GPSP_PC_SAMPLER_VERBOSE_VALUE}
    GPSP_DISABLE_INTERP=${GPSP_DISABLE_INTERP_VALUE}
    USE_QEMU=${USE_QEMU_VALUE}
    USE_DEBUG=${USE_DEBUG_VALUE}
    USE_DYNAREC=${USE_DYNAREC_VALUE}
)

if(USE_DYNAREC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_DYNAREC)
endif()

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
