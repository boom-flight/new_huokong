/**
 * @file imu_log_event.h
 * @brief IMU 日志事件类型、字段和文本格式化接口。
 */

#ifndef IMU_LOG_EVENT_H
#define IMU_LOG_EVENT_H

#include "imu/imu_policy.h"
#include "imu/imu_snapshot.h"

#include <stddef.h>
#include <stdint.h>

/** @brief IMU 日志事件的类型。 */
typedef enum {
    /** @brief 记录服务启动时的初始状态。 */
    IMU_LOG_INITIAL_STATE,
    /** @brief 记录一次状态转换。 */
    IMU_LOG_STATE,
    /** @brief 记录 BMI088 加速度计和陀螺仪 ID。 */
    IMU_LOG_IDS,
    /** @brief 记录校准完成。 */
    IMU_LOG_CALIBRATION_COMPLETE,
    /** @brief 记录周期性诊断计数器。 */
    IMU_LOG_DIAGNOSTICS
} imu_log_event_kind_t;

/** @brief 可异步提交并格式化的 IMU 日志事件。 */
typedef struct {
    /** @brief 事件类型。 */
    imu_log_event_kind_t kind;
    /** @brief 状态转换前的 IMU 状态。 */
    imu_state_t previous_state;
    /** @brief 当前或状态转换后的 IMU 状态。 */
    imu_state_t state;
    /** @brief BMI088 加速度计 ID。 */
    uint8_t accel_id;
    /** @brief BMI088 陀螺仪 ID。 */
    uint8_t gyro_id;
    /** @brief 事件对应的诊断计数器快照。 */
    imu_diagnostics_t diagnostics;
} imu_log_event_t;

/**
 * @brief 将 IMU 日志事件格式化为以空字符结尾的文本。
 *
 * @param event 要格式化的事件，不能为 NULL。
 * @param[out] buffer 接收文本的缓冲区，不能为 NULL。
 * @param capacity 缓冲区容量，包含结尾空字符的空间。
 * @return 未截断文本的长度；参数无效或事件类型未知时返回 0。
 * @note 当缓冲区不足时，输出会被截断，但返回值仍表示完整文本长度。
 */
size_t imu_log_event_format(const imu_log_event_t *event, char *buffer,
                            size_t capacity);

#endif
