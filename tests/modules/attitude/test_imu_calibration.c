#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "attitude/imu_calibration.h"

static void assert_close(float actual, float expected)
{
    assert(isfinite(actual));
    assert(fabsf(actual - expected) <= 1.0e-6f);
}

static void assert_vec_zero(imu_vec3f_t value)
{
    assert_close(value.x, 0.0f);
    assert_close(value.y, 0.0f);
    assert_close(value.z, 0.0f);
}

static void test_completion_requires_exactly_2000_consecutive_samples(void)
{
    imu_calibration_t calibration;
    imu_calibration_init(&calibration);

    for (uint32_t i = 0; i < 1999u; ++i) {
        assert(imu_calibration_push(&calibration,
                                    (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                    (imu_vec3f_t){0.5f, -0.25f, 0.125f},
                                    true) == IMU_CALIBRATION_ACCEPTED);
    }
    assert(!calibration.complete);
    assert(calibration.accepted == 1999u);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                (imu_vec3f_t){0.5f, -0.25f, 0.125f},
                                true) == IMU_CALIBRATION_COMPLETE);
    assert(calibration.complete);
    assert(calibration.accepted == IMU_CALIBRATION_SAMPLE_COUNT);
    assert_close(calibration.gyro_bias_dps.x, 0.5f);
    assert_close(calibration.gyro_bias_dps.y, -0.25f);
    assert_close(calibration.gyro_bias_dps.z, 0.125f);
    assert_close(calibration.gravity_g.x, 0.0f);
    assert_close(calibration.gravity_g.y, 0.0f);
    assert_close(calibration.gravity_g.z, 1.0f);
}

static void test_accel_norm_boundaries_are_inclusive(void)
{
    imu_calibration_t calibration;
    imu_calibration_init(&calibration);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.9f, 0.0f, 0.0f},
                                (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                                true) == IMU_CALIBRATION_ACCEPTED);
    assert(calibration.accepted == 1u);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 1.1f, 0.0f},
                                (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                                true) == IMU_CALIBRATION_ACCEPTED);
    assert(calibration.accepted == 2u);
}

static void test_stationary_predicates_match_calibration_boundaries(void)
{
    assert(imu_calibration_accel_stationary(
        (imu_vec3f_t){0.9f, 0.0f, 0.0f}));
    assert(imu_calibration_accel_stationary(
        (imu_vec3f_t){0.0f, 1.1f, 0.0f}));
    assert(!imu_calibration_accel_stationary(
        (imu_vec3f_t){0.8999f, 0.0f, 0.0f}));
    assert(!imu_calibration_accel_stationary(
        (imu_vec3f_t){0.0f, 0.0f, 1.1001f}));
    assert(!imu_calibration_accel_stationary(
        (imu_vec3f_t){NAN, 0.0f, 1.0f}));

    assert(imu_calibration_gyro_stationary(
        (imu_vec3f_t){2.999f, 0.0f, 0.0f}));
    assert(!imu_calibration_gyro_stationary(
        (imu_vec3f_t){0.0f, 3.0f, 0.0f}));
    assert(!imu_calibration_gyro_stationary(
        (imu_vec3f_t){0.0f, NAN, 0.0f}));
}

static void test_accel_norm_below_lower_bound_resets_all_state(void)
{
    imu_calibration_t calibration = {0};
    imu_calibration_init(&calibration);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                (imu_vec3f_t){0.25f, -0.5f, 0.75f},
                                true) == IMU_CALIBRATION_ACCEPTED);
    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 1.0f, 0.0f},
                                (imu_vec3f_t){-0.125f, 0.25f, -0.5f},
                                true) == IMU_CALIBRATION_ACCEPTED);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.8999f, 0.0f, 0.0f},
                                (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                                true) == IMU_CALIBRATION_RESET);
    assert(calibration.accepted == 0u);
    assert(!calibration.complete);
    assert_vec_zero(calibration.gyro_sum_dps);
    assert_vec_zero(calibration.accel_sum_g);
    assert_vec_zero(calibration.gyro_bias_dps);
    assert_vec_zero(calibration.gravity_g);
}

