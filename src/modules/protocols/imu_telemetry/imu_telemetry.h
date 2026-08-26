/**
 * @file imu_telemetry.h
 * @brief IMU 遥测 v2 固定长度帧的编码接口。
 */

#ifndef IMU_TELEMETRY_H
#define IMU_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "attitude/imu_types.h"

/** @brief 遥测帧总长度，单位为字节。 */
#define IMU_TELEMETRY_FRAME_SIZE 40u
/** @brief 当前遥测帧格式版本。 */
#define IMU_TELEMETRY_VERSION 2u

/**
 * @brief 一帧遥测数据在编码前的物理量表示。
 */
typedef struct {
    /** @brief 采样时间戳，单位为微秒。 */
    uint32_t timestamp_us;
    /** @brief 状态位字段，仅低 9 位写入帧。 */
    uint16_t status;
    /** @brief 横滚、俯仰、偏航角，单位为度。 */
    imu_vec3f_t euler_deg;
    /** @brief 三轴角速度，单位为度每秒。 */
    imu_vec3f_t gyro_dps;
    /** @brief 三轴加速度，单位为 g。 */
    imu_vec3f_t accel_g;
    /** @brief 当前姿态四元数，排列为 w、x、y、z。 */
    imu_quatf_t quaternion;
} imu_telemetry_sample_t;

/**
 * @brief 计算 CRC-16/CCITT-FALSE 校验值。
 * @param data 待校验的数据缓冲区。
 * @param length 待校验的字节数。
 * @return 初值为 0xFFFF、生成多项式为 0x1021 的 CRC 值。
 */
uint16_t imu_telemetry_crc16_ccitt_false(const uint8_t *data, size_t length);

/**
 * @brief 将一帧遥测样本编码为固定长度的小端字节帧。
 * @param sequence 帧序号。
 * @param sample 待编码的遥测样本。
 * @param frame 接收 IMU_TELEMETRY_FRAME_SIZE 字节帧的缓冲区。
 * @note 浮点值会按协议缩放为有符号 16 位整数，非有限值编码为零，超范围值饱和。
 */
void imu_telemetry_encode(uint16_t sequence,
                          const imu_telemetry_sample_t *sample,
                          uint8_t frame[IMU_TELEMETRY_FRAME_SIZE]);

#endif
