#include "bmi088_stm32.h"

#include "main.h"
#include "time/monotonic_clock_stm32.h"

#include <rtthread.h>

enum {
    BMI088_SPI_TIMEOUT_MS = 10u,
};

static SPI_HandleTypeDef hspi1;
static bmi088_drdy_notify_fn drdy_notify;
static void *drdy_notify_context;
static volatile bmi088_drdy_latch_t accel_latch;
static volatile bmi088_drdy_latch_t gyro_latch;
static bool adapter_initialized;

static void cleanup_adapter(void)
{
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    adapter_initialized = false;
    drdy_notify = NULL;
    drdy_notify_context = NULL;

    if (hspi1.Instance == SPI1) {
        (void)HAL_SPI_DeInit(&hspi1);
    }

    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12 | GPIO_PIN_14);
    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);

    accel_latch = (bmi088_drdy_latch_t){0};
    gyro_latch = (bmi088_drdy_latch_t){0};
    hspi1 = (SPI_HandleTypeDef){0};
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

static bool bus_read(void *context, bmi088_target_t target, uint8_t reg,
                     uint8_t *data, size_t length)
{
    uint8_t command = (uint8_t)(reg | 0x80u);
    uint8_t dummy = 0u;
    HAL_StatusTypeDef status;
    (void)context;

    if (!adapter_initialized || hspi1.Instance != SPI1 || data == NULL ||
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

static bool bus_write(void *context, bmi088_target_t target, uint8_t reg,
                      uint8_t value)
{
    uint8_t command[2] = {(uint8_t)(reg & 0x7Fu), value};
    HAL_StatusTypeDef status;
    (void)context;

    if (!adapter_initialized || hspi1.Instance != SPI1 ||
        (target != BMI088_ACCEL && target != BMI088_GYRO)) {
        return false;
    }

    chip_select(target, GPIO_PIN_RESET);
    status = HAL_SPI_Transmit(&hspi1, command, 2u, BMI088_SPI_TIMEOUT_MS);
    chip_select(target, GPIO_PIN_SET);
    return status == HAL_OK;
}

static void bus_delay_ms(void *context, uint32_t delay_ms)
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

bool bmi088_stm32_init(bmi088_drdy_notify_fn notify, void *context)
{
    if (notify == NULL) {
        return false;
    }
    if (adapter_initialized) {
        return notify == drdy_notify && context == drdy_notify_context;
    }

    drdy_notify = notify;
    drdy_notify_context = context;
    accel_latch = (bmi088_drdy_latch_t){0};
    gyro_latch = (bmi088_drdy_latch_t){0};

    if (!init_spi_gpio()) {
        cleanup_adapter();
        return false;
    }

    init_exti_gpio();
    adapter_initialized = true;
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5u, 0u);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    return true;
}

void bmi088_stm32_deinit(void)
{
    cleanup_adapter();
}

bmi088_bus_t bmi088_stm32_bus(void)
{
    return (bmi088_bus_t){
        .context = NULL,
        .read = bus_read,
        .write = bus_write,
        .delay_ms = bus_delay_ms,
    };
}

bmi088_drdy_latch_t bmi088_stm32_accel_latch(void)
{
    bmi088_drdy_latch_t result = {0};
    const rt_base_t level = rt_hw_interrupt_disable();

    if (adapter_initialized) {
        result.timestamp_us = accel_latch.timestamp_us;
        result.sequence = accel_latch.sequence;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

bmi088_drdy_latch_t bmi088_stm32_gyro_latch(void)
{
    bmi088_drdy_latch_t result = {0};
    const rt_base_t level = rt_hw_interrupt_disable();

    if (adapter_initialized) {
        result.timestamp_us = gyro_latch.timestamp_us;
        result.sequence = gyro_latch.sequence;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

void EXTI15_10_IRQHandler(void)
{
    rt_interrupt_enter();
    if (!adapter_initialized || drdy_notify == NULL) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12 | GPIO_PIN_14);
        rt_interrupt_leave();
        return;
    }
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_12) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12);
        accel_latch.timestamp_us = monotonic_clock_stm32_now_us();
        ++accel_latch.sequence;
        drdy_notify(drdy_notify_context, BMI088_DRDY_EVENT_ACCEL);
    }
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_14) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_14);
        gyro_latch.timestamp_us = monotonic_clock_stm32_now_us();
        ++gyro_latch.sequence;
        drdy_notify(drdy_notify_context, BMI088_DRDY_EVENT_GYRO);
    }
    rt_interrupt_leave();
}