static void test_gyro_norm_must_be_strictly_below_three(void)
{
    imu_calibration_t calibration;
    imu_calibration_init(&calibration);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                (imu_vec3f_t){2.999f, 0.0f, 0.0f},
                                true) == IMU_CALIBRATION_ACCEPTED);
    assert(calibration.accepted == 1u);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                (imu_vec3f_t){0.0f, 3.0f, 0.0f},
                                true) == IMU_CALIBRATION_RESET);
    assert(calibration.accepted == 0u);
    assert(!calibration.complete);
}

static void test_invalid_timestamp_resets(void)
{
    imu_calibration_t calibration;
    imu_calibration_init(&calibration);
    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                (imu_vec3f_t){0.25f, -0.5f, 0.75f},
                                true) == IMU_CALIBRATION_ACCEPTED);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                                false) == IMU_CALIBRATION_RESET);
    assert(calibration.accepted == 0u);
    assert(!calibration.complete);
    assert_vec_zero(calibration.gyro_sum_dps);
    assert_vec_zero(calibration.accel_sum_g);
    assert_vec_zero(calibration.gyro_bias_dps);
    assert_vec_zero(calibration.gravity_g);
}

static void test_non_finite_samples_reset(void)
{
    static const struct {
        imu_vec3f_t accel_g;
        imu_vec3f_t gyro_dps;
    } rejected[] = {
        {{NAN, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
        {{INFINITY, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, NAN, 0.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, INFINITY}},
    };

    for (size_t i = 0; i < sizeof rejected / sizeof rejected[0]; ++i) {
        imu_calibration_t calibration;
        imu_calibration_init(&calibration);
        assert(imu_calibration_push(&calibration,
                                    (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                    (imu_vec3f_t){0.25f, -0.5f, 0.75f},
                                    true) == IMU_CALIBRATION_ACCEPTED);

        assert(imu_calibration_push(&calibration,
                                    rejected[i].accel_g,
                                    rejected[i].gyro_dps,
                                    true) ==
               IMU_CALIBRATION_RESET);
        assert(calibration.accepted == 0u);
        assert(!calibration.complete);
        assert_vec_zero(calibration.gyro_sum_dps);
        assert_vec_zero(calibration.accel_sum_g);
        assert_vec_zero(calibration.gyro_bias_dps);
        assert_vec_zero(calibration.gravity_g);
    }
}

static void test_movement_after_1999_samples_clears_count_and_sums(void)
{
    imu_calibration_t calibration;
    imu_calibration_init(&calibration);

    for (uint32_t i = 0; i < 1999u; ++i) {
        assert(imu_calibration_push(&calibration,
                                    (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                    (imu_vec3f_t){0.5f, -0.25f, 0.125f},
                                    true) == IMU_CALIBRATION_ACCEPTED);
    }

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 0.0f, 1.1001f},
                                (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                                true) == IMU_CALIBRATION_RESET);
    assert(calibration.accepted == 0u);
    assert(!calibration.complete);
    assert_vec_zero(calibration.gyro_sum_dps);
    assert_vec_zero(calibration.accel_sum_g);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 1.0f, 0.0f},
                                (imu_vec3f_t){-0.5f, 0.25f, -0.125f},
                                true) == IMU_CALIBRATION_ACCEPTED);
    assert(calibration.accepted == 1u);
    assert_close(calibration.gyro_sum_dps.x, -0.5f);
    assert_close(calibration.gyro_sum_dps.y, 0.25f);
    assert_close(calibration.gyro_sum_dps.z, -0.125f);
    assert_close(calibration.accel_sum_g.x, 0.0f);
    assert_close(calibration.accel_sum_g.y, 1.0f);
    assert_close(calibration.accel_sum_g.z, 0.0f);
}

