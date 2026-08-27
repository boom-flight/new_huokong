/**
 * @file monotonic_clock.h
 * @brief Platform contract for a monotonic microsecond clock.
 */

#ifndef MONOTONIC_CLOCK_H
#define MONOTONIC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

bool monotonic_clock_init(void);
void monotonic_clock_deinit(void);
uint32_t monotonic_clock_now_us(void);

#endif
