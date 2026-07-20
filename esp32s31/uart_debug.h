#ifndef ESP32S31_UART_DEBUG_H
#define ESP32S31_UART_DEBUG_H

#include <stdbool.h>
#include <stdint.h>

/* The debugger is polled between retro_run() calls, so UART input can never
 * interrupt generated code or observe a half-published guest register set. */
void esp32s31_uart_debug_init(void);
void esp32s31_uart_debug_poll(void);
bool esp32s31_uart_debug_should_run_frame(void);
void esp32s31_uart_debug_frame_complete(void);
uint16_t esp32s31_uart_debug_joypad_mask(void);

#endif
