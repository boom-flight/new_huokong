#include "imu_service_logic.h"
#include "imu_service.h"
#include "attitude/imu_calibration.h"
#include "attitude/mahony.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static void test_dt_classification_has_inclusive_integration_boundaries(void)
{
    assert(imu_classify_gyro_delta_us(0u) == IMU_DT_SKIP_KEEP_VALID);
    assert(imu_classify_gyro_delta_us(499u) == IMU_DT_SKIP_KEEP_VALID);
    assert(imu_classify_gyro_delta_us(500u) == IMU_DT_INTEGRATE);
    assert(imu_classify_gyro_delta_us(2000u) == IMU_DT_INTEGRATE);
    assert(imu_classify_gyro_delta_us(2001u) == IMU_DT_SKIP_KEEP_VALID);
    assert(imu_classify_gyro_delta_us(20000u) == IMU_DT_SKIP_KEEP_VALID);
    assert(imu_classify_gyro_delta_us(20001u) == IMU_DT_SKIP_INVALIDATE);
    assert(imu_classify_gyro_delta_us(UINT32_MAX) == IMU_DT_SKIP_INVALIDATE);
}

static void test_accel_correction_requires_the_newest_consumed_sequence(void)
{
    const imu_vec3f_t one_g = {0.0f, 0.0f, 1.0f};

    assert(imu_accel_correction_valid(one_g, 7u, 7u, 1000u, 6000u));
    assert(!imu_accel_correction_valid(one_g, 8u, 7u, 1000u, 1500u));
}

static void test_accel_correction_uses_inclusive_norm_and_freshness_gates(void)
{
    assert(imu_accel_correction_valid((imu_vec3f_t){0.7f, 0.0f, 0.0f},
                                      8u, 8u, 1000u, 6000u));
    assert(imu_accel_correction_valid((imu_vec3f_t){1.3f, 0.0f, 0.0f},
                                      8u, 8u, 1000u, 6000u));
    assert(!imu_accel_correction_valid((imu_vec3f_t){0.699f, 0.0f, 0.0f},
                                       8u, 8u, 1000u, 1500u));
    assert(!imu_accel_correction_valid((imu_vec3f_t){0.0f, 0.0f, 1.31f},
                                       8u, 8u, 1000u, 1500u));
    assert(!imu_accel_correction_valid((imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                       8u, 8u, 1000u, 6001u));
    assert(!imu_accel_correction_valid((imu_vec3f_t){NAN, 0.0f, 1.0f},
                                       8u, 8u, 1000u, 1500u));
    assert(!imu_accel_correction_valid((imu_vec3f_t){0.0f, 0.0f, 0.0f},
                                       8u, 8u, 1000u, 1500u));
}

static void test_gyro_saturation_starts_at_raw_magnitude_31130(void)
{
    assert(!imu_gyro_saturated((bmi088_raw_sample_t){31129, -31129, 0}));
    assert(imu_gyro_saturated((bmi088_raw_sample_t){31130, 0, 0}));
    assert(imu_gyro_saturated((bmi088_raw_sample_t){0, -31130, 0}));
    assert(imu_gyro_saturated((bmi088_raw_sample_t){0, 0, INT16_MIN}));
}

static void test_gyro_bias_equal_to_measurement_gives_exact_zero(void)
{
    const imu_vec3f_t measured = {1.5f, -2.25f, 0.125f};
    const imu_vec3f_t corrected = imu_apply_gyro_bias(measured, measured);

    assert(corrected.x == 0.0f);
    assert(corrected.y == 0.0f);
    assert(corrected.z == 0.0f);
}

static void test_gyro_bias_subtracts_arbitrary_signed_vectors_component_wise(void)
{
    const imu_vec3f_t corrected = imu_apply_gyro_bias(
        (imu_vec3f_t){-3.5f, 4.0f, -0.25f},
        (imu_vec3f_t){1.5f, -2.0f, -0.75f});

    assert(corrected.x == -5.0f);
    assert(corrected.y == 6.0f);
    assert(corrected.z == 0.5f);
}

static void test_calibration_admission_accepts_a_coherent_sample(void)
{
    const imu_vec3f_t one_g = {0.0f, 0.0f, 1.0f};
    const imu_vec3f_t stationary_gyro = {0.5f, -0.25f, 0.125f};

    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, false, true,
                                     stationary_gyro, one_g, 12u, 12u,
                                     1000u, 6000u) ==
           IMU_CALIBRATION_ADMISSION_ACCEPT);
}

