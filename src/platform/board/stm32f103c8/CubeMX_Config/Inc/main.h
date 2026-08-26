/**
 * @file main.h
 * @brief CubeMX 生成入口所需的 HAL 声明。
 * @note 项目平台代码通过本头文件获取 STM32F1 HAL 和错误处理入口。
 */

#ifndef MAIN_H
#define MAIN_H

#include <stm32f1xx_hal.h>

/**
 * @brief 处理不可恢复的 HAL 初始化错误。
 */
void Error_Handler(void);

#endif
