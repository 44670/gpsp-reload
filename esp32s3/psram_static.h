/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef ESP32S3_PSRAM_STATIC_H
#define ESP32S3_PSRAM_STATIC_H

#include <stdbool.h>
#include <stddef.h>

#include "common.h"

#define ESP32S3_STATIC_AUDIO_FRAMES 2048u

#if defined(HAVE_DYNAREC)
bool esp32s3_static_buffers_init_translation_caches(u8 **rom_cache,
                                                    u8 **ram_cache);
const char *esp32s3_static_buffers_last_error(void);
#endif
u16 *esp32s3_static_screen_pixels(void);
u16 *esp32s3_static_processed_pixels(void);
u16 *esp32s3_static_previous_pixels(void);
s16 *esp32s3_static_audio_sample_buffer(size_t required_bytes);
size_t esp32s3_static_audio_sample_buffer_size(void);

#if defined(HAVE_DYNAREC)
bool esp32s3_jit_data_range_is_static(const void *data_start, size_t size);
u8 *esp32s3_jit_data_to_exec(const void *data_ptr);
u8 *esp32s3_jit_exec_to_data(const void *exec_ptr);
void esp32s3_jit_cache_sync(void *data_start, void *data_end);
#endif

#endif
