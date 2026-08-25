#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "attitude/mahony.h"

_Static_assert(sizeof(imu_vec3f_t) == 3u * sizeof(float),
               "imu_vec3f_t layout changed");
_Static_assert(sizeof(imu_quatf_t) == 4u * sizeof(float),
               "imu_quatf_t layout changed");

static void assert_close(float actual, float expected, float tolerance)
{
    assert(isfinite(actual));
    assert(fabsf(actual - expected) <= tolerance);
}

static void assert_quaternion_finite_unit(imu_quatf_t q, float tolerance)
{
    const float norm = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);

    assert(isfinite(q.w));
    assert(isfinite(q.x));
    assert(isfinite(q.y));
    assert(isfinite(q.z));
    assert_close(norm, 1.0f, tolerance);
}

static void test_gravity_initializer_sets_standard_zyx_yaw_zero(void)
{
    static const struct {
        imu_vec3f_t gravity;
        imu_vec3f_t expected_euler_deg;
    } cases[] = {
        {{0.0f, 0.5f, 0.866025404f}, {30.0f, 0.0f, 0.0f}},
        {{0.0f, -0.5f, 0.866025404f}, {-30.0f, 0.0f, 0.0f}},
        {{-0.422618262f, 0.0f, 0.906307787f}, {0.0f, 25.0f, 0.0f}},
        {{0.422618262f, 0.0f, 0.906307787f}, {0.0f, -25.0f, 0.0f}},
        {{0.258819045f, 0.330366090f, 0.907673371f}, {20.0f, -15.0f, 0.0f}},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        mahony_t m;
        mahony_init_from_gravity(&m, cases[i].gravity, 0.2f, 0.3f);

        assert(m.valid);
        assert_close(m.two_kp, 0.4f, 1.0e-7f);
        assert_close(m.two_ki, 0.6f, 1.0e-7f);
        assert_close(m.integral_fb.x, 0.0f, 0.0f);
        assert_close(m.integral_fb.y, 0.0f, 0.0f);
        assert_close(m.integral_fb.z, 0.0f, 0.0f);
        assert_quaternion_finite_unit(m.q, 1.0e-6f);

        const imu_vec3f_t euler = mahony_euler_deg(&m);
        assert_close(euler.x, cases[i].expected_euler_deg.x, 2.0e-4f);
        assert_close(euler.y, cases[i].expected_euler_deg.y, 2.0e-4f);
        assert_close(euler.z, cases[i].expected_euler_deg.z, 2.0e-4f);
    }

    static const imu_vec3f_t invalid_gravity[] = {
        {0.0f, 0.0f, 0.0f},
        {NAN, 0.0f, 1.0f},
        {0.0f, INFINITY, 1.0f},
        {FLT_MAX, FLT_MAX, FLT_MAX},
    };
    for (size_t i = 0; i < sizeof invalid_gravity / sizeof invalid_gravity[0]; ++i) {
        mahony_t m;
        mahony_init_from_gravity(&m, invalid_gravity[i], 0.2f, 0.0f);
        assert(!m.valid);
        assert_close(m.q.w, 1.0f, 0.0f);
        assert_close(m.q.x, 0.0f, 0.0f);
        assert_close(m.q.y, 0.0f, 0.0f);
        assert_close(m.q.z, 0.0f, 0.0f);
    }
}