static void test_alternating_samples_produce_arithmetic_means(void)
{
    imu_calibration_t calibration;
    imu_calibration_init(&calibration);

    for (uint32_t i = 0; i < IMU_CALIBRATION_SAMPLE_COUNT; ++i) {
        const bool even = (i % 2u) == 0u;
        const imu_vec3f_t accel = even
            ? (imu_vec3f_t){0.0f, 0.0f, 0.9f}
            : (imu_vec3f_t){0.0f, 0.0f, 1.1f};
        const imu_vec3f_t gyro = even
            ? (imu_vec3f_t){1.0f, -1.0f, 0.25f}
            : (imu_vec3f_t){2.0f, 1.0f, -0.25f};
        const imu_calibration_step_t expected =
            i == 1999u ? IMU_CALIBRATION_COMPLETE : IMU_CALIBRATION_ACCEPTED;

        assert(imu_calibration_push(&calibration, accel, gyro, true) == expected);
    }

    assert_close(calibration.gyro_bias_dps.x, 1.5f);
    assert_close(calibration.gyro_bias_dps.y, 0.0f);
    assert_close(calibration.gyro_bias_dps.z, 0.0f);
    assert_close(calibration.gravity_g.x, 0.0f);
    assert_close(calibration.gravity_g.y, 0.0f);
    assert_close(calibration.gravity_g.z, 1.0f);
}

static void test_completed_calibration_is_immutable(void)
{
    imu_calibration_t calibration;
    imu_calibration_init(&calibration);
    for (uint32_t i = 0; i < IMU_CALIBRATION_SAMPLE_COUNT; ++i) {
        (void)imu_calibration_push(&calibration,
                                   (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                   (imu_vec3f_t){0.5f, -0.25f, 0.125f},
                                   true);
    }
    const imu_calibration_t completed = calibration;

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){INFINITY, 0.0f, 0.0f},
                                (imu_vec3f_t){0.0f, NAN, 0.0f},
                                false) == IMU_CALIBRATION_COMPLETE);
    assert(memcmp(&calibration, &completed, sizeof calibration) == 0);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                (imu_vec3f_t){2.0f, 0.0f, 0.0f},
                                true) == IMU_CALIBRATION_COMPLETE);
    assert(memcmp(&calibration, &completed, sizeof calibration) == 0);
}

static void test_explicit_init_restarts_completed_calibration(void)
{
    imu_calibration_t calibration = {0};
    imu_calibration_init(&calibration);
    for (uint32_t i = 0; i < IMU_CALIBRATION_SAMPLE_COUNT; ++i) {
        (void)imu_calibration_push(&calibration,
                                   (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                                   (imu_vec3f_t){0.5f, -0.25f, 0.125f},
                                   true);
    }
    assert(calibration.complete);

    imu_calibration_init(&calibration);
    assert(calibration.accepted == 0u);
    assert(!calibration.complete);
    assert_vec_zero(calibration.gyro_sum_dps);
    assert_vec_zero(calibration.accel_sum_g);
    assert_vec_zero(calibration.gyro_bias_dps);
    assert_vec_zero(calibration.gravity_g);

    assert(imu_calibration_push(&calibration,
                                (imu_vec3f_t){0.0f, 1.0f, 0.0f},
                                (imu_vec3f_t){-0.5f, 0.25f, -0.125f},
                                true) == IMU_CALIBRATION_ACCEPTED);
    assert(calibration.accepted == 1u);
    assert_close(calibration.gyro_sum_dps.x, -0.5f);
    assert_close(calibration.gyro_sum_dps.y, 0.25f);
    assert_close(calibration.gyro_sum_dps.z, -0.125f);
    assert_close(calibration.accel_sum_g.x, 0.0f);
    assert_close(calibration.accel_sum_g.y, 1.0f);
    assert_close(calibration.accel_sum_g.z, 0.0f);
}

int main(void)
{
    test_accel_norm_below_lower_bound_resets_all_state();
    test_explicit_init_restarts_completed_calibration();
    test_completion_requires_exactly_2000_consecutive_samples();
    test_accel_norm_boundaries_are_inclusive();
    test_stationary_predicates_match_calibration_boundaries();
    test_gyro_norm_must_be_strictly_below_three();
    test_invalid_timestamp_resets();
    test_non_finite_samples_reset();
    test_movement_after_1999_samples_clears_count_and_sums();
    test_alternating_samples_produce_arithmetic_means();
    test_completed_calibration_is_immutable();
    return 0;
}
