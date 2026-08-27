/**
 * @file bmi088.h
 * @brief Platform contract for the BMI088 hardware adapter.
 */

#ifndef BMI088_PLATFORM_H
#define BMI088_PLATFORM_H

#include "bmi088/bmi088.h"

#include <stdbool.h>
#include <stdint.h>

#define BMI088_DRDY_EVENT_ACCEL (1u << 0)
#define BMI088_DRDY_EVENT_GYRO  (1u << 1)

typedef struct {
    uint32_t timestamp_us;
    uint32_t sequence;
} bmi088_drdy_latch_t;

typedef void (*bmi088_drdy_notify_fn)(void *context, uint32_t event_mask);

bool bmi088_platform_init(bmi088_drdy_notify_fn notify, void *context);
void bmi088_platform_deinit(void);
bmi088_bus_t bmi088_platform_bus(void);
bmi088_drdy_latch_t bmi088_platform_accel_latch(void);
bmi088_drdy_latch_t bmi088_platform_gyro_latch(void);

#endif
