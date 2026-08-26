/**
 * @file imu_log_service.h
 * @brief IMU 异步日志服务生命周期和事件提交接口。
 */

#ifndef IMU_LOG_SERVICE_H
#define IMU_LOG_SERVICE_H

#include "logging/imu_log_event.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化 IMU 日志队列和日志线程。
 *
 * @return 日志服务启用成功时为 true；初始化失败时为 false。
 */
bool imu_log_service_init(void);

/**
 * @brief 停止 IMU 日志线程并释放日志队列。
 *
 * @return 服务已停止或清理成功时为 true；线程未能在超时时间内停止时为 false。
 */
bool imu_log_service_deinit(void);

/**
 * @brief 将一条 IMU 日志事件提交到异步日志队列。
 *
 * @param event 要提交的日志事件。
 * @return 事件成功进入队列时为 true；服务未启用或队列已满时为 false。
 */
bool imu_log_submit(imu_log_event_t event);

/**
 * @brief 读取 IMU 日志队列累计丢弃数。
 *
 * @return 累计丢弃的日志事件数。
 */
uint32_t imu_log_drop_count(void);

#endif
