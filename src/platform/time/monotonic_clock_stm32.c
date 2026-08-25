#include "monotonic_clock_stm32.h"

#include "main.h"
#include "timing/timestamp_extender.h"

#include <rtthread.h>

static TIM_HandleTypeDef htim2;
static volatile uint16_t timestamp_high_word;
static bool clock_initialized;

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

void monotonic_clock_stm32_deinit(void)
{
    cleanup_clock();
}

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

void TIM2_IRQHandler(void)
{
    rt_interrupt_enter();
    if (clock_initialized && htim2.Instance == TIM2) {
        HAL_TIM_IRQHandler(&htim2);
    }
    rt_interrupt_leave();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (clock_initialized && htim != NULL && htim->Instance == TIM2) {
        ++timestamp_high_word;
    }
}
