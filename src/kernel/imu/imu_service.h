/**
 * @file imu_service.h
 * @brief IMU 服务生命周期、快照读取和遥测丢弃统计接口。
 */

#ifndef IMU_SERVICE_H
#define IMU_SERVICE_H

#include "imu/imu_snapshot.h"

#include <stdbool.h>

/**
 * @brief 初始化 IMU 服务及其采样线程。
 *
 * @return 服务启动成功时为 true；资源或线程初始化失败时为 false。
 */
bool imu_service_init(void);

/**
 * @brief 停止 IMU 服务并释放其资源。
 *
 * @return 服务已停止或清理成功时为 true；线程未能在超时时间内停止时为 false。
 */
bool imu_service_deinit(void);

/**
 * @brief 读取当前已发布的 IMU 快照。
 *
 * @param[out] out 用于接收快照的目标对象，不能为 NULL。
 * @return 成功复制快照时为 true；out 为 NULL 时为 false。
 */
bool imu_snapshot_read(imu_snapshot_t *out);

/**
 * @brief 记录一次遥测发送丢弃。
 *
 * @note 计数器在达到 uint32_t 最大值后保持饱和。
 */
void imu_service_record_telemetry_drop(void);

#endif
