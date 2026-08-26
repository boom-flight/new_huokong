/**
 * @file bmi088_stm32.h
 * @brief BMI088 传感器的 STM32 SPI 和数据就绪中断适配接口。
 * @note 该接口同时提供 SPI 总线操作和加速度计/陀螺仪 DRDY 时间锁存。
 */

#ifndef BMI088_STM32_H
#define BMI088_STM32_H

#include "bmi088/bmi088.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief BMI088 加速度计数据就绪事件位。 */
#define BMI088_DRDY_EVENT_ACCEL (1u << 0)
/** @brief BMI088 陀螺仪数据就绪事件位。 */
#define BMI088_DRDY_EVENT_GYRO  (1u << 1)

/**
 * @brief 一次 DRDY 中断的时间戳和递增序号。
 * @note timestamp_us 使用 monotonic_clock_stm32_now_us() 的微秒时间基准。
 */
typedef struct {
    /** @brief 最近一次 DRDY 边沿的单调时钟时间，单位为微秒。 */
    uint32_t timestamp_us;
    /** @brief 对应传感器 DRDY 边沿的累计序号。 */
    uint32_t sequence;
} bmi088_drdy_latch_t;

/**
 * @brief BMI088 DRDY 事件通知回调类型。
 * @param context 初始化时传入的调用上下文。
 * @param event_mask 一个或多个 BMI088_DRDY_EVENT_* 事件位。
 */
typedef void (*bmi088_drdy_notify_fn)(void *context, uint32_t event_mask);

/**
 * @brief 初始化 BMI088 的 STM32 硬件适配器。
 * @param notify DRDY 中断通知回调，不能为 NULL。
 * @param context 传递给 notify 的调用上下文。
 * @return 初始化成功或参数和上下文与当前实例一致时返回 true，否则返回 false。
 * @note 当前硬件使用 SPI1；加速度计片选为 PB13、陀螺仪片选为 PA4，DRDY 为 PB12/PB14。
 */
bool bmi088_stm32_init(bmi088_drdy_notify_fn notify, void *context);

/**
 * @brief 关闭 BMI088 硬件适配器并清理 SPI、IRQ 和 DRDY 锁存状态。
 * @note 片选引脚会被置为非选中电平，但 GPIO 配置和时钟不会被反初始化。
 */
void bmi088_stm32_deinit(void);

/**
 * @brief 获取供 BMI088 驱动使用的 STM32 总线操作表。
 * @return 已绑定 SPI 读写和毫秒延时回调的总线描述。
 */
bmi088_bus_t bmi088_stm32_bus(void);

/**
 * @brief 读取最近一次加速度计 DRDY 的锁存信息。
 * @return 加速度计 DRDY 的时间戳和序号；适配器未初始化时返回零值。
 */
bmi088_drdy_latch_t bmi088_stm32_accel_latch(void);

/**
 * @brief 读取最近一次陀螺仪 DRDY 的锁存信息。
 * @return 陀螺仪 DRDY 的时间戳和序号；适配器未初始化时返回零值。
 */
bmi088_drdy_latch_t bmi088_stm32_gyro_latch(void);

#endif
