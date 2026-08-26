/**
 * @file imu_snapshot.h
 * @brief IMU 状态位、诊断计数器和对外数据快照定义。
 */

#ifndef IMU_SNAPSHOT_H
#define IMU_SNAPSHOT_H

#include "attitude/imu_types.h"

#include <stdint.h>

/** @brief IMU 快照中的状态位。 */
enum {
    /** @brief 姿态估计结果有效。 */
    IMU_STATUS_VALID = 1u << 0,
    /** @brief IMU 正在校准。 */
    IMU_STATUS_CALIBRATING = 1u << 1,
    /** @brief BMI088 初始化失败。 */
    IMU_STATUS_BMI_INIT_FAILED = 1u << 2,
    /** @brief 陀螺仪原始读数达到饱和边界。 */
    IMU_STATUS_GYRO_SATURATED = 1u << 3,
    /** @brief 加速度不能用于姿态校正。 */
    IMU_STATUS_ACCEL_CORRECTION_INVALID = 1u << 4,
    /** @brief 采样时间基线或时间间隔无效。 */
    IMU_STATUS_TIMESTAMP_INVALID = 1u << 5,
    /** @brief SPI 采样读取失败。 */
    IMU_STATUS_SPI_ERROR = 1u << 6,
    /** @brief 数据就绪事件期间发生样本覆盖。 */
    IMU_STATUS_EVENT_OVERRUN = 1u << 7,
    /** @brief 至少一次遥测发送被丢弃。 */
    IMU_STATUS_TELEMETRY_DROPPED = 1u << 8
};

/** @brief IMU 运行诊断计数器。 */
typedef struct {
    /** @brief 成功处理的加速度样本数。 */
    uint32_t accel_samples;
    /** @brief 成功处理的陀螺仪样本数。 */
    uint32_t gyro_samples;
    /** @brief 加速度事件或样本覆盖次数。 */
    uint32_t accel_overruns;
    /** @brief 陀螺仪事件或样本覆盖次数。 */
    uint32_t gyro_overruns;
    /** @brief 传感器 SPI 读取错误次数。 */
    uint32_t spi_errors;
    /** @brief 被时间间隔策略拒绝的积分次数。 */
    uint32_t rejected_dt;
    /** @brief 导致估计器失效的长时间间隔次数。 */
    uint32_t long_gaps;
    /** @brief 传感器重新初始化次数。 */
    uint32_t sensor_reinitializations;
    /** @brief 遥测发送丢弃次数。 */
    uint32_t telemetry_drops;
} imu_diagnostics_t;

/** @brief 对外发布的一致 IMU 数据快照。 */
typedef struct {
    /** @brief 快照时间戳，单位为微秒。 */
    uint32_t timestamp_us;
    /** @brief IMU_STATUS_* 状态位掩码。 */
    uint16_t status;
    /** @brief 当前姿态四元数。 */
    imu_quatf_t quaternion;
    /** @brief 当前欧拉角，单位为度。 */
    imu_vec3f_t euler_deg;
    /** @brief 去偏后的角速度，单位为度/秒。 */
    imu_vec3f_t gyro_dps;
    /** @brief 加速度，单位为 g。 */
    imu_vec3f_t accel_g;
    /** @brief 运行诊断计数器。 */
    imu_diagnostics_t diagnostics;
} imu_snapshot_t;

#endif