static void test_one_step_uses_doubled_gain_with_rmcs_half_error(void)
{
    mahony_t m;
    mahony_init_from_gravity(&m, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.0f);

    assert(mahony_update(&m,
                         (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                         (imu_vec3f_t){0.0f, 0.707106781f, 0.707106781f},
                         true,
                         0.001f));

    /* half_error.x=sqrt(1/2)/2, so q.x=0.5*(0.4*half_error.x)*0.001. */
    assert_close(m.q.x, 0.0000707107f, 2.0e-9f);
    assert_close(m.q.y, 0.0f, 0.0f);
    assert_close(m.q.z, 0.0f, 0.0f);
}

static void test_one_step_uses_all_half_error_components_and_clears_disabled_integral(void)
{
    mahony_t m = {
        .q = {0.8f, 0.2f, -0.4f, 0.4f},
        .integral_fb = {0.3f, -0.2f, 0.1f},
        .two_kp = 0.4f,
        .two_ki = 0.0f,
        .valid = true,
    };

    assert(mahony_update(&m,
                         (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                         (imu_vec3f_t){0.0f, 0.6f, 0.8f},
                         true,
                         0.001f));

    /* half_error=(0.18,0.32,-0.24); expected values are hand-normalized. */
    assert_close(m.q.w, 0.800037597f, 2.0e-7f);
    assert_close(m.q.x, 0.200022399f, 2.0e-7f);
    assert_close(m.q.y, -0.399924798f, 2.0e-7f);
    assert_close(m.q.z, 0.399988798f, 2.0e-7f);
    assert_close(m.integral_fb.x, 0.0f, 0.0f);
    assert_close(m.integral_fb.y, 0.0f, 0.0f);
    assert_close(m.integral_fb.z, 0.0f, 0.0f);
}

static void test_rejected_dt_is_complete_no_op(void)
{
    static const float rejected_dt[] = {
        -INFINITY, -1.0f, 0.0f, 0.000499f, 0.002001f, INFINITY, NAN,
    };
    mahony_t m;
    mahony_init_from_gravity(&m, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.1f);
    m.integral_fb = (imu_vec3f_t){0.1f, -0.2f, 0.3f};

    for (size_t i = 0; i < sizeof rejected_dt / sizeof rejected_dt[0]; ++i) {
        const mahony_t before = m;
        assert(!mahony_update(&m,
                              (imu_vec3f_t){1.0f, 2.0f, 3.0f},
                              (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                              true,
                              rejected_dt[i]));
        assert(memcmp(&m, &before, sizeof m) == 0);
    }

    m.integral_fb = (imu_vec3f_t){0.0f, 0.0f, 0.0f};
    assert(mahony_update(&m,
                         (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                         (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                         false,
                         0.0005f));
    assert(mahony_update(&m,
                         (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                         (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                         false,
                         0.002f));
}

static void test_invalidation_preserves_quaternion_and_recovers(void)
{
    mahony_t m;
    mahony_init_from_gravity(&m,
                             (imu_vec3f_t){0.0f, 0.5f, 0.866025404f},
                             0.2f,
                             0.1f);
    m.integral_fb = (imu_vec3f_t){0.1f, -0.2f, 0.3f};
    const imu_quatf_t before = mahony_quaternion(&m);

    mahony_invalidate(&m);
    assert(!m.valid);
    assert(memcmp(&before, &m.q, sizeof before) == 0);
    assert_close(m.integral_fb.x, 0.0f, 0.0f);
    assert_close(m.integral_fb.y, 0.0f, 0.0f);
    assert_close(m.integral_fb.z, 0.0f, 0.0f);

    assert(mahony_update(&m,
                         (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                         (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                         false,
                         0.001f));
    assert(m.valid);
    assert_close(m.q.w, before.w, 2.0e-7f);
    assert_close(m.q.x, before.x, 2.0e-7f);
    assert_close(m.q.y, before.y, 2.0e-7f);
    assert_close(m.q.z, before.z, 2.0e-7f);

    mahony_invalidate(NULL);
}

static void test_invalid_accel_and_disabled_correction_stay_finite(void)
{
    static const struct {
        imu_vec3f_t accel;
        bool correction_valid;
    } cases[] = {
        {{0.0f, 0.0f, 0.0f}, true},
        {{NAN, 0.0f, 1.0f}, true},
        {{FLT_MAX, FLT_MAX, FLT_MAX}, true},
        {{NAN, INFINITY, -INFINITY}, false},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        mahony_t m;
        mahony_init_from_gravity(&m, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.1f);

        assert(mahony_update(&m,
                             (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                             cases[i].accel,
                             cases[i].correction_valid,
                             0.001f));
        assert(m.valid);
        assert_quaternion_finite_unit(m.q, 1.0e-6f);
    }
}

static void test_failed_normalization_does_not_publish_non_finite_state(void)
{
    mahony_t m;
    mahony_init_from_gravity(&m, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.1f);
    m.integral_fb = (imu_vec3f_t){0.1f, -0.2f, 0.3f};

    const mahony_t before_huge = m;
    assert(!mahony_update(&m,
                          (imu_vec3f_t){FLT_MAX, FLT_MAX, FLT_MAX},
                          (imu_vec3f_t){0.0f, 0.0f, 1.0f},
                          false,
                          0.001f));
    assert(memcmp(&m, &before_huge, sizeof m) == 0);
    assert_quaternion_finite_unit(m.q, 1.0e-6f);

    const mahony_t before_nan = m;
    assert(!mahony_update(&m,
                          (imu_vec3f_t){NAN, 0.0f, 0.0f},
                          (imu_vec3f_t){0.0f, 1.0f, 0.0f},
                          true,
                          0.001f));
    assert(memcmp(&m, &before_nan, sizeof m) == 0);
    assert_quaternion_finite_unit(m.q, 1.0e-6f);
}

static void test_gravity_only_feedback_converges_in_initial_heading_gauge(void)
{
    static const struct {
        imu_vec3f_t gravity;
        float roll_deg;
        float pitch_deg;
        float yaw_deg;
    } cases[] = {
        {{0.0f, 0.5f, 0.866025404f}, 30.0f, 0.0f, 0.0f},
        {{0.0f, -0.5f, 0.866025404f}, -30.0f, 0.0f, 0.0f},
        {{-0.422618262f, 0.0f, 0.906307787f}, 0.0f, 25.0f, 0.0f},
        {{0.422618262f, 0.0f, 0.906307787f}, 0.0f, -25.0f, 0.0f},
        {{0.258819045f, 0.330366090f, 0.907673371f},
         20.0f, -15.0f, -2.659638f},
    };
    float max_roll_pitch_error = 0.0f;
    float max_gauge_yaw_error = 0.0f;
    float max_norm_error = 0.0f;

    /*
     * From identity, gravity-only RMCS feedback preserves its unobservable
     * heading gauge and reaches the shortest-tilt quaternion. The combined
     * fixture's standard-ZYX yaw is therefore -2.659638 deg, distinct from
     * the gravity initializer's explicitly tested standard-ZYX yaw of zero.
     */
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        mahony_t m;
        mahony_init_from_gravity(&m, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.0f);
        assert_close(m.two_kp, 0.4f, 1.0e-7f);

        for (unsigned step = 0; step < 30000u; ++step) {
            assert(mahony_update(&m,
                                 (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                                 cases[i].gravity,
                                 true,
                                 0.001f));
        }

        const imu_vec3f_t euler = mahony_euler_deg(&m);
        const float roll_error = fabsf(euler.x - cases[i].roll_deg);
        const float pitch_error = fabsf(euler.y - cases[i].pitch_deg);
        const float yaw_error = fabsf(euler.z - cases[i].yaw_deg);
        const float norm = sqrtf(m.q.w * m.q.w + m.q.x * m.q.x +
                                 m.q.y * m.q.y + m.q.z * m.q.z);
        const float norm_error = fabsf(norm - 1.0f);
        if (roll_error > max_roll_pitch_error) max_roll_pitch_error = roll_error;
        if (pitch_error > max_roll_pitch_error) max_roll_pitch_error = pitch_error;
        if (yaw_error > max_gauge_yaw_error) max_gauge_yaw_error = yaw_error;
        if (norm_error > max_norm_error) max_norm_error = norm_error;

        assert(roll_error < 0.5f);
        assert(pitch_error < 0.5f);
        assert(yaw_error < 0.1f);
        assert(norm_error < 1.0e-4f);
        assert_quaternion_finite_unit(m.q, 1.0e-4f);
    }

    printf("mahony gravity-only convergence: 30.000 s at Kp 0.2, "
           "max roll/pitch %.6f deg, standard-ZYX gauge yaw error %.6f deg, "
           "norm %.9f\n",
           (double)max_roll_pitch_error,
           (double)max_gauge_yaw_error,
           (double)max_norm_error);
}

static void test_constant_yaw_rate_integrates_ninety_degrees(void)
{
    mahony_t m;
    mahony_init_from_gravity(&m, (imu_vec3f_t){0.0f, 0.0f, 1.0f}, 0.2f, 0.0f);

    for (unsigned step = 0; step < 1000u; ++step) {
        assert(mahony_update(&m,
                             (imu_vec3f_t){0.0f, 0.0f, 1.570796327f},
                             (imu_vec3f_t){0.0f, 0.0f, 0.0f},
                             false,
                             0.001f));
    }

    const imu_vec3f_t euler = mahony_euler_deg(&m);
    assert(euler.z > 89.8f && euler.z < 90.2f);
    assert_close(euler.x, 0.0f, 1.0e-5f);
    assert_close(euler.y, 0.0f, 1.0e-5f);
    assert_quaternion_finite_unit(m.q, 1.0e-4f);
    printf("mahony constant yaw: %.6f deg\n", (double)euler.z);
}

static void test_quaternion_sign_equivalence_and_euler_wrap(void)
{
    mahony_t positive = {
        .q = {0.004363309f, 0.0f, 0.0f, 0.999990481f},
        .valid = true,
    };
    mahony_t negative = {
        .q = {-0.004363309f, 0.0f, 0.0f, -0.999990481f},
        .valid = true,
    };
    const imu_vec3f_t positive_euler = mahony_euler_deg(&positive);
    const imu_vec3f_t negative_euler = mahony_euler_deg(&negative);

    assert_close(positive_euler.z, 179.5f, 2.0e-4f);
    assert_close(negative_euler.x, positive_euler.x, 1.0e-6f);
    assert_close(negative_euler.y, positive_euler.y, 1.0e-6f);
    assert_close(negative_euler.z, positive_euler.z, 1.0e-6f);

    mahony_t half_turn = {.q = {0.0f, 0.0f, 0.0f, 1.0f}, .valid = true};
    const imu_vec3f_t wrapped = mahony_euler_deg(&half_turn);
    assert_close(wrapped.z, -180.0f, 0.0f);
    assert(wrapped.x >= -180.0f && wrapped.x < 180.0f);
    assert(wrapped.y >= -180.0f && wrapped.y < 180.0f);
    assert(wrapped.z >= -180.0f && wrapped.z < 180.0f);
}

int main(void)
{
    test_gravity_initializer_sets_standard_zyx_yaw_zero();
    test_one_step_uses_doubled_gain_with_rmcs_half_error();
    test_one_step_uses_all_half_error_components_and_clears_disabled_integral();
    test_rejected_dt_is_complete_no_op();
    test_invalidation_preserves_quaternion_and_recovers();
    test_invalid_accel_and_disabled_correction_stay_finite();
    test_failed_normalization_does_not_publish_non_finite_state();
    test_gravity_only_feedback_converges_in_initial_heading_gauge();
    test_constant_yaw_rate_integrates_ninety_degrees();
    test_quaternion_sign_equivalence_and_euler_wrap();
    return 0;
}
