#ifndef IMU_SERVICE_LOGIC_H
#define IMU_SERVICE_LOGIC_H

#include "imu_service.h"
#include "attitude/imu_calibration.h"
#include "attitude/mahony.h"
#include "bmi088/bmi088.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IMU_DT_INTEGRATE,
    IMU_DT_SKIP_KEEP_VALID,
    IMU_DT_SKIP_INVALIDATE
} imu_dt_action_t;

typedef struct {
    bool rejected_dt;
    bool long_gap;
    bool update_succeeded;
} imu_running_gyro_result_t;

typedef enum {
    IMU_CALIBRATION_ADMISSION_ACCEPT,
    IMU_CALIBRATION_ADMISSION_SKIP_PENDING_ACCEL,
    IMU_CALIBRATION_ADMISSION_RESET,
} imu_calibration_admission_t;

typedef struct {
    uint16_t next_sequence;
    uint32_t drops;
    bool drop_sticky;
} telemetry_attempt_state_t;

typedef struct {
    uint16_t duration_ms;
    uint8_t next_phase;
    bool on;
} imu_led_step_t;

imu_dt_action_t imu_classify_gyro_delta_us(uint32_t delta_us);
void imu_accept_new_gyro_sample(uint32_t timestamp_us,
                                uint32_t *last_gyro_timestamp_us,
                                bool *last_gyro_timestamp_valid,
                                bool *gyro_expired);
uint16_t imu_gyro_timestamp_condition(bool had_baseline,
                                      imu_dt_action_t action);
imu_running_gyro_result_t imu_apply_running_gyro_timing(
    mahony_t *estimator,
    bool had_baseline,
    imu_dt_action_t action,
    imu_vec3f_t gyro_rad_s,
    imu_vec3f_t accel_g,
    bool correction_valid,
    float dt_s);
uint32_t imu_gyro_expiry_delay_us(bool has_gyro_timestamp,
                                  uint32_t now_us,
                                  uint32_t last_gyro_timestamp_us);
bool imu_accel_correction_valid(imu_vec3f_t newest_accel_g,
                                uint32_t newest_accel_sequence,
                                uint32_t consumed_accel_sequence,
                                uint32_t newest_accel_timestamp_us,
                                uint32_t gyro_timestamp_us);
imu_calibration_admission_t imu_calibration_admission(
    bool has_gyro_baseline,
    imu_dt_action_t dt_action,
    bool gyro_overrun,
    bool has_accel_sample,
    imu_vec3f_t gyro_dps,
    imu_vec3f_t consumed_accel_g,
    uint32_t newest_accel_sequence,
    uint32_t consumed_accel_sequence,
    uint32_t consumed_accel_timestamp_us,
    uint32_t gyro_timestamp_us);
bool imu_calibration_apply_admission(
    imu_calibration_t *calibration,
    imu_calibration_admission_t admission,
    imu_vec3f_t accel_g,
    imu_vec3f_t gyro_dps,
    imu_calibration_step_t *step);
bool imu_gyro_saturated(bmi088_raw_sample_t raw);
imu_vec3f_t imu_apply_gyro_bias(imu_vec3f_t measured, imu_vec3f_t bias);
uint16_t imu_service_status(imu_state_t state,
                            bool estimator_valid,
                            uint16_t state_conditions,
                            uint16_t accel_conditions,
                            uint16_t gyro_conditions);
bool imu_gyro_expiry_transition(imu_state_t state,
                                bool gyro_pending,
                                uint32_t now_us,
                                bool has_last_gyro_timestamp,
                                uint32_t last_gyro_timestamp_us,
                                bool *gyro_expired,
                                uint16_t *gyro_conditions,
                                mahony_t *estimator);
imu_led_step_t imu_led_step(imu_state_t state, uint8_t phase);
uint32_t imu_housekeeping_wait_ticks(uint32_t now,
                                     uint32_t led_deadline,
                                     uint32_t diagnostics_deadline);
uint16_t telemetry_attempt_begin(telemetry_attempt_state_t *state);
void telemetry_attempt_dropped(telemetry_attempt_state_t *state);
void telemetry_attempt_queued(telemetry_attempt_state_t *state);

#endif
