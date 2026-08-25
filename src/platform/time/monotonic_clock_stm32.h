#ifndef MONOTONIC_CLOCK_STM32_H
#define MONOTONIC_CLOCK_STM32_H

#include <stdbool.h>
#include <stdint.h>

bool monotonic_clock_stm32_init(void);
void monotonic_clock_stm32_deinit(void);
uint32_t monotonic_clock_stm32_now_us(void);

#endif
