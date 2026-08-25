#include "bmi088_port.h"

#include "main.h"
#include "telemetry_dma_state.h"
#include "timestamp_extender.h"

#include <rtthread.h>

enum {
    BMI088_SPI_TIMEOUT_MS = 10u,
    BMI088_TELEMETRY_FRAME_SIZE = 32u,
};

static SPI_HandleTypeDef hspi1;
static TIM_HandleTypeDef htim2;
static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef hdma_usart2_tx;

static struct rt_event *imu_event;
static volatile bmi088_drdy_latch_t accel_latch;
static volatile bmi088_drdy_latch_t gyro_latch;
static volatile uint16_t timestamp_high_word;
static telemetry_dma_state_t telemetry_dma_state;
static bool port_initialized;

static void cleanup_port(void)
{
    port_initialized = false;

    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    HAL_NVIC_DisableIRQ(TIM2_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel7_IRQn);
    HAL_NVIC_DisableIRQ(USART2_IRQn);

    if (htim2.Instance == TIM2) {
        (void)HAL_TIM_Base_Stop_IT(&htim2);
        __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
        (void)HAL_TIM_Base_DeInit(&htim2);
    }
    if (huart2.Instance == USART2) {
        (void)HAL_UART_DeInit(&huart2);
    }
    if (hdma_usart2_tx.Instance == DMA1_Channel7) {
        (void)HAL_DMA_DeInit(&hdma_usart2_tx);
    }
    if (hspi1.Instance == SPI1) {
        (void)HAL_SPI_DeInit(&hspi1);
    }

    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12 | GPIO_PIN_14);
    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel7_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);

    accel_latch = (bmi088_drdy_latch_t){0};
    gyro_latch = (bmi088_drdy_latch_t){0};
    timestamp_high_word = 0u;
    telemetry_dma_state_reset(&telemetry_dma_state);
    imu_event = NULL;
    hspi1 = (SPI_HandleTypeDef){0};
    htim2 = (TIM_HandleTypeDef){0};
    huart2 = (UART_HandleTypeDef){0};
    hdma_usart2_tx = (DMA_HandleTypeDef){0};
}

static GPIO_TypeDef *target_cs_port(bmi088_target_t target)
{
    return target == BMI088_ACCEL ? GPIOB : GPIOA;
}

static uint16_t target_cs_pin(bmi088_target_t target)
{
    return target == BMI088_ACCEL ? GPIO_PIN_13 : GPIO_PIN_4;
}

static void chip_select(bmi088_target_t target, GPIO_PinState state)
{
    HAL_GPIO_WritePin(target_cs_port(target), target_cs_pin(target), state);
}

static bool port_read(void *context, bmi088_target_t target, uint8_t reg,
                      uint8_t *data, size_t length)
{
    uint8_t command = (uint8_t)(reg | 0x80u);
    uint8_t dummy = 0u;
    HAL_StatusTypeDef status;
    (void)context;

    if (!port_initialized || hspi1.Instance != SPI1 || data == NULL ||
        length == 0u || length > UINT16_MAX ||
        (target != BMI088_ACCEL && target != BMI088_GYRO)) {
        return false;
    }

    chip_select(target, GPIO_PIN_RESET);
    status = HAL_SPI_Transmit(&hspi1, &command, 1u, BMI088_SPI_TIMEOUT_MS);
    if (status == HAL_OK && target == BMI088_ACCEL) {
        status = HAL_SPI_Receive(&hspi1, &dummy, 1u,
                                 BMI088_SPI_TIMEOUT_MS);
    }
    if (status == HAL_OK) {
        status = HAL_SPI_Receive(&hspi1, data, (uint16_t)length,
                                 BMI088_SPI_TIMEOUT_MS);
    }
    chip_select(target, GPIO_PIN_SET);
    return status == HAL_OK;
}

static bool port_write(void *context, bmi088_target_t target, uint8_t reg,
                       uint8_t value)
{
    uint8_t command[2] = {(uint8_t)(reg & 0x7Fu), value};
    HAL_StatusTypeDef status;
    (void)context;

    if (!port_initialized || hspi1.Instance != SPI1 ||
        (target != BMI088_ACCEL && target != BMI088_GYRO)) {
        return false;
    }
    chip_select(target, GPIO_PIN_RESET);
    status = HAL_SPI_Transmit(&hspi1, command, 2u, BMI088_SPI_TIMEOUT_MS);
    chip_select(target, GPIO_PIN_SET);
    return status == HAL_OK;
}

static void port_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    rt_thread_mdelay((rt_int32_t)delay_ms);
}

static bool init_spi_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);

    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_SPI1_CLK_ENABLE();
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
    hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7u;
    return HAL_SPI_Init(&hspi1) == HAL_OK;
}

static void init_exti_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_12 | GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12 | GPIO_PIN_14);
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

static bool init_telemetry_uart(void)
{
    GPIO_InitTypeDef gpio = {0};

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
        return false;
    }

    return true;
}

