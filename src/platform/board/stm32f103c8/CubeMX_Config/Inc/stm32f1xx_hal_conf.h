/**
 * @file stm32f1xx_hal_conf.h
 * @brief STM32F1 HAL 模块、时钟参数和回调配置。
 * @note 该配置供本项目 STM32F103C8 板级适配使用，保持 HAL 断言为空操作。
 */

#ifndef STM32F1XX_HAL_CONF_H
#define STM32F1XX_HAL_CONF_H

/** @brief 启用项目所需的 STM32F1 HAL 外设模块。 */
#define HAL_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/** @brief 外部高速时钟频率，单位为 Hz。 */
#ifndef HSE_VALUE
#define HSE_VALUE 8000000U
#endif
/** @brief HSE 启动超时时间，单位为毫秒。 */
#define HSE_STARTUP_TIMEOUT 100U
#ifndef HSI_VALUE
#define HSI_VALUE 8000000U
#endif
/** @brief 低速内部时钟频率，单位为 Hz。 */
#define LSI_VALUE 40000U
/** @brief 低速外部时钟频率，单位为 Hz。 */
#define LSE_VALUE 32768U
/** @brief LSE 启动超时时间，单位为毫秒。 */
#define LSE_STARTUP_TIMEOUT 5000U

/** @brief 板级供电电压，单位为 mV。 */
#define VDD_VALUE 3300U
/** @brief HAL SysTick 中断优先级。 */
#define TICK_INT_PRIORITY 0U
/** @brief HAL 不直接使用 RTOS 适配层。 */
#define USE_RTOS 0U
/** @brief 启用 Flash 预取。 */
#define PREFETCH_ENABLE 1U

/** @brief 禁用 HAL 外设句柄的动态回调注册功能，使用静态回调入口。 */
#define USE_HAL_SPI_REGISTER_CALLBACKS 0U
#define USE_HAL_TIM_REGISTER_CALLBACKS 0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U
/** @brief 禁用 SPI 硬件 CRC。 */
#define USE_SPI_CRC 0U

#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_exti.h"
#include "stm32f1xx_hal_dma.h"
#include "stm32f1xx_hal_cortex.h"
#include "stm32f1xx_hal_flash.h"
#include "stm32f1xx_hal_pwr.h"
#include "stm32f1xx_hal_spi.h"
#include "stm32f1xx_hal_tim.h"
#include "stm32f1xx_hal_uart.h"

/** @brief 裁剪版固件不执行 HAL 参数断言。 */
#define assert_param(expr) ((void)0U)

#endif
