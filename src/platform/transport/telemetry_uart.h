/**
 * @file telemetry_uart.h
 * @brief 遥测 UART 异步发送平台契约。
 */

#ifndef TELEMETRY_UART_H
#define TELEMETRY_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 一次遥测帧发送请求的启动或异步状态。 */
typedef enum {
    /** @brief DMA 发送已成功启动。 */
    TELEMETRY_UART_SEND_STARTED,
    /** @brief UART DMA 当前已有发送任务。 */
    TELEMETRY_UART_SEND_BUSY,
    /** @brief 发送未能启动。 */
    TELEMETRY_UART_SEND_START_FAILED,
    /** @brief 上一次 DMA 发送发生了异步错误。 */
    TELEMETRY_UART_SEND_ASYNC_ERROR,
} telemetry_uart_send_result_t;

/**
 * @brief 初始化遥测 UART 及其异步发送资源。
 * @return 初始化成功或已完成初始化时返回 true，否则返回 false。
 */
bool telemetry_uart_init(void);

/**
 * @brief 关闭遥测 UART 并清理异步发送状态。
 */
void telemetry_uart_deinit(void);

/**
 * @brief 启动一帧遥测数据的异步发送。
 * @param frame 待发送的完整遥测帧缓冲区。
 * @param length 帧长度，必须等于适配器要求的固定帧长度。
 * @return 发送启动结果，见 telemetry_uart_send_result_t。
 * @note 调用者必须保证 frame 在异步发送完成前保持有效且内容不变。
 */
telemetry_uart_send_result_t telemetry_uart_send(const uint8_t *frame,
                                                 size_t length);

#endif
