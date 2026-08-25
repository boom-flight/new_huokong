#include "imu_service_logic.h"

#include <math.h>
#include <stdlib.h>

imu_dt_action_t imu_classify_gyro_delta_us(uint32_t delta_us)
{
    if (delta_us >= 500u && delta_us <= 2000u) {
        return IMU_DT_INTEGRATE;
    }
    if (delta_us <= 20000u) {
        return IMU_DT_SKIP_KEEP_VALID;
    }
    return IMU_DT_SKIP_INVALIDATE;
}

void imu_accept_new_gyro_sample(uint32_t timestamp_us,
                                uint32_t *last_gyro_timestamp_us,
                                bool *last_gyro_timestamp_valid,
                                bool *gyro_expired)
{
    *last_gyro_timestamp_us = timestamp_us;
    *last_gyro_timestamp_valid = true;
    *gyro_expired = false;
}

uint16_t imu_gyro_timestamp_condition(bool had_baseline,
                                      imu_dt_action_t action)
{
    return !had_baseline || action != IMU_DT_INTEGRATE
               ? IMU_STATUS_TIMESTAMP_INVALID
               : 0u;
}

imu_running_gyro_result_t imu_apply_running_gyro_timing(
    mahony_t *estimator,
    bool had_baseline,
    imu_dt_action_t action,
    imu_vec3f_t gyro_rad_s,
    imu_vec3f_t accel_g,
    bool correction_valid,
    float dt_s)
{
    imu_running_gyro_result_t result = {0};

    if (!had_baseline) {
        return result;
    }
    if (action == IMU_DT_SKIP_KEEP_VALID) {
        result.rejected_dt = true;
        return result;
    }
    if (action == IMU_DT_SKIP_INVALIDATE) {
        result.rejected_dt = true;
        result.long_gap = true;
        mahony_invalidate(estimator);
        return result;
    }

    result.update_succeeded = mahony_update(
        estimator, gyro_rad_s, accel_g, correction_valid, dt_s);
    return result;
}

uint32_t imu_gyro_expiry_delay_us(bool has_gyro_timestamp,
                                  uint32_t now_us,
                                  uint32_t last_gyro_timestamp_us)
{
    const uint32_t expiry_age_us = 20001u;
    uint32_t age_us;

    if (!has_gyro_timestamp) {
        return UINT32_MAX;
    }
    age_us = (uint32_t)(now_us - last_gyro_timestamp_us);
    return age_us >= expiry_age_us ? 0u : expiry_age_us - age_us;
}

bool imu_accel_correction_valid(imu_vec3f_t newest_accel_g,
                                uint32_t newest_accel_sequence,
                                uint32_t consumed_accel_sequence,
                                uint32_t newest_accel_timestamp_us,
                                uint32_t gyro_timestamp_us)
{
    const float minimum_norm2 = 0.7f * 0.7f;
    const float maximum_norm2 = 1.3f * 1.3f;
    const float norm2 = newest_accel_g.x * newest_accel_g.x +
                        newest_accel_g.y * newest_accel_g.y +
                        newest_accel_g.z * newest_accel_g.z;

    return newest_accel_sequence == consumed_accel_sequence &&
           (uint32_t)(gyro_timestamp_us - newest_accel_timestamp_us) <= 5000u &&
           isfinite(norm2) && norm2 >= minimum_norm2 &&
           norm2 <= maximum_norm2;
}

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
    uint32_t gyro_timestamp_us)
{
    if (!has_gyro_baseline || dt_action != IMU_DT_INTEGRATE || gyro_overrun) {
        return IMU_CALIBRATION_ADMISSION_RESET;
    }
    if (!has_accel_sample || !imu_calibration_gyro_stationary(gyro_dps) ||
        !imu_calibration_accel_stationary(consumed_accel_g) ||
        (uint32_t)(gyro_timestamp_us - consumed_accel_timestamp_us) > 5000u) {
        return IMU_CALIBRATION_ADMISSION_RESET;
    }
    if (newest_accel_sequence != consumed_accel_sequence) {
        return IMU_CALIBRATION_ADMISSION_SKIP_PENDING_ACCEL;
    }
    return IMU_CALIBRATION_ADMISSION_ACCEPT;
}

