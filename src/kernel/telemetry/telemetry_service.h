/**
 * @file telemetry_service.h
 * @brief 遥测 UART 服务生命周期接口和线程配置。
 */

#ifndef TELEMETRY_SERVICE_H
#define TELEMETRY_SERVICE_H

#include <stdbool.h>

/** @brief 遥测线程的 RT-Thread 优先级。 */
#define TELEMETRY_THREAD_PRIORITY 15u

/** @brief 遥测线程栈大小，单位为字节。 */
#define TELEMETRY_THREAD_STACK_SIZE 512u

/**
 * @brief 初始化遥测 UART 和发送线程。
 *
 * @return 服务启动成功时为 true；底层 UART 或线程初始化失败时为 false。
 */
bool telemetry_service_init(void);

/**
 * @brief 停止遥测发送线程并释放 UART 资源。
 *
 * @return 服务已停止或清理成功时为 true；线程未能在超时时间内停止时为 false。
 */
bool telemetry_service_deinit(void);

#endif
