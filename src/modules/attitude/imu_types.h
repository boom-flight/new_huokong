/**
 * @file imu_types.h
 * @brief IMU 姿态计算使用的基础向量和四元数类型。
 */

#ifndef IMU_TYPES_H
#define IMU_TYPES_H

/**
 * @brief 三维单精度浮点向量。
 *
 * 向量的物理单位由使用它的接口定义，例如 g、度每秒或弧度每秒。
 */
typedef struct {
    /** @brief X 轴分量。 */
    float x;
    /** @brief Y 轴分量。 */
    float y;
    /** @brief Z 轴分量。 */
    float z;
} imu_vec3f_t;

/**
 * @brief 单精度浮点四元数，采用 w、x、y、z 排列。
 */
typedef struct {
    /** @brief 标量分量。 */
    float w;
    /** @brief X 轴虚部。 */
    float x;
    /** @brief Y 轴虚部。 */
    float y;
    /** @brief Z 轴虚部。 */
    float z;
} imu_quatf_t;

#endif
