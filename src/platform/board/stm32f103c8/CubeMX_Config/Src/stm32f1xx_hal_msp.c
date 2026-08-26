/**
 * @file stm32f1xx_hal_msp.c
 * @brief STM32F1 HAL 底层硬件资源初始化和反初始化回调。
 * @note 本文件只配置 HAL 通用资源和 USART1 引脚；USART2、SPI1、TIM2 等资源由平台适配器自行管理。
 */

#include "main.h"

/**
 * @brief 初始化 HAL 通用底层资源。
 * @note 开启 AFIO/PWR 时钟并关闭 JTAG、保留 SWD 调试接口。
 */
void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

/**
 * @brief 初始化 USART1 的时钟和 GPIO。
 * @param uart 待初始化的 UART 句柄。
 * @note 仅为 USART1 配置 PA9 TX 和 PA10 RX；其他 UART 实例由各自适配器处理。
 */
void HAL_UART_MspInit(UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = {0};

    if (uart->Instance != USART1)
    {
        return;
    }

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/**
 * @brief 释放 USART1 的时钟和 GPIO 资源。
 * @param uart 待反初始化的 UART 句柄。
 */
void HAL_UART_MspDeInit(UART_HandleTypeDef *uart)
{
    if (uart->Instance != USART1)
    {
        return;
    }

    __HAL_RCC_USART1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
}
