#include "attitude/mahony.h"

#include <math.h>
#include <stddef.h>

#define RAD_TO_DEG 57.295779513082320876f

static bool finite_vec(imu_vec3f_t value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static float vec_norm_squared(imu_vec3f_t value)
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

static bool normalize_quaternion(imu_quatf_t *q)
{
    const float norm_squared = q->w * q->w + q->x * q->x +
                               q->y * q->y + q->z * q->z;
    if (!isfinite(norm_squared) || norm_squared <= 0.0f) {
        return false;
    }

    const float reciprocal_norm = 1.0f / sqrtf(norm_squared);
    q->w *= reciprocal_norm;
    q->x *= reciprocal_norm;
    q->y *= reciprocal_norm;
    q->z *= reciprocal_norm;
    return isfinite(q->w) && isfinite(q->x) && isfinite(q->y) && isfinite(q->z);
}

static float clamp_unit(float value)
{
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

static float wrap_deg(float angle_deg)
{
    float wrapped = fmodf(angle_deg + 180.0f, 360.0f);
    if (wrapped < 0.0f) wrapped += 360.0f;
    return wrapped - 180.0f;
}

void mahony_init_from_gravity(mahony_t *self,
                              imu_vec3f_t accel_g,
                              float kp,
                              float ki)
{
    if (self == NULL) return;

    self->q = (imu_quatf_t){1.0f, 0.0f, 0.0f, 0.0f};
    self->integral_fb = (imu_vec3f_t){0.0f, 0.0f, 0.0f};
    self->two_kp = 2.0f * kp;
    self->two_ki = 2.0f * ki;
    self->valid = false;

    if (!finite_vec(accel_g)) return;

    const float norm_squared = vec_norm_squared(accel_g);
    if (!isfinite(norm_squared) || norm_squared <= 0.0f) return;

    const float norm = sqrtf(norm_squared);
    const float roll = atan2f(accel_g.y, accel_g.z);
    const float pitch = -asinf(clamp_unit(accel_g.x / norm));
    const float half_roll = 0.5f * roll;
    const float half_pitch = 0.5f * pitch;
    const float cos_roll = cosf(half_roll);
    const float sin_roll = sinf(half_roll);
    const float cos_pitch = cosf(half_pitch);
    const float sin_pitch = sinf(half_pitch);
    imu_quatf_t initialized = {
        cos_roll * cos_pitch,
        sin_roll * cos_pitch,
        cos_roll * sin_pitch,
        -sin_roll * sin_pitch,
    };

    if (!normalize_quaternion(&initialized)) return;
    self->q = initialized;
    self->valid = true;
}

void mahony_invalidate(mahony_t *self)
{
    if (self == NULL) return;
    self->integral_fb = (imu_vec3f_t){0.0f, 0.0f, 0.0f};
    self->valid = false;
}

bool mahony_update(mahony_t *self,
                   imu_vec3f_t gyro_rad_s,
                   imu_vec3f_t accel_g,
                   bool correction_valid,
                   float dt_s)
{
    if (self == NULL || !isfinite(dt_s) || dt_s < 0.0005f || dt_s > 0.002f) {
        return false;
    }

    imu_vec3f_t integral_fb = self->integral_fb;
    if (correction_valid && finite_vec(accel_g)) {
        const float norm_squared = vec_norm_squared(accel_g);
        if (isfinite(norm_squared) && norm_squared > 0.0f) {
            const float reciprocal_norm = 1.0f / sqrtf(norm_squared);
            accel_g.x *= reciprocal_norm;
            accel_g.y *= reciprocal_norm;
            accel_g.z *= reciprocal_norm;

            const float half_vx = self->q.x * self->q.z - self->q.w * self->q.y;
            const float half_vy = self->q.w * self->q.x + self->q.y * self->q.z;
            const float half_vz = self->q.w * self->q.w - 0.5f + self->q.z * self->q.z;
            const imu_vec3f_t half_error = {
                accel_g.y * half_vz - accel_g.z * half_vy,
                accel_g.z * half_vx - accel_g.x * half_vz,
                accel_g.x * half_vy - accel_g.y * half_vx,
            };

            if (self->two_ki > 0.0f) {
                integral_fb.x += half_error.x * self->two_ki * dt_s;
                integral_fb.y += half_error.y * self->two_ki * dt_s;
                integral_fb.z += half_error.z * self->two_ki * dt_s;
                gyro_rad_s.x += integral_fb.x;
                gyro_rad_s.y += integral_fb.y;
                gyro_rad_s.z += integral_fb.z;
            } else {
                integral_fb = (imu_vec3f_t){0.0f, 0.0f, 0.0f};
            }

            gyro_rad_s.x += half_error.x * self->two_kp;
            gyro_rad_s.y += half_error.y * self->two_kp;
            gyro_rad_s.z += half_error.z * self->two_kp;
        }
    }

    const float half_dt = 0.5f * dt_s;
    imu_quatf_t integrated = {
        self->q.w + (-self->q.x * gyro_rad_s.x - self->q.y * gyro_rad_s.y -
                     self->q.z * gyro_rad_s.z) * half_dt,
        self->q.x + (self->q.w * gyro_rad_s.x + self->q.y * gyro_rad_s.z -
                     self->q.z * gyro_rad_s.y) * half_dt,
        self->q.y + (self->q.w * gyro_rad_s.y - self->q.x * gyro_rad_s.z +
                     self->q.z * gyro_rad_s.x) * half_dt,
        self->q.z + (self->q.w * gyro_rad_s.z + self->q.x * gyro_rad_s.y -
                     self->q.y * gyro_rad_s.x) * half_dt,
    };
    if (!normalize_quaternion(&integrated)) return false;

    self->q = integrated;
    self->integral_fb = integral_fb;
    self->valid = true;
    return true;
}

imu_quatf_t mahony_quaternion(const mahony_t *self)
{
    return self->q;
}

imu_vec3f_t mahony_euler_deg(const mahony_t *self)
{
    const imu_quatf_t q = self->q;
    const float roll = atan2f(2.0f * (q.w * q.x + q.y * q.z),
                              1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    const float pitch = asinf(clamp_unit(2.0f * (q.w * q.y - q.z * q.x)));
    const float yaw = atan2f(2.0f * (q.w * q.z + q.x * q.y),
                             1.0f - 2.0f * (q.y * q.y + q.z * q.z));

    return (imu_vec3f_t){
        wrap_deg(roll * RAD_TO_DEG),
        wrap_deg(pitch * RAD_TO_DEG),
        wrap_deg(yaw * RAD_TO_DEG),
    };
}