static void enable_port_irqs(void)
{
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5u, 0u);
    HAL_NVIC_SetPriority(TIM2_IRQn, 5u, 0u);
    HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 6u, 0u);
    HAL_NVIC_SetPriority(USART2_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

bool bmi088_port_init(struct rt_event *event)
{
    bool success;

    if (event == NULL) {
        return false;
    }
    if (port_initialized) {
        return event == imu_event;
    }

    imu_event = event;
    accel_latch = (bmi088_drdy_latch_t){0};
    gyro_latch = (bmi088_drdy_latch_t){0};
    timestamp_high_word = 0u;
    telemetry_dma_state_reset(&telemetry_dma_state);
    success = init_spi_gpio() && init_timestamp_timer() &&
              init_telemetry_uart();
    if (success) {
        init_exti_gpio();
        success = HAL_TIM_Base_Start_IT(&htim2) == HAL_OK;
    }
    if (!success) {
        cleanup_port();
        return false;
    }

    port_initialized = true;
    enable_port_irqs();
    return true;
}

void bmi088_port_deinit(void)
{
    cleanup_port();
}

bmi088_bus_t bmi088_port_bus(void)
{
    return (bmi088_bus_t){
        .context = NULL,
        .read = port_read,
        .write = port_write,
        .delay_ms = port_delay_ms,
    };
}

bmi088_drdy_latch_t bmi088_port_accel_latch(void)
{
    bmi088_drdy_latch_t result = {0};
    const rt_base_t level = rt_hw_interrupt_disable();
    if (port_initialized) {
        result.timestamp_us = accel_latch.timestamp_us;
        result.sequence = accel_latch.sequence;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

bmi088_drdy_latch_t bmi088_port_gyro_latch(void)
{
    bmi088_drdy_latch_t result = {0};
    const rt_base_t level = rt_hw_interrupt_disable();
    if (port_initialized) {
        result.timestamp_us = gyro_latch.timestamp_us;
        result.sequence = gyro_latch.sequence;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

uint32_t bmi088_port_timestamp_us(void)
{
    uint16_t high_word;
    uint16_t counter;
    bool update_pending;
    const rt_base_t level = rt_hw_interrupt_disable();
    if (!port_initialized || htim2.Instance != TIM2) {
        rt_hw_interrupt_enable(level);
        return 0u;
    }
    high_word = timestamp_high_word;
    counter = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
    update_pending = __HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_UPDATE) != RESET;
    rt_hw_interrupt_enable(level);
    return timestamp_extender_compose(high_word, counter, update_pending);
}

bool bmi088_port_telemetry_try_start(const uint8_t *frame, size_t length)
{
    rt_base_t level;

    if (frame == NULL || length != BMI088_TELEMETRY_FRAME_SIZE) {
        return false;
    }
    level = rt_hw_interrupt_disable();
    if (!port_initialized || huart2.Instance != USART2 ||
        hdma_usart2_tx.Instance != DMA1_Channel7 ||
        !telemetry_dma_state_reserve(&telemetry_dma_state)) {
        rt_hw_interrupt_enable(level);
        return false;
    }
    rt_hw_interrupt_enable(level);

    if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)frame,
                              (uint16_t)length) != HAL_OK) {
        level = rt_hw_interrupt_disable();
        telemetry_dma_state_cancel(&telemetry_dma_state);
        rt_hw_interrupt_enable(level);
        return false;
    }
    return true;
}

bool bmi088_port_telemetry_busy(void)
{
    bool busy;
    const rt_base_t level = rt_hw_interrupt_disable();
    busy = port_initialized && telemetry_dma_state_busy(&telemetry_dma_state);
    rt_hw_interrupt_enable(level);
    return busy;
}

bool bmi088_port_telemetry_take_failure(void)
{
    bool failed;
    const rt_base_t level = rt_hw_interrupt_disable();

    failed = port_initialized &&
             telemetry_dma_state_take_failure(&telemetry_dma_state);
    rt_hw_interrupt_enable(level);
    return failed;
}

void EXTI15_10_IRQHandler(void)
{
    rt_interrupt_enter();
    if (!port_initialized || imu_event == NULL) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12 | GPIO_PIN_14);
        rt_interrupt_leave();
        return;
    }
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_12) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12);
        accel_latch.timestamp_us = bmi088_port_timestamp_us();
        ++accel_latch.sequence;
        (void)rt_event_send(imu_event, BMI088_EVENT_ACCEL);
    }
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_14) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_14);
        gyro_latch.timestamp_us = bmi088_port_timestamp_us();
        ++gyro_latch.sequence;
        (void)rt_event_send(imu_event, BMI088_EVENT_GYRO);
    }
    rt_interrupt_leave();
}

void TIM2_IRQHandler(void)
{
    rt_interrupt_enter();
    if (port_initialized && htim2.Instance == TIM2) {
        HAL_TIM_IRQHandler(&htim2);
    }
    rt_interrupt_leave();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (port_initialized && htim != NULL && htim->Instance == TIM2) {
        ++timestamp_high_word;
    }
}

void DMA1_Channel7_IRQHandler(void)
{
    rt_interrupt_enter();
    if (port_initialized && hdma_usart2_tx.Instance == DMA1_Channel7) {
        HAL_DMA_IRQHandler(&hdma_usart2_tx);
    }
    rt_interrupt_leave();
}

void USART2_IRQHandler(void)
{
    rt_interrupt_enter();
    if (port_initialized && huart2.Instance == USART2) {
        HAL_UART_IRQHandler(&huart2);
    }
    rt_interrupt_leave();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    const rt_base_t level = rt_hw_interrupt_disable();

    if (port_initialized && huart != NULL && huart->Instance == USART2) {
        telemetry_dma_state_complete(&telemetry_dma_state);
    }
    rt_hw_interrupt_enable(level);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    const rt_base_t level = rt_hw_interrupt_disable();

    if (port_initialized && huart != NULL && huart->Instance == USART2) {
        telemetry_dma_state_async_error(&telemetry_dma_state);
    }
    rt_hw_interrupt_enable(level);
}
