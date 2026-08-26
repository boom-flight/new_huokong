/**
 * @file mahony.h
 * @brief Mahony 姿态解算器接口。
 */

#ifndef MAHONY_H
#define MAHONY_H

#include <stdbool.h>

#include "attitude/imu_types.h"

/**
 * @brief Mahony 姿态解算器的状态。
 */
typedef struct {
    /** @brief 当前姿态四元数，排列为 w、x、y、z。 */
    imu_quatf_t q;
    /** @brief 积分反馈项，单位与陀螺仪输入一致，为弧度每秒。 */
    imu_vec3f_t integral_fb;
    /** @brief 比例增益的两倍。 */
    float two_kp;
    /** @brief 积分增益的两倍。 */
    float two_ki;
    /** @brief 当前四元数是否经过有效初始化或更新。 */
    bool valid;
} mahony_t;

/**
 * @brief 根据重力方向初始化姿态解算器。
 * @param self 待初始化的解算器状态。
 * @param accel_g 加速度向量，单位为 g；用于确定横滚和俯仰。
 * @param kp 比例反馈增益。
 * @param ki 积分反馈增益。
 * @note 输入无效时保留单位四元数，但将 valid 置为 false。
 */
void mahony_init_from_gravity(mahony_t *self,
                              imu_vec3f_t accel_g,
                              float kp,
                              float ki);
/**
 * @brief 使解算器失效并清除积分反馈。
 * @param self 待失效的解算器状态。
 */
void mahony_invalidate(mahony_t *self);

/**
 * @brief 使用陀螺仪积分并按条件施加加速度反馈。
 * @param self 姿态解算器状态。
 * @param gyro_rad_s 角速度向量，单位为弧度每秒。
 * @param accel_g 加速度向量，单位为 g。
 * @param correction_valid 是否允许使用加速度反馈校正。
 * @param dt_s 本次更新的时间间隔，单位为秒。
 * @return 更新成功且姿态四元数保持有限时返回 true。
 * @note 仅接受 0.0005 秒至 0.002 秒范围内的时间间隔。
 */
bool mahony_update(mahony_t *self,
                   imu_vec3f_t gyro_rad_s,
                   imu_vec3f_t accel_g,
                   bool correction_valid,
                   float dt_s);
/**
 * @brief 读取当前姿态四元数。
 * @param self 姿态解算器状态。
 * @return 当前姿态四元数。
 */
imu_quatf_t mahony_quaternion(const mahony_t *self);

/**
 * @brief 将当前姿态四元数转换为欧拉角。
 * @param self 姿态解算器状态。
 * @return 按横滚、俯仰、偏航排列的角度向量，单位为度，范围约为 [-180, 180)。
 */
imu_vec3f_t mahony_euler_deg(const mahony_t *self);

#endif
