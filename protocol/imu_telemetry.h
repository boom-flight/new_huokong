#ifndef IMU_TELEMETRY_H
#define IMU_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "include/imu_types.h"

#define IMU_TELEMETRY_FRAME_SIZE 32u
#define IMU_TELEMETRY_VERSION 1u

typedef struct {
    uint32_t timestamp_us;
    uint16_t status;
    imu_vec3f_t euler_deg;
    imu_vec3f_t gyro_dps;
    imu_vec3f_t accel_g;
} imu_telemetry_sample_t;

uint16_t imu_telemetry_crc16_ccitt_false(const uint8_t *data, size_t length);
void imu_telemetry_encode(uint16_t sequence,
                          const imu_telemetry_sample_t *sample,
                          uint8_t frame[IMU_TELEMETRY_FRAME_SIZE]);

#endif
