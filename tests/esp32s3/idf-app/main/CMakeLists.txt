set(GPSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(GPSP_COMMON "${GPSP_ROOT}/libretro/libretro-common")

if(NOT DEFINED GPSP_TEST_ROM OR GPSP_TEST_ROM STREQUAL "")
    set(GPSP_TEST_ROM "${GPSP_ROOT}/tests/dhrystone/dhrystone_arm.gba")
endif()

if(NOT DEFINED GPSP_TEST_MODE OR GPSP_TEST_MODE STREQUAL "")
    set(GPSP_TEST_MODE "dhrystone")
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

if(NOT EXISTS "${GPSP_TEST_ROM}")
    message(FATAL_ERROR "GPSP_TEST_ROM does not exist: ${GPSP_TEST_ROM}")
endif()

set(GPSP_TEST_ROM_EMBED "${CMAKE_CURRENT_BINARY_DIR}/test_rom.gba")
configure_file("${GPSP_TEST_ROM}" "${GPSP_TEST_ROM_EMBED}" COPYONLY)

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
    "${GPSP_ROOT}/cpu_threaded.c"
    "${GPSP_ROOT}/esp32s3/psram_static.c"
    "${GPSP_ROOT}/esp32s3/xtensa_runtime.c"
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

idf_component_register(
    SRCS ${GPSP_SOURCES}
    EMBED_FILES "${GPSP_TEST_ROM_EMBED}"
    REQUIRES esp_mm esp_psram
    INCLUDE_DIRS
      "."
      "${GPSP_ROOT}"
      "${GPSP_ROOT}/tests/dhrystone"
      "${GPSP_ROOT}/libretro"
      "${GPSP_COMMON}/include"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HAVE_DYNAREC
    XTENSA_ARCH
    HAVE_STRINGS_H
    HAVE_STDINT_H
    HAVE_INTTYPES_H
    __LIBRETRO__
    INLINE=inline
    NDEBUG=1
    FRONTEND_SUPPORTS_RGB565
    # Keep the ESP32-S3 QEMU test fast; production should use SMALL_TRANSLATION_CACHE.
    ROM_TRANSLATION_CACHE_SIZE=8192
    RAM_TRANSLATION_CACHE_SIZE=24576
    ROM_BUFFER_SIZE=1
    GPSP_TEST_BACKEND=\"${GPSP_TEST_BACKEND}\"
    GPSP_TEST_MODE=\"${GPSP_TEST_MODE}\"
    GPSP_TEST_FRAMES=${GPSP_TEST_FRAMES}
    GPSP_TEST_EXPECT_FB_HASH=${GPSP_TEST_EXPECT_FB_HASH}
    GPSP_TEST_DUMP_FRAME=${GPSP_TEST_DUMP_FRAME}
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
