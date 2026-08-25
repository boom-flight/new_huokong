#ifndef BMI088_PORT_H
#define BMI088_PORT_H

#include "bmi088.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BMI088_EVENT_ACCEL (1u << 0)
#define BMI088_EVENT_GYRO (1u << 1)

struct rt_event;

typedef struct {
    uint32_t timestamp_us;
    uint32_t sequence;
} bmi088_drdy_latch_t;

bool bmi088_port_init(struct rt_event *event);
void bmi088_port_deinit(void);
bmi088_bus_t bmi088_port_bus(void);
bmi088_drdy_latch_t bmi088_port_accel_latch(void);
bmi088_drdy_latch_t bmi088_port_gyro_latch(void);
uint32_t bmi088_port_timestamp_us(void);
bool bmi088_port_telemetry_try_start(const uint8_t *frame, size_t length);
bool bmi088_port_telemetry_busy(void);
bool bmi088_port_telemetry_take_failure(void);

#endif
