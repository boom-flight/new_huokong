/**
 * @file monotonic_clock_stm32.h
 * @brief STM32 单调微秒时钟的公开接口。
 * @note 时钟值基于 TIM2 的 16 位计数器和更新中断扩展。
 */

#ifndef MONOTONIC_CLOCK_STM32_H
#define MONOTONIC_CLOCK_STM32_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化 TIM2 单调时钟及其更新中断。
 * @return 初始化成功或已完成初始化时返回 true，否则返回 false。
 * @note 当前 STM32F103 时钟配置下，TIM2 计数频率为 1 MHz。
 */
bool monotonic_clock_stm32_init(void);

/**
 * @brief 停止并清理 TIM2 单调时钟。
 */
void monotonic_clock_stm32_deinit(void);

/**
 * @brief 获取当前单调时间。
 * @return 自时钟初始化以来的单调时间，单位为微秒；未初始化时返回 0。
 * @note 读取同时考虑计数器回卷和待处理的更新标志。
 */
uint32_t monotonic_clock_stm32_now_us(void);

#endif
