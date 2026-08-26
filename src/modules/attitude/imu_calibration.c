/**
 * @file imu_calibration.c
 * @brief 静止 IMU 样本校准的累计和判定实现。
 */

#include "attitude/imu_calibration.h"

#include <math.h>

/**
 * @brief 逐分量相加两个三维向量。
 * @param lhs 左操作数。
 * @param rhs 右操作数。
 * @return 两个向量的逐分量和。
 */
static imu_vec3f_t vec_add(imu_vec3f_t lhs, imu_vec3f_t rhs)
{
    return (imu_vec3f_t){lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

/**
 * @brief 将三维向量的每个分量乘以同一个标量。
 * @param value 待缩放的向量。
 * @param scale 缩放因子。
 * @return 缩放后的向量。
 */
static imu_vec3f_t vec_scale(imu_vec3f_t value, float scale)
{
    return (imu_vec3f_t){value.x * scale, value.y * scale, value.z * scale};
}

void imu_calibration_init(imu_calibration_t *self)
{
    *self = (imu_calibration_t){0};
}

bool imu_calibration_accel_stationary(imu_vec3f_t accel_g)
{
    const float norm = sqrtf(accel_g.x * accel_g.x +
                             accel_g.y * accel_g.y +
                             accel_g.z * accel_g.z);

    return isfinite(norm) && norm >= 0.9f && norm <= 1.1f;
}

bool imu_calibration_gyro_stationary(imu_vec3f_t gyro_dps)
{
    const float norm = sqrtf(gyro_dps.x * gyro_dps.x +
                             gyro_dps.y * gyro_dps.y +
                             gyro_dps.z * gyro_dps.z);

    return isfinite(norm) && norm < 3.0f;
}

imu_calibration_step_t imu_calibration_push(imu_calibration_t *self,
                                             imu_vec3f_t accel_g,
                                             imu_vec3f_t gyro_dps,
                                             bool timestamp_valid)
{
    if (self->complete) return IMU_CALIBRATION_COMPLETE;

    if (!timestamp_valid || !imu_calibration_accel_stationary(accel_g) ||
        !imu_calibration_gyro_stationary(gyro_dps)) {
        imu_calibration_init(self);
        return IMU_CALIBRATION_RESET;
    }

    self->gyro_sum_dps = vec_add(self->gyro_sum_dps, gyro_dps);
    self->accel_sum_g = vec_add(self->accel_sum_g, accel_g);
    ++self->accepted;

    if (self->accepted == IMU_CALIBRATION_SAMPLE_COUNT) {
        const float reciprocal_count =
            1.0f / (float)IMU_CALIBRATION_SAMPLE_COUNT;
        self->gyro_bias_dps = vec_scale(self->gyro_sum_dps, reciprocal_count);
        self->gravity_g = vec_scale(self->accel_sum_g, reciprocal_count);
        self->complete = true;
        return IMU_CALIBRATION_COMPLETE;
    }

    return IMU_CALIBRATION_ACCEPTED;
}
