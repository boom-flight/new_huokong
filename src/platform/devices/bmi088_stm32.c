/**
 * @file bmi088_stm32.c
 * @brief BMI088 的 STM32F103C8 SPI 总线和 DRDY 中断适配实现。
 * @note SPI1 使用 PA5/PA6/PA7，传感器片选使用 PA4/PB13，DRDY 使用 PB12/PB14。
 */

#include "bmi088_stm32.h"

#include "main.h"
#include "time/monotonic_clock_stm32.h"

#include <rtthread.h>

enum {
    /** @brief 单次 SPI 事务的 HAL 超时时间，单位为毫秒。 */
    BMI088_SPI_TIMEOUT_MS = 10u,
};

/** @brief BMI088 SPI 句柄、DRDY 通知目标和硬件锁存状态。 */
static SPI_HandleTypeDef hspi1;
static bmi088_drdy_notify_fn drdy_notify;
static void *drdy_notify_context;
static volatile bmi088_drdy_latch_t accel_latch;
static volatile bmi088_drdy_latch_t gyro_latch;
static bool adapter_initialized;

/**
 * @brief 回滚适配器初始化并复位外部中断、片选和 DRDY 状态。
 * @note 该函数用于初始化失败和显式反初始化路径。
 */
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

/**
 * @brief 返回指定 BMI088 目标的片选 GPIO 端口。
 * @param target BMI088 加速度计或陀螺仪目标。
 * @return 目标对应的片选 GPIO 端口。
 */
static GPIO_TypeDef *target_cs_port(bmi088_target_t target)
{
    return target == BMI088_ACCEL ? GPIOB : GPIOA;
}

/**
 * @brief 返回指定 BMI088 目标的片选 GPIO 引脚。
 * @param target BMI088 加速度计或陀螺仪目标。
 * @return 目标对应的片选 GPIO 引脚。
 */
static uint16_t target_cs_pin(bmi088_target_t target)
{
    return target == BMI088_ACCEL ? GPIO_PIN_13 : GPIO_PIN_4;
}

/**
 * @brief 设置指定 BMI088 芯片的片选电平。
 * @param target BMI088 加速度计或陀螺仪目标。
 * @param state 片选 GPIO 电平。
 */
static void chip_select(bmi088_target_t target, GPIO_PinState state)
{
    HAL_GPIO_WritePin(target_cs_port(target), target_cs_pin(target), state);
}

/**
 * @brief 在中断保护下读取一个 DRDY 锁存值。
 * @param latch 待读取的易变锁存值。
 * @return 锁存值；适配器未初始化时返回零值。
 * @note 读取期间暂时屏蔽硬件中断，避免时间戳和序号被拆分读取。
 */
static bmi088_drdy_latch_t read_latch(
    volatile const bmi088_drdy_latch_t *latch)
{
    bmi088_drdy_latch_t result = {0};
    const rt_base_t level = rt_hw_interrupt_disable();

    if (adapter_initialized) {
        result.timestamp_us = latch->timestamp_us;
        result.sequence = latch->sequence;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

/**
 * @brief 通过 BMI088 SPI 总线读取寄存器。
 * @param context 总线上下文，当前实现未使用。
 * @param target 目标传感器。
 * @param reg 起始寄存器地址。
 * @param data 接收缓冲区。
 * @param length 要读取的字节数。
 * @return SPI 事务成功返回 true，否则返回 false。
 * @note 加速度计读取遵循其额外 dummy 字节规则，陀螺仪读取不插入该字节。
 */
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

/**
 * @brief 通过 BMI088 SPI 总线写入一个寄存器。
 * @param context 总线上下文，当前实现未使用。
 * @param target 目标传感器。
 * @param reg 目标寄存器地址。
 * @param value 要写入的寄存器值。
 * @return SPI 事务成功返回 true，否则返回 false。
 */
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

/**
 * @brief 为 BMI088 驱动提供毫秒级线程延时。
 * @param context 总线上下文，当前实现未使用。
 * @param delay_ms 延时时间，单位为毫秒。
 */
static void bus_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    rt_thread_mdelay((rt_int32_t)delay_ms);
}

/**
 * @brief 初始化 BMI088 SPI1 外设和片选 GPIO。
 * @return SPI HAL 初始化成功返回 true，否则返回 false。
 */
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

/**
 * @brief 初始化两个 BMI088 DRDY 输入及其外部中断线。
 * @note PB12 对应加速度计，PB14 对应陀螺仪，均配置为上拉下降沿触发。
 */
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

/**
 * @brief 初始化 BMI088 硬件适配器和 DRDY 中断。
 * @param notify DRDY 事件通知回调，不能为 NULL。
 * @param context 传递给 notify 的调用上下文。
 * @return 初始化成功返回 true，否则返回 false。
 * @note 重复初始化仅在回调和上下文与当前实例一致时成功。
 */
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

/**
 * @brief 关闭 BMI088 硬件适配器。
 */
void bmi088_stm32_deinit(void)
{
    cleanup_adapter();
}

/**
 * @brief 构造 BMI088 STM32 总线操作表。
 * @return 当前适配器的 SPI 读写和延时回调集合。
 */
bmi088_bus_t bmi088_stm32_bus(void)
{
    return (bmi088_bus_t){
        .context = NULL,
        .read = bus_read,
        .write = bus_write,
        .delay_ms = bus_delay_ms,
    };
}

/**
 * @brief 获取加速度计 DRDY 锁存值。
 * @return 加速度计最近一次 DRDY 的时间戳和序号。
 */
bmi088_drdy_latch_t bmi088_stm32_accel_latch(void)
{
    return read_latch(&accel_latch);
}

/**
 * @brief 获取陀螺仪 DRDY 锁存值。
 * @return 陀螺仪最近一次 DRDY 的时间戳和序号。
 */
bmi088_drdy_latch_t bmi088_stm32_gyro_latch(void)
{
    return read_latch(&gyro_latch);
}

/**
 * @brief 处理 BMI088 的 PB12/PB14 外部中断。
 * @note 在 RT-Thread 中断上下文中锁存当前微秒时间、递增序号并通知上层。
 */
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