bool imu_calibration_apply_admission(
    imu_calibration_t *calibration,
    imu_calibration_admission_t admission,
    imu_vec3f_t accel_g,
    imu_vec3f_t gyro_dps,
    imu_calibration_step_t *step)
{
    if (admission == IMU_CALIBRATION_ADMISSION_SKIP_PENDING_ACCEL) {
        return false;
    }
    *step = imu_calibration_push(
        calibration, accel_g, gyro_dps,
        admission == IMU_CALIBRATION_ADMISSION_ACCEPT);
    return true;
}

bool imu_gyro_saturated(bmi088_raw_sample_t raw)
{
    const int32_t limit = 31130;

    return abs((int32_t)raw.x) >= limit ||
           abs((int32_t)raw.y) >= limit ||
           abs((int32_t)raw.z) >= limit;
}

imu_vec3f_t imu_apply_gyro_bias(imu_vec3f_t measured, imu_vec3f_t bias)
{
    return (imu_vec3f_t){
        measured.x - bias.x,
        measured.y - bias.y,
        measured.z - bias.z,
    };
}

uint16_t imu_service_status(imu_state_t state,
                            bool estimator_valid,
                            uint16_t state_conditions,
                            uint16_t accel_conditions,
                            uint16_t gyro_conditions)
{
    uint16_t status =
        (uint16_t)(state_conditions | accel_conditions | gyro_conditions);

    if (state == IMU_CALIBRATING) {
        status |= IMU_STATUS_CALIBRATING;
    }
    if (state == IMU_RUNNING && estimator_valid &&
        ((accel_conditions | gyro_conditions) & IMU_STATUS_SPI_ERROR) == 0u) {
        status |= IMU_STATUS_VALID;
    }
    return status;
}

bool imu_gyro_expiry_transition(imu_state_t state,
                                bool gyro_pending,
                                uint32_t now_us,
                                bool has_last_gyro_timestamp,
                                uint32_t last_gyro_timestamp_us,
                                bool *gyro_expired,
                                uint16_t *gyro_conditions,
                                mahony_t *estimator)
{
    bool publish_required;

    if (state != IMU_RUNNING || gyro_pending || *gyro_expired ||
        imu_gyro_expiry_delay_us(has_last_gyro_timestamp, now_us,
                                 last_gyro_timestamp_us) != 0u) {
        return false;
    }

    publish_required = estimator->valid ||
        (*gyro_conditions & IMU_STATUS_TIMESTAMP_INVALID) == 0u;
    *gyro_expired = true;
    *gyro_conditions |= IMU_STATUS_TIMESTAMP_INVALID;
    mahony_invalidate(estimator);
    return publish_required;
}

imu_led_step_t imu_led_step(imu_state_t state, uint8_t phase)
{
    if (state == IMU_FAULT_RETRY) {
        static const imu_led_step_t steps[] = {
            {100u, 1u, true},
            {100u, 2u, false},
            {100u, 3u, true},
            {700u, 0u, false},
        };

        return steps[phase < 4u ? phase : 0u];
    }

    if (phase != 0u) {
        return (imu_led_step_t){
            .duration_ms = state == IMU_CALIBRATING ? 100u
                           : state == IMU_RUNNING ? 1900u
                                                  : 900u,
            .next_phase = 0u,
            .on = false,
        };
    }
    return (imu_led_step_t){100u, 1u, true};
}

uint32_t imu_housekeeping_wait_ticks(uint32_t now,
                                     uint32_t led_deadline,
                                     uint32_t diagnostics_deadline)
{
    const uint32_t led_wait =
        (int32_t)(now - led_deadline) >= 0 ? 0u : led_deadline - now;
    const uint32_t diagnostics_wait =
        (int32_t)(now - diagnostics_deadline) >= 0
            ? 0u
            : diagnostics_deadline - now;

    return led_wait < diagnostics_wait ? led_wait : diagnostics_wait;
}

uint16_t telemetry_attempt_begin(telemetry_attempt_state_t *state)
{
    const uint16_t sequence = state->next_sequence;

    state->next_sequence = (uint16_t)(sequence + 1u);
    return sequence;
}

void telemetry_attempt_dropped(telemetry_attempt_state_t *state)
{
    ++state->drops;
    state->drop_sticky = true;
}

void telemetry_attempt_queued(telemetry_attempt_state_t *state)
{
    state->drop_sticky = false;
}
