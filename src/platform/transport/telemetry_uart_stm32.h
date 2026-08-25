#ifndef TELEMETRY_UART_STM32_H
#define TELEMETRY_UART_STM32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool telemetry_uart_stm32_init(void);
void telemetry_uart_stm32_deinit(void);
bool telemetry_uart_stm32_try_start(const uint8_t *frame, size_t length);
bool telemetry_uart_stm32_busy(void);
bool telemetry_uart_stm32_take_failure(void);

#endif
