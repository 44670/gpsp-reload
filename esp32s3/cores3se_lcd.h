/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef ESP32S3_CORES3SE_LCD_H
#define ESP32S3_CORES3SE_LCD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool esp32s3_cores3se_lcd_init(void);
bool esp32s3_cores3se_lcd_ready(void);
bool esp32s3_cores3se_lcd_present_rgb565(const void *pixels,
                                          unsigned width,
                                          unsigned height,
                                          size_t pitch);

#endif
