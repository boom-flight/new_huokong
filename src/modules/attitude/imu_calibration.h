/**
 * @file imu_calibration.h
 * @brief 静止状态下的 IMU 零偏和重力向量校准接口。
 */

#ifndef IMU_CALIBRATION_H
#define IMU_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "attitude/imu_types.h"

/** @brief 完成一次校准所需的有效静止样本数。 */
#define IMU_CALIBRATION_SAMPLE_COUNT 2000u

/**
 * @brief 单次校准样本的处理结果。
 */
typedef enum {
    /** @brief 当前样本无效，累计状态已重置。 */
    IMU_CALIBRATION_RESET,
    /** @brief 当前样本有效，累计尚未完成。 */
    IMU_CALIBRATION_ACCEPTED,
    /** @brief 已收集足够样本并计算出校准结果。 */
    IMU_CALIBRATION_COMPLETE,
} imu_calibration_step_t;

/**
 * @brief IMU 校准的累计状态和结果。
 */
typedef struct {
    /** @brief 已接受的静止样本数量。 */
    uint32_t accepted;
    /** @brief 陀螺仪样本和，单位为度每秒。 */
    imu_vec3f_t gyro_sum_dps;
    /** @brief 加速度样本和，单位为 g。 */
    imu_vec3f_t accel_sum_g;
    /** @brief 计算得到的陀螺仪零偏，单位为度每秒。 */
    imu_vec3f_t gyro_bias_dps;
    /** @brief 计算得到的重力方向向量，单位为 g。 */
    imu_vec3f_t gravity_g;
    /** @brief 是否已完成当前校准周期。 */
    bool complete;
} imu_calibration_t;

/**
 * @brief 将校准状态清零并开始新的校准周期。
 * @param self 待初始化的校准状态。
 */
void imu_calibration_init(imu_calibration_t *self);

/**
 * @brief 判断加速度样本的模长是否落在静止判定范围内。
 * @param accel_g 加速度向量，单位为 g。
 * @return 样本有限且模长位于 0.9 g 至 1.1 g（含边界）时返回 true。
 */
bool imu_calibration_accel_stationary(imu_vec3f_t accel_g);

/**
 * @brief 判断陀螺仪样本的角速度是否落在静止判定范围内。
 * @param gyro_dps 角速度向量，单位为度每秒。
 * @return 样本有限且模长小于 3 度每秒时返回 true。
 */
bool imu_calibration_gyro_stationary(imu_vec3f_t gyro_dps);

/**
 * @brief 校验并累计一个 IMU 校准样本。
 * @param self 校准状态；未完成时无效样本会清零该状态，已完成时直接返回完成状态。
 * @param accel_g 加速度向量，单位为 g。
 * @param gyro_dps 角速度向量，单位为度每秒。
 * @param timestamp_valid 当前样本的时间戳是否有效。
 * @return 当前样本导致的校准状态变化；若校准已完成则直接返回完成状态。
 * @note 收集到 IMU_CALIBRATION_SAMPLE_COUNT 个有效样本后，结果字段会被更新。
 */
imu_calibration_step_t imu_calibration_push(imu_calibration_t *self,
                                             imu_vec3f_t accel_g,
                                             imu_vec3f_t gyro_dps,
                                             bool timestamp_valid);

#endif
