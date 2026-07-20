#ifndef GPSP_ESP32S31_GAME_MENU_H
#define GPSP_ESP32S31_GAME_MENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32S31_GAME_MENU_PATH_BYTES 512u

/* Blocks until a .gba file is chosen. The TF card must already be mounted. */
bool esp32s31_game_menu_select_rom(char *path, size_t path_bytes);

void esp32s31_game_menu_show_progress(const char *stage,
                                      const char *path,
                                      uint32_t completed,
                                      uint32_t total,
                                      const char *detail);

void esp32s31_game_menu_show_error(const char *message,
                                   const char *detail);

#endif
