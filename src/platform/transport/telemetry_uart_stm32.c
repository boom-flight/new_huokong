#include "telemetry_uart_stm32.h"

#include "main.h"
#include "transport/dma_tx_state.h"

#include <rtthread.h>

enum {
    TELEMETRY_FRAME_SIZE = 32u,
};

static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef hdma_usart2_tx;
static dma_tx_state_t telemetry_dma_state;
static bool uart_initialized;

static uint32_t enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

static void leave_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

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

void telemetry_uart_stm32_deinit(void)
{
    cleanup_uart();
}

bool telemetry_uart_stm32_try_start(const uint8_t *frame, size_t length)
{
    uint32_t primask;

    if (frame == NULL || length != TELEMETRY_FRAME_SIZE) {
        return false;
    }
    primask = enter_critical();
    if (!uart_initialized || huart2.Instance != USART2 ||
        hdma_usart2_tx.Instance != DMA1_Channel7 ||
        !dma_tx_state_reserve(&telemetry_dma_state)) {
        leave_critical(primask);
        return false;
    }
    leave_critical(primask);

    if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)frame,
                              (uint16_t)length) != HAL_OK) {
        primask = enter_critical();
        dma_tx_state_cancel(&telemetry_dma_state);
        leave_critical(primask);
        return false;
    }
    return true;
}

bool telemetry_uart_stm32_busy(void)
{
    bool busy;
    const uint32_t primask = enter_critical();

    busy = uart_initialized && dma_tx_state_busy(&telemetry_dma_state);
    leave_critical(primask);
    return busy;
}

bool telemetry_uart_stm32_take_failure(void)
{
    bool failed;
    const uint32_t primask = enter_critical();

    failed = uart_initialized &&
             dma_tx_state_take_failure(&telemetry_dma_state);
    leave_critical(primask);
    return failed;
}

void DMA1_Channel7_IRQHandler(void)
{
    rt_interrupt_enter();
    if (uart_initialized && hdma_usart2_tx.Instance == DMA1_Channel7) {
        HAL_DMA_IRQHandler(&hdma_usart2_tx);
    }
    rt_interrupt_leave();
}

void USART2_IRQHandler(void)
{
    rt_interrupt_enter();
    if (uart_initialized && huart2.Instance == USART2) {
        HAL_UART_IRQHandler(&huart2);
    }
    rt_interrupt_leave();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    const uint32_t primask = enter_critical();

    if (uart_initialized && huart != NULL && huart->Instance == USART2) {
        dma_tx_state_complete(&telemetry_dma_state);
    }
    leave_critical(primask);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    const uint32_t primask = enter_critical();

    if (uart_initialized && huart != NULL && huart->Instance == USART2) {
        dma_tx_state_async_error(&telemetry_dma_state);
    }
    leave_critical(primask);
}
