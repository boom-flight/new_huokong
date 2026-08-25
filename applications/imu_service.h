#ifndef IMU_SERVICE_H
#define IMU_SERVICE_H

#include "attitude/imu_types.h"

#include <stdbool.h>
#include <stdint.h>

#define IMU_THREAD_PRIORITY 5u
#define IMU_THREAD_STACK_SIZE 768u

enum {
    IMU_STATUS_VALID = 1u << 0,
    IMU_STATUS_CALIBRATING = 1u << 1,
    IMU_STATUS_BMI_INIT_FAILED = 1u << 2,
    IMU_STATUS_GYRO_SATURATED = 1u << 3,
    IMU_STATUS_ACCEL_CORRECTION_INVALID = 1u << 4,
    IMU_STATUS_TIMESTAMP_INVALID = 1u << 5,
    IMU_STATUS_SPI_ERROR = 1u << 6,
    IMU_STATUS_EVENT_OVERRUN = 1u << 7,
    IMU_STATUS_TELEMETRY_DROPPED = 1u << 8
};

typedef enum {
    IMU_INITIALIZING,
    IMU_CALIBRATING,
    IMU_RUNNING,
    IMU_FAULT_RETRY
} imu_state_t;

typedef struct {
    uint32_t accel_samples;
    uint32_t gyro_samples;
    uint32_t accel_overruns;
    uint32_t gyro_overruns;
    uint32_t spi_errors;
    uint32_t rejected_dt;
    uint32_t long_gaps;
    uint32_t sensor_reinitializations;
    uint32_t telemetry_drops;
} imu_diagnostics_t;

typedef struct {
    uint32_t timestamp_us;
    uint16_t status;
    imu_quatf_t quaternion;
    imu_vec3f_t euler_deg;
    imu_vec3f_t gyro_dps;
    imu_vec3f_t accel_g;
    imu_diagnostics_t diagnostics;
} imu_snapshot_t;

bool imu_service_init(void);
bool imu_snapshot_read(imu_snapshot_t *out);
void imu_service_record_telemetry_drop(void);

#endif
