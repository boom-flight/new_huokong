#ifndef IMU_CALIBRATION_H
#define IMU_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "include/imu_types.h"

#define IMU_CALIBRATION_SAMPLE_COUNT 2000u

typedef enum {
    IMU_CALIBRATION_RESET,
    IMU_CALIBRATION_ACCEPTED,
    IMU_CALIBRATION_COMPLETE,
} imu_calibration_step_t;

typedef struct {
    uint32_t accepted;
    imu_vec3f_t gyro_sum_dps;
    imu_vec3f_t accel_sum_g;
    imu_vec3f_t gyro_bias_dps;
    imu_vec3f_t gravity_g;
    bool complete;
} imu_calibration_t;

void imu_calibration_init(imu_calibration_t *self);
bool imu_calibration_accel_stationary(imu_vec3f_t accel_g);
bool imu_calibration_gyro_stationary(imu_vec3f_t gyro_dps);
imu_calibration_step_t imu_calibration_push(imu_calibration_t *self,
                                             imu_vec3f_t accel_g,
                                             imu_vec3f_t gyro_dps,
                                             bool timestamp_valid);

#endif
