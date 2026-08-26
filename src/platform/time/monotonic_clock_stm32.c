/**
 * @file monotonic_clock_stm32.c
 * @brief 使用 STM32F103C8 TIM2 实现单调微秒时钟。
 * @note TIM2 以 1 MHz 计数，更新中断维护高 16 位，读取时由临界区保证一致性。
 */

#include "monotonic_clock_stm32.h"

#include "main.h"
#include "timing/timestamp_extender.h"

#include <rtthread.h>

/** @brief TIM2 句柄和用于扩展 16 位计数器的高位字。 */
static TIM_HandleTypeDef htim2;
static volatile uint16_t timestamp_high_word;
static bool clock_initialized;

/**
 * @brief 停止 TIM2 并清除更新中断、NVIC 挂起位及软件时间状态。
 */
static void cleanup_clock(void)
{
    clock_initialized = false;

    HAL_NVIC_DisableIRQ(TIM2_IRQn);
    if (htim2.Instance == TIM2) {
        (void)HAL_TIM_Base_Stop_IT(&htim2);
        __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
        (void)HAL_TIM_Base_DeInit(&htim2);
    }
    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);

    timestamp_high_word = 0u;
    htim2 = (TIM_HandleTypeDef){0};
}

/**
 * @brief 配置 TIM2 为 1 MHz、16 位自动重装载计数器。
 * @return HAL 配置成功返回 true，否则返回 false。
 */
static bool init_timestamp_timer(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 71u;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 65535u;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) {
        return false;
    }
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    return true;
}

/**
 * @brief 初始化 TIM2 单调时钟及其周期更新中断。
 * @return 初始化成功或已完成初始化时返回 true，否则返回 false。
 * @note 初始化失败时会清理已配置的 TIM2 资源。
 */
bool monotonic_clock_stm32_init(void)
{
    if (clock_initialized) {
        return true;
    }

    timestamp_high_word = 0u;
    if (!init_timestamp_timer() ||
        HAL_TIM_Base_Start_IT(&htim2) != HAL_OK) {
        cleanup_clock();
        return false;
    }

    clock_initialized = true;
    HAL_NVIC_SetPriority(TIM2_IRQn, 5u, 0u);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    return true;
}

/**
 * @brief 关闭 TIM2 单调时钟。
 */
void monotonic_clock_stm32_deinit(void)
{
    cleanup_clock();
}

/**
 * @brief 原子读取并扩展 TIM2 当前计数值。
 * @return 单调时间，单位为微秒；时钟未初始化时返回 0。
 */
uint32_t monotonic_clock_stm32_now_us(void)
{
    uint16_t high_word;
    uint16_t counter;
    bool update_pending;
    const uint32_t interrupt_mask = __get_PRIMASK();

    __disable_irq();

    if (!clock_initialized || htim2.Instance != TIM2) {
        __set_PRIMASK(interrupt_mask);
        return 0u;
    }
    high_word = timestamp_high_word;
    counter = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
    update_pending = __HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_UPDATE) != RESET;
    __set_PRIMASK(interrupt_mask);
    return timestamp_extender_compose(high_word, counter, update_pending);
}

/**
 * @brief 处理 TIM2 更新中断并转交 STM32 HAL。
 * @note 该函数运行在 RT-Thread 中断上下文中。
 */
void TIM2_IRQHandler(void)
{
    rt_interrupt_enter();
    if (clock_initialized && htim2.Instance == TIM2) {
        HAL_TIM_IRQHandler(&htim2);
    }
    rt_interrupt_leave();
}

/**
 * @brief 处理 TIM2 周期到期回调并扩展计数器高位。
 * @param htim 触发回调的定时器句柄。
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (clock_initialized && htim != NULL && htim->Instance == TIM2) {
        ++timestamp_high_word;
    }
}
