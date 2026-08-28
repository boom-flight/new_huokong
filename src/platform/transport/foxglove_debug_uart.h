#ifndef FOXGLOVE_DEBUG_UART_H
#define FOXGLOVE_DEBUG_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FOXGLOVE_DEBUG_UART_FRAME_SIZE 104u

typedef enum {
    FOXGLOVE_DEBUG_UART_OK,
    FOXGLOVE_DEBUG_UART_FAILED,
} foxglove_debug_uart_result_t;

bool foxglove_debug_uart_init(void);
void foxglove_debug_uart_deinit(void);
foxglove_debug_uart_result_t foxglove_debug_uart_send(const uint8_t *frame,
                                                      size_t length);

#endif