static void test_calibration_admission_skips_a_pending_accel(void)
{
    const imu_vec3f_t one_g = {0.0f, 0.0f, 1.0f};
    const imu_vec3f_t stationary_gyro = {0.5f, -0.25f, 0.125f};

    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, false, true,
                                     stationary_gyro, one_g, 13u, 12u,
                                     1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_SKIP_PENDING_ACCEL);
}

static void test_calibration_admission_gives_gyro_failures_reset_precedence(void)
{
    const imu_vec3f_t one_g = {0.0f, 0.0f, 1.0f};
    const imu_vec3f_t stationary_gyro = {0.5f, -0.25f, 0.125f};

    assert(imu_calibration_admission(false, IMU_DT_INTEGRATE, false, true,
                                     stationary_gyro, one_g, 13u, 12u,
                                     1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_admission(true, IMU_DT_SKIP_KEEP_VALID, false, true,
                                     stationary_gyro, one_g, 13u, 12u,
                                     1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_admission(true, IMU_DT_SKIP_INVALIDATE, false, true,
                                     stationary_gyro, one_g, 13u, 12u,
                                     1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, true, true,
                                     stationary_gyro, one_g, 13u, 12u,
                                     1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
}

static void test_pending_accel_cannot_mask_observable_sample_failures(void)
{
    const imu_vec3f_t one_g = {0.0f, 0.0f, 1.0f};
    const imu_vec3f_t stationary_gyro = {0.5f, -0.25f, 0.125f};

    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, false, false,
                                     stationary_gyro, one_g, 13u, 12u,
                                     1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, false, true,
                                     (imu_vec3f_t){3.0f, 0.0f, 0.0f},
                                     one_g, 13u, 12u, 1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, false, true,
                                     (imu_vec3f_t){NAN, 0.0f, 0.0f},
                                     one_g, 13u, 12u, 1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, false, true,
                                     stationary_gyro,
                                     (imu_vec3f_t){NAN, 0.0f, 1.0f},
                                     13u, 12u, 1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, false, true,
                                     stationary_gyro,
                                     (imu_vec3f_t){0.8f, 0.0f, 0.0f},
                                     13u, 12u, 1000u, 1500u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_admission(true, IMU_DT_INTEGRATE, false, true,
                                     stationary_gyro, one_g, 13u, 12u,
                                     1000u, 6001u) ==
           IMU_CALIBRATION_ADMISSION_RESET);
}

static void test_production_admission_application_interleaves_skip_and_accept(void)
{
    imu_calibration_t calibration;
    imu_calibration_step_t step = IMU_CALIBRATION_RESET;

    imu_calibration_init(&calibration);
    for (uint32_t i = 0u; i < IMU_CALIBRATION_SAMPLE_COUNT; ++i) {
        const imu_vec3f_t accel = {
            0.0f, 0.0f, (i & 1u) == 0u ? 0.9f : 1.1f};
        const imu_vec3f_t gyro = {
            (i & 1u) == 0u ? 0.5f : 1.5f, 0.0f, 0.0f};
        const uint32_t consumed_sequence = i + 1u;
        const uint32_t pending_sequence = consumed_sequence + 1u;
        const uint32_t accel_timestamp_us = i * 1000u;
        const uint32_t gyro_timestamp_us = accel_timestamp_us + 1000u;
        const imu_calibration_admission_t pending = imu_calibration_admission(
            true, IMU_DT_INTEGRATE, false, true, gyro, accel,
            pending_sequence, consumed_sequence, accel_timestamp_us,
            gyro_timestamp_us);
        const imu_calibration_t before_skip = calibration;

        assert(pending == IMU_CALIBRATION_ADMISSION_SKIP_PENDING_ACCEL);
        assert(!imu_calibration_apply_admission(
            &calibration, pending, accel, gyro, &step));
        assert(memcmp(&calibration, &before_skip, sizeof calibration) == 0);

        const imu_calibration_admission_t coherent = imu_calibration_admission(
            true, IMU_DT_INTEGRATE, false, true, gyro, accel,
            pending_sequence, pending_sequence, accel_timestamp_us,
            gyro_timestamp_us);
        assert(coherent == IMU_CALIBRATION_ADMISSION_ACCEPT);
        assert(imu_calibration_apply_admission(
            &calibration, coherent, accel, gyro, &step));
        assert(calibration.accepted == i + 1u);
    }

    assert(step == IMU_CALIBRATION_COMPLETE);
    assert(calibration.complete);
    assert(calibration.accepted == IMU_CALIBRATION_SAMPLE_COUNT);
    assert(calibration.gyro_bias_dps.x == 1.0f);
    assert(calibration.gravity_g.z == 1.0f);

    const imu_vec3f_t corrected = imu_apply_gyro_bias(
        (imu_vec3f_t){1.5f, 0.0f, 0.0f}, calibration.gyro_bias_dps);
    assert(corrected.x == 0.5f);
    assert(corrected.y == 0.0f);
    assert(corrected.z == 0.0f);
}

static void test_reset_admission_clears_existing_calibration_progress(void)
{
    imu_calibration_t calibration;
    imu_calibration_step_t step = IMU_CALIBRATION_ACCEPTED;

    imu_calibration_init(&calibration);
    assert(imu_calibration_push(
               &calibration, (imu_vec3f_t){0.0f, 0.0f, 1.0f},
               (imu_vec3f_t){0.5f, -0.25f, 0.125f}, true) ==
           IMU_CALIBRATION_ACCEPTED);

    const imu_calibration_admission_t stale = imu_calibration_admission(
        true, IMU_DT_INTEGRATE, false, true,
        (imu_vec3f_t){0.5f, -0.25f, 0.125f},
        (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 12u, 12u, 1000u, 6001u);
    assert(stale == IMU_CALIBRATION_ADMISSION_RESET);
    assert(imu_calibration_apply_admission(
        &calibration, stale, (imu_vec3f_t){0.0f, 0.0f, 1.0f},
        (imu_vec3f_t){0.5f, -0.25f, 0.125f}, &step));
    assert(step == IMU_CALIBRATION_RESET);
    assert(calibration.accepted == 0u);
    assert(calibration.gyro_sum_dps.x == 0.0f);
    assert(calibration.gyro_sum_dps.y == 0.0f);
    assert(calibration.gyro_sum_dps.z == 0.0f);
    assert(calibration.accel_sum_g.x == 0.0f);
    assert(calibration.accel_sum_g.y == 0.0f);
    assert(calibration.accel_sum_g.z == 0.0f);
}

static void test_status_composition_preserves_both_source_conditions(void)
{
    const uint16_t accel = IMU_STATUS_ACCEL_CORRECTION_INVALID |
                           IMU_STATUS_SPI_ERROR |
                           IMU_STATUS_EVENT_OVERRUN;
    const uint16_t gyro = IMU_STATUS_GYRO_SATURATED |
                          IMU_STATUS_TIMESTAMP_INVALID;

    assert(imu_service_status(IMU_CALIBRATING, true, 0u, accel, gyro) ==
           (IMU_STATUS_CALIBRATING | IMU_STATUS_ACCEL_CORRECTION_INVALID |
            IMU_STATUS_SPI_ERROR | IMU_STATUS_EVENT_OVERRUN |
            IMU_STATUS_GYRO_SATURATED | IMU_STATUS_TIMESTAMP_INVALID));
    assert(imu_service_status(IMU_RUNNING, true, 0u, 0u, 0u) ==
           IMU_STATUS_VALID);
    assert(imu_service_status(IMU_RUNNING, true, 0u, 0u,
                              IMU_STATUS_SPI_ERROR) == IMU_STATUS_SPI_ERROR);
}

static void test_led_phases_encode_each_state_pattern(void)
{
    imu_led_step_t step = imu_led_step(IMU_INITIALIZING, 0u);

    assert(step.on && step.duration_ms == 100u && step.next_phase == 1u);
    step = imu_led_step(IMU_INITIALIZING, step.next_phase);
    assert(!step.on && step.duration_ms == 900u && step.next_phase == 0u);

    step = imu_led_step(IMU_CALIBRATING, 0u);
    assert(step.on && step.duration_ms == 100u && step.next_phase == 1u);
    step = imu_led_step(IMU_CALIBRATING, step.next_phase);
    assert(!step.on && step.duration_ms == 100u && step.next_phase == 0u);

    step = imu_led_step(IMU_RUNNING, 0u);
    assert(step.on && step.duration_ms == 100u && step.next_phase == 1u);
    step = imu_led_step(IMU_RUNNING, step.next_phase);
    assert(!step.on && step.duration_ms == 1900u && step.next_phase == 0u);
}

static void test_fault_led_has_two_pulses_within_one_second(void)
{
    imu_led_step_t step = imu_led_step(IMU_FAULT_RETRY, 0u);

    assert(step.on && step.duration_ms == 100u && step.next_phase == 1u);
    step = imu_led_step(IMU_FAULT_RETRY, step.next_phase);
    assert(!step.on && step.duration_ms == 100u && step.next_phase == 2u);
    step = imu_led_step(IMU_FAULT_RETRY, step.next_phase);
    assert(step.on && step.duration_ms == 100u && step.next_phase == 3u);
    step = imu_led_step(IMU_FAULT_RETRY, step.next_phase);
    assert(!step.on && step.duration_ms == 700u && step.next_phase == 0u);
}

static void test_attempt_sequence_advances_while_drop_stays_sticky_until_queue(void)
{
    telemetry_attempt_state_t state = {0};

    assert(telemetry_attempt_begin(&state) == 0u);
    telemetry_attempt_dropped(&state);
    assert(state.next_sequence == 1u);
    assert(state.drops == 1u);
    assert(state.drop_sticky);

    assert(telemetry_attempt_begin(&state) == 1u);
    assert(state.drop_sticky);
    telemetry_attempt_dropped(&state);
    assert(state.next_sequence == 2u);
    assert(state.drops == 2u);
    assert(state.drop_sticky);

    assert(telemetry_attempt_begin(&state) == 2u);
    assert(state.drop_sticky);
    telemetry_attempt_queued(&state);
    assert(state.next_sequence == 3u);
    assert(state.drops == 2u);
    assert(!state.drop_sticky);
}

static void test_attempt_sequence_wraps_after_uint16_max(void)
{
    telemetry_attempt_state_t state = {.next_sequence = UINT16_MAX};

    assert(telemetry_attempt_begin(&state) == UINT16_MAX);
    telemetry_attempt_queued(&state);
    assert(state.next_sequence == 0u);
}

static void test_housekeeping_wait_is_zero_at_or_after_a_deadline(void)
{
    assert(imu_housekeeping_wait_ticks(100u, 100u, 200u) == 0u);
    assert(imu_housekeeping_wait_ticks(101u, 100u, 200u) == 0u);
    assert(imu_housekeeping_wait_ticks(300u, 100u, 200u) == 0u);
}

static void test_housekeeping_wait_uses_the_nearest_future_deadline(void)
{
    assert(imu_housekeeping_wait_ticks(100u, 125u, 110u) == 10u);
    assert(imu_housekeeping_wait_ticks(100u, 105u, 125u) == 5u);
}

static void test_housekeeping_wait_handles_tick_wrap(void)
{
    assert(imu_housekeeping_wait_ticks(UINT32_MAX - 5u, 3u, 10u) == 9u);
    assert(imu_housekeeping_wait_ticks(2u, UINT32_MAX, 10u) == 0u);
}

static void test_gyro_expiry_delay_requires_an_applicable_timestamp(void)
{
    assert(imu_gyro_expiry_delay_us(false, 50000u, 1000u) == UINT32_MAX);
}

static void test_gyro_expiry_delay_expires_only_after_20000_us(void)
{
    assert(imu_gyro_expiry_delay_us(true, 21000u, 1000u) == 1u);
    assert(imu_gyro_expiry_delay_us(true, 21001u, 1000u) == 0u);
}

static void test_gyro_expiry_delay_handles_timestamp_wrap(void)
{
    assert(imu_gyro_expiry_delay_us(true, 50u, UINT32_MAX - 100u) ==
           19850u);
    assert(imu_gyro_expiry_delay_us(true, 0u, UINT32_MAX - 20000u) == 0u);
}

static void test_gyro_expiry_transition_is_one_shot_and_clears_valid(void)
{
    mahony_t estimator;
    bool expired = false;
    uint16_t gyro_conditions = 0u;

    mahony_init_from_gravity(
        &estimator, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.0f);
    assert(estimator.valid);
    assert(!imu_gyro_expiry_transition(
        IMU_RUNNING, false, 21000u, true, 1000u, &expired,
        &gyro_conditions, &estimator));
    assert(!expired);
    assert(estimator.valid);

    assert(imu_gyro_expiry_transition(
        IMU_RUNNING, false, 21001u, true, 1000u, &expired,
        &gyro_conditions, &estimator));
    assert(expired);
    assert((gyro_conditions & IMU_STATUS_TIMESTAMP_INVALID) != 0u);
    assert(!estimator.valid);
    const uint16_t status = imu_service_status(
        IMU_RUNNING, estimator.valid, 0u, 0u, gyro_conditions);
    assert((status & IMU_STATUS_TIMESTAMP_INVALID) != 0u);
    assert((status & IMU_STATUS_VALID) == 0u);

    assert(!imu_gyro_expiry_transition(
        IMU_RUNNING, false, 50000u, true, 1000u, &expired,
        &gyro_conditions, &estimator));
}

static void test_gyro_expiry_suppresses_pending_and_non_running_transitions(void)
{
    mahony_t estimator;
    bool expired = false;
    uint16_t gyro_conditions = 0u;

    mahony_init_from_gravity(
        &estimator, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.0f);
    assert(!imu_gyro_expiry_transition(
        IMU_RUNNING, true, 50000u, true, 1000u, &expired,
        &gyro_conditions, &estimator));
    assert(!expired && estimator.valid && gyro_conditions == 0u);

    assert(!imu_gyro_expiry_transition(
        IMU_CALIBRATING, false, 50000u, true, 1000u, &expired,
        &gyro_conditions, &estimator));
    assert(!expired && estimator.valid && gyro_conditions == 0u);
}

static void test_gyro_expiry_requests_publish_only_for_an_output_change(void)
{
    mahony_t estimator = {0};
    bool expired = false;
    uint16_t gyro_conditions = IMU_STATUS_TIMESTAMP_INVALID;

    assert(!imu_gyro_expiry_transition(
        IMU_RUNNING, false, 21001u, true, 1000u, &expired,
        &gyro_conditions, &estimator));
    assert(expired);
    assert(!estimator.valid);
    assert(gyro_conditions == IMU_STATUS_TIMESTAMP_INVALID);
}

static void test_gyro_expiry_transition_handles_timestamp_wrap(void)
{
    mahony_t estimator;
    bool expired = false;
    uint16_t gyro_conditions = 0u;

    mahony_init_from_gravity(
        &estimator, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.0f);
    assert(!imu_gyro_expiry_transition(
        IMU_RUNNING, false, 0u, true, UINT32_MAX - 19999u, &expired,
        &gyro_conditions, &estimator));
    assert(imu_gyro_expiry_transition(
        IMU_RUNNING, false, 0u, true, UINT32_MAX - 20000u, &expired,
        &gyro_conditions, &estimator));
    assert(expired);
    assert(!estimator.valid);
}

static void test_running_gyro_timing_handles_nonintegrating_actions(void)
{
    const imu_vec3f_t zero = {0.0f, 0.0f, 0.0f};
    const imu_vec3f_t one_g = {0.0f, 0.0f, 1.0f};
    mahony_t estimator;

    mahony_init_from_gravity(&estimator, one_g, 0.2f, 0.0f);
    assert(imu_gyro_timestamp_condition(false, IMU_DT_INTEGRATE) ==
           IMU_STATUS_TIMESTAMP_INVALID);
    imu_running_gyro_result_t result = imu_apply_running_gyro_timing(
        &estimator, false, IMU_DT_INTEGRATE, zero, one_g, true, 0.001f);
    assert(!result.rejected_dt);
    assert(!result.long_gap);
    assert(!result.update_succeeded);
    assert(estimator.valid);

    result = imu_apply_running_gyro_timing(
        &estimator, true, IMU_DT_SKIP_KEEP_VALID, zero, one_g, true, 0.003f);
    assert(result.rejected_dt);
    assert(!result.long_gap);
    assert(!result.update_succeeded);
    assert(estimator.valid);

    result = imu_apply_running_gyro_timing(
        &estimator, true, IMU_DT_SKIP_INVALIDATE, zero, one_g, true, 0.021f);
    assert(result.rejected_dt);
    assert(result.long_gap);
    assert(!result.update_succeeded);
    assert(!estimator.valid);
}

static void test_gyro_stream_recovers_through_production_timing_seams(void)
{
    const imu_vec3f_t zero = {0.0f, 0.0f, 0.0f};
    const imu_vec3f_t one_g = {0.0f, 0.0f, 1.0f};
    mahony_t estimator;
    uint32_t gyro_baseline_us = 1000u;
    uint32_t last_gyro_timestamp_us = 1000u;
    bool last_gyro_timestamp_valid = true;
    bool expired = false;
    uint16_t gyro_conditions = 0u;

    mahony_init_from_gravity(&estimator, one_g, 0.2f, 0.0f);
    assert(estimator.valid);
    assert(imu_service_status(IMU_RUNNING, estimator.valid, 0u, 0u,
                              gyro_conditions) == IMU_STATUS_VALID);

    assert(imu_gyro_expiry_transition(
        IMU_RUNNING, false, 21001u, last_gyro_timestamp_valid,
        last_gyro_timestamp_us, &expired, &gyro_conditions, &estimator));
    assert(!imu_gyro_expiry_transition(
        IMU_RUNNING, false, 21002u, last_gyro_timestamp_valid,
        last_gyro_timestamp_us, &expired, &gyro_conditions, &estimator));
    assert(expired);
    assert((gyro_conditions & IMU_STATUS_TIMESTAMP_INVALID) != 0u);
    assert(!estimator.valid);
    uint16_t status = imu_service_status(
        IMU_RUNNING, estimator.valid, 0u, 0u, gyro_conditions);
    assert((status & IMU_STATUS_TIMESTAMP_INVALID) != 0u);
    assert((status & IMU_STATUS_VALID) == 0u);

    const uint32_t first_return_us = 22002u;
    imu_dt_action_t action = imu_classify_gyro_delta_us(
        (uint32_t)(first_return_us - gyro_baseline_us));
    assert(action == IMU_DT_SKIP_INVALIDATE);
    imu_accept_new_gyro_sample(first_return_us, &last_gyro_timestamp_us,
                               &last_gyro_timestamp_valid, &expired);
    assert(last_gyro_timestamp_us == first_return_us);
    assert(last_gyro_timestamp_valid);
    assert(!expired);
    gyro_conditions = imu_gyro_timestamp_condition(true, action);
    assert(gyro_conditions == IMU_STATUS_TIMESTAMP_INVALID);
    imu_running_gyro_result_t result = imu_apply_running_gyro_timing(
        &estimator, true, action, zero, one_g, true, 0.021002f);
    assert(result.rejected_dt);
    assert(result.long_gap);
    assert(!result.update_succeeded);
    assert(!estimator.valid);
    status = imu_service_status(
        IMU_RUNNING, estimator.valid, 0u, 0u, gyro_conditions);
    assert((status & IMU_STATUS_TIMESTAMP_INVALID) != 0u);
    assert((status & IMU_STATUS_VALID) == 0u);

    gyro_baseline_us = first_return_us;
    const uint32_t second_return_us = 23002u;
    action = imu_classify_gyro_delta_us(
        (uint32_t)(second_return_us - gyro_baseline_us));
    assert(action == IMU_DT_INTEGRATE);
    imu_accept_new_gyro_sample(second_return_us, &last_gyro_timestamp_us,
                               &last_gyro_timestamp_valid, &expired);
    gyro_conditions = imu_gyro_timestamp_condition(true, action);
    assert(gyro_conditions == 0u);
    result = imu_apply_running_gyro_timing(
        &estimator, true, action, zero, one_g, true, 0.001f);
    assert(!result.rejected_dt);
    assert(!result.long_gap);
    assert(result.update_succeeded);
    assert(estimator.valid);
    status = imu_service_status(
        IMU_RUNNING, estimator.valid, 0u, 0u, gyro_conditions);
    assert((status & IMU_STATUS_TIMESTAMP_INVALID) == 0u);
    assert((status & IMU_STATUS_VALID) != 0u);
}

int main(void)
{
    test_dt_classification_has_inclusive_integration_boundaries();
    test_accel_correction_requires_the_newest_consumed_sequence();
    test_accel_correction_uses_inclusive_norm_and_freshness_gates();
    test_gyro_saturation_starts_at_raw_magnitude_31130();
    test_gyro_bias_equal_to_measurement_gives_exact_zero();
    test_gyro_bias_subtracts_arbitrary_signed_vectors_component_wise();
    test_calibration_admission_accepts_a_coherent_sample();
    test_calibration_admission_skips_a_pending_accel();
    test_calibration_admission_gives_gyro_failures_reset_precedence();
    test_pending_accel_cannot_mask_observable_sample_failures();
    test_production_admission_application_interleaves_skip_and_accept();
    test_reset_admission_clears_existing_calibration_progress();
    test_status_composition_preserves_both_source_conditions();
    test_led_phases_encode_each_state_pattern();
    test_fault_led_has_two_pulses_within_one_second();
    test_attempt_sequence_advances_while_drop_stays_sticky_until_queue();
    test_attempt_sequence_wraps_after_uint16_max();
    test_housekeeping_wait_is_zero_at_or_after_a_deadline();
    test_housekeeping_wait_uses_the_nearest_future_deadline();
    test_housekeeping_wait_handles_tick_wrap();
    test_gyro_expiry_delay_requires_an_applicable_timestamp();
    test_gyro_expiry_delay_expires_only_after_20000_us();
    test_gyro_expiry_delay_handles_timestamp_wrap();
    test_gyro_expiry_transition_is_one_shot_and_clears_valid();
    test_gyro_expiry_suppresses_pending_and_non_running_transitions();
    test_gyro_expiry_requests_publish_only_for_an_output_change();
    test_gyro_expiry_transition_handles_timestamp_wrap();
    test_running_gyro_timing_handles_nonintegrating_actions();
    test_gyro_stream_recovers_through_production_timing_seams();
    return 0;
}
