/**
 * @file telemetry_uart_stm32.c
 * @brief 基于 STM32 HAL 和 DMA 的遥测 UART 发送实现。
 * @note 本文件仅实现 USART2 TX；发送使用 DMA1 通道 7，TX 引脚为 PA2。
 */

#include "telemetry_uart_stm32.h"

#include "main.h"
#include "transport/dma_tx_state.h"

#include <rtthread.h>

enum {
    /** @brief 适配器接受的固定遥测帧长度，单位为字节。 */
    TELEMETRY_FRAME_SIZE = 40u,
};

/** @brief UART、DMA 句柄及其发送状态，仅由本适配器持有。 */
static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef hdma_usart2_tx;
static dma_tx_state_t telemetry_dma_state;
static bool uart_initialized;

/**
 * @brief 保存当前中断屏蔽状态并临时关闭中断。
 * @return 调用前的 PRIMASK 值，供 leave_critical() 恢复。
 */
static uint32_t enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

/**
 * @brief 恢复进入临界区前的中断屏蔽状态。
 * @param primask enter_critical() 返回的 PRIMASK 值。
 */
static void leave_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/**
 * @brief 回滚 UART 初始化并清除 DMA、IRQ 和发送状态。
 * @note 该函数同时用于初始化失败路径和显式反初始化。
 */
static void cleanup_uart(void)
{
    uart_initialized = false;

    HAL_NVIC_DisableIRQ(DMA1_Channel7_IRQn);
    HAL_NVIC_DisableIRQ(USART2_IRQn);

    if (huart2.Instance == USART2) {
        (void)HAL_UART_DeInit(&huart2);
    }
    if (hdma_usart2_tx.Instance == DMA1_Channel7) {
        (void)HAL_DMA_DeInit(&hdma_usart2_tx);
    }

    HAL_NVIC_ClearPendingIRQ(DMA1_Channel7_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);

    dma_tx_state_reset(&telemetry_dma_state);
    huart2 = (UART_HandleTypeDef){0};
    hdma_usart2_tx = (DMA_HandleTypeDef){0};
}

/**
 * @brief 初始化 USART2 TX、DMA1 通道 7 及对应中断。
 * @return 初始化成功或已完成初始化时返回 true，否则返回 false。
 * @note 初始化失败时会通过 cleanup_uart() 清理部分已创建的资源。
 */
bool telemetry_uart_stm32_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (uart_initialized) {
        return true;
    }

    dma_tx_state_reset(&telemetry_dma_state);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    hdma_usart2_tx.Instance = DMA1_Channel7;
    hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK) {
        cleanup_uart();
        return false;
    }

    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200u;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    __HAL_LINKDMA(&huart2, hdmatx, hdma_usart2_tx);
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        cleanup_uart();
        return false;
    }

    uart_initialized = true;
    HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 6u, 0u);
    HAL_NVIC_SetPriority(USART2_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    return true;
}

/**
 * @brief 关闭 USART2 DMA 发送适配器。
 */
void telemetry_uart_stm32_deinit(void)
{
    cleanup_uart();
}

/**
 * @brief 启动固定长度遥测帧的异步发送。
 * @param frame 待发送的帧缓冲区。
 * @param length 帧长度，必须为 TELEMETRY_FRAME_SIZE。
 * @return 发送启动结果，见 telemetry_uart_send_result_t。
 * @note 发送占用由 dma_tx_state_t 串行化，DMA 完成或错误回调负责释放状态。
 */
telemetry_uart_send_result_t telemetry_uart_stm32_send(const uint8_t *frame,
                                                       size_t length)
{
    uint32_t primask;

    if (frame == NULL || length != TELEMETRY_FRAME_SIZE) {
        return TELEMETRY_UART_SEND_START_FAILED;
    }
    primask = enter_critical();
    if (!uart_initialized || huart2.Instance != USART2 ||
        hdma_usart2_tx.Instance != DMA1_Channel7) {
        leave_critical(primask);
        return TELEMETRY_UART_SEND_START_FAILED;
    }
    if (dma_tx_state_take_failure(&telemetry_dma_state)) {
        leave_critical(primask);
        return TELEMETRY_UART_SEND_ASYNC_ERROR;
    }
    if (!dma_tx_state_reserve(&telemetry_dma_state)) {
        leave_critical(primask);
        return TELEMETRY_UART_SEND_BUSY;
    }
    leave_critical(primask);

    if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)frame,
                              (uint16_t)length) != HAL_OK) {
        primask = enter_critical();
        dma_tx_state_release(&telemetry_dma_state);
        leave_critical(primask);
        return TELEMETRY_UART_SEND_START_FAILED;
    }
    return TELEMETRY_UART_SEND_STARTED;
}

/**
 * @brief 处理 DMA1 通道 7 中断并转交 STM32 HAL。
 * @note 该函数运行在 RT-Thread 中断上下文中。
 */
void DMA1_Channel7_IRQHandler(void)
{
    rt_interrupt_enter();
    if (uart_initialized && hdma_usart2_tx.Instance == DMA1_Channel7) {
        HAL_DMA_IRQHandler(&hdma_usart2_tx);
    }
    rt_interrupt_leave();
}

/**
 * @brief 处理 USART2 中断并转交 STM32 HAL。
 * @note 该函数运行在 RT-Thread 中断上下文中。
 */
void USART2_IRQHandler(void)
{
    rt_interrupt_enter();
    if (uart_initialized && huart2.Instance == USART2) {
        HAL_UART_IRQHandler(&huart2);
    }
    rt_interrupt_leave();
}

/**
 * @brief 处理 UART DMA 发送完成回调。
 * @param huart 触发回调的 UART 句柄。
 * @note 仅接受 USART2 的回调，并释放对应的发送占用状态。
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    const uint32_t primask = enter_critical();

    if (uart_initialized && huart != NULL && huart->Instance == USART2) {
        dma_tx_state_release(&telemetry_dma_state);
    }
    leave_critical(primask);
}

/**
 * @brief 处理 UART 异步错误回调。
 * @param huart 触发回调的 UART 句柄。
 * @note 仅接受 USART2 的回调，并记录错误供后续发送请求读取。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    const uint32_t primask = enter_critical();

    if (uart_initialized && huart != NULL && huart->Instance == USART2) {
        dma_tx_state_async_error(&telemetry_dma_state);
    }
    leave_critical(primask);
}
