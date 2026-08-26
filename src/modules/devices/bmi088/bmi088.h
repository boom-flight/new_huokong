/**
 * @file bmi088.h
 * @brief BMI088 加速度计和陀螺仪的总线无关驱动接口。
 */

#ifndef BMI088_H
#define BMI088_H

#include "attitude/imu_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief BMI088 内部目标器件。
 */
typedef enum {
    /** @brief 加速度计器件。 */
    BMI088_ACCEL,
    /** @brief 陀螺仪器件。 */
    BMI088_GYRO
} bmi088_target_t;

/**
 * @brief 从 BMI088 目标器件读取连续寄存器。
 * @param context 总线实现的上下文指针。
 * @param target 目标器件。
 * @param reg 起始寄存器地址。
 * @param data 接收数据的缓冲区。
 * @param length 要读取的字节数。
 * @return 读取成功时返回 true。
 */
typedef bool (*bmi088_read_fn)(void *context, bmi088_target_t target,
                              uint8_t reg, uint8_t *data, size_t length);
/**
 * @brief 向 BMI088 目标器件写入一个寄存器。
 * @param context 总线实现的上下文指针。
 * @param target 目标器件。
 * @param reg 寄存器地址。
 * @param value 要写入的值。
 * @return 写入成功时返回 true。
 */
typedef bool (*bmi088_write_fn)(void *context, bmi088_target_t target,
                               uint8_t reg, uint8_t value);
/**
 * @brief 延时指定的毫秒数。
 * @param context 总线实现的上下文指针。
 * @param delay_ms 延时时间，单位为毫秒。
 */
typedef void (*bmi088_delay_ms_fn)(void *context, uint32_t delay_ms);

/**
 * @brief BMI088 所需的抽象总线操作集合。
 */
typedef struct {
    /** @brief 传递给所有总线回调的上下文。 */
    void *context;
    /** @brief 读寄存器回调。 */
    bmi088_read_fn read;
    /** @brief 写寄存器回调。 */
    bmi088_write_fn write;
    /** @brief 延时回调。 */
    bmi088_delay_ms_fn delay_ms;
} bmi088_bus_t;

/**
 * @brief 传感器轴到机体轴的置换和符号映射。
 *
 * source_axis[i] 指定机体第 i 轴取用的传感器轴，sign[i] 必须为 1 或 -1。
 */
typedef struct {
    /** @brief 三个机体轴对应的传感器轴索引，取值范围为 0 至 2。 */
    uint8_t source_axis[3];
    /** @brief 三个机体轴对应的符号，取值只能为 1 或 -1。 */
    int8_t sign[3];
} bmi088_axis_map_t;

/**
 * @brief BMI088 原始三轴采样值。
 */
typedef struct {
    /** @brief X 轴有符号原始计数。 */
    int16_t x;
    /** @brief Y 轴有符号原始计数。 */
    int16_t y;
    /** @brief Z 轴有符号原始计数。 */
    int16_t z;
} bmi088_raw_sample_t;

/**
 * @brief BMI088 操作结果。
 */
typedef enum {
    /** @brief 操作成功。 */
    BMI088_OK = 0,
    /** @brief 参数为空或总线回调缺失。 */
    BMI088_BAD_ARGUMENT,
    /** @brief 总线读写失败。 */
    BMI088_BUS_ERROR,
    /** @brief 读取到的芯片 ID 不符合预期。 */
    BMI088_BAD_ID,
    /** @brief 寄存器写入后的回读校验失败。 */
    BMI088_VERIFY_ERROR,
    /** @brief 轴映射不是合法的右手坐标系。 */
    BMI088_BAD_AXIS_MAP
} bmi088_result_t;

/**
 * @brief BMI088 驱动实例及其轴映射配置。
 */
typedef struct {
    /** @brief 已绑定的抽象总线操作。 */
    bmi088_bus_t bus;
    /** @brief 传感器轴到机体轴的映射。 */
    bmi088_axis_map_t axis_map;
} bmi088_t;

/** @brief 默认的同向轴映射（传感器轴 XYZ 到机体轴 XYZ）。 */
extern const bmi088_axis_map_t BMI088_AXIS_MAP;

/**
 * @brief 检查轴映射是否为合法的右手坐标系。
 * @param map 待检查的轴映射。
 * @return 轴索引恰好构成置换、符号合法且整体保持右手性时返回 true。
 */
bool bmi088_axis_map_is_right_handed(const bmi088_axis_map_t *map);

/**
 * @brief 复位并配置 BMI088，同时校验两个芯片的 ID。
 * @param self 待初始化的驱动实例。
 * @param bus 总线读写和延时回调。
 * @param axis_map 传感器轴到机体轴的映射。
 * @param accel_id 可选的加速度计芯片 ID 输出指针。
 * @param gyro_id 可选的陀螺仪芯片 ID 输出指针。
 * @return 初始化及配置结果。
 * @note 配置寄存器会在写入后回读校验，失败时不会发布可用实例状态。
 */
bmi088_result_t bmi088_init(bmi088_t *self, bmi088_bus_t bus,
                            bmi088_axis_map_t axis_map, uint8_t *accel_id,
                            uint8_t *gyro_id);
/**
 * @brief 读取并转换一组三轴加速度数据。
 * @param self 已初始化的驱动实例。
 * @param raw 原始三轴数据输出指针。
 * @param body_g 按机体轴映射后的加速度输出，单位为 g。
 * @return 读取和转换结果。
 */
bmi088_result_t bmi088_read_accel(bmi088_t *self, bmi088_raw_sample_t *raw,
                                  imu_vec3f_t *body_g);

/**
 * @brief 读取并转换一组三轴陀螺仪数据。
 * @param self 已初始化的驱动实例。
 * @param raw 原始三轴数据输出指针。
 * @param body_dps 按机体轴映射后的角速度输出，单位为度每秒。
 * @return 读取和转换结果。
 */
bmi088_result_t bmi088_read_gyro(bmi088_t *self, bmi088_raw_sample_t *raw,
                                 imu_vec3f_t *body_dps);

#endif
