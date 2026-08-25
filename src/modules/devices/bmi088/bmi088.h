#ifndef BMI088_H
#define BMI088_H

#include "attitude/imu_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BMI088_ACCEL,
    BMI088_GYRO
} bmi088_target_t;

typedef bool (*bmi088_read_fn)(void *context, bmi088_target_t target,
                              uint8_t reg, uint8_t *data, size_t length);
typedef bool (*bmi088_write_fn)(void *context, bmi088_target_t target,
                               uint8_t reg, uint8_t value);
typedef void (*bmi088_delay_ms_fn)(void *context, uint32_t delay_ms);

typedef struct {
    void *context;
    bmi088_read_fn read;
    bmi088_write_fn write;
    bmi088_delay_ms_fn delay_ms;
} bmi088_bus_t;

typedef struct {
    uint8_t source_axis[3];
    int8_t sign[3];
} bmi088_axis_map_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} bmi088_raw_sample_t;

typedef enum {
    BMI088_OK = 0,
    BMI088_BAD_ARGUMENT,
    BMI088_BUS_ERROR,
    BMI088_BAD_ID,
    BMI088_VERIFY_ERROR,
    BMI088_BAD_AXIS_MAP
} bmi088_result_t;

typedef struct {
    bmi088_bus_t bus;
    bmi088_axis_map_t axis_map;
} bmi088_t;

extern const bmi088_axis_map_t BMI088_AXIS_MAP;

bool bmi088_axis_map_is_right_handed(const bmi088_axis_map_t *map);
bmi088_result_t bmi088_init(bmi088_t *self, bmi088_bus_t bus,
                            bmi088_axis_map_t axis_map, uint8_t *accel_id,
                            uint8_t *gyro_id);
bmi088_result_t bmi088_read_accel(bmi088_t *self, bmi088_raw_sample_t *raw,
                                  imu_vec3f_t *body_g);
bmi088_result_t bmi088_read_gyro(bmi088_t *self, bmi088_raw_sample_t *raw,
                                 imu_vec3f_t *body_dps);

#endif
