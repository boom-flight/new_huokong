#include <board.h>
#include <drv_gpio.h>
#include <rtthread.h>

#include "imu/imu_service.h"
#include "telemetry/telemetry_service.h"

#define STATE_LED_PIN GET_PIN(B, 6)

/**
 * @file main.c
 * @brief RT-Thread 固件应用入口，负责配置板级状态并启动遥测与 IMU 服务。
 */

/**
 * @brief 按固定顺序完成应用启动。
 *
 * 启动顺序为配置状态 LED、输出启动日志、初始化 telemetry 服务，最后初始化
 * IMU 服务。telemetry 必须先启动，以便 IMU 服务运行后立即具备遥测通道。
 *
 * @return RT_EOK 所有启动步骤成功。
 * @return -RT_ERROR telemetry 或 IMU 初始化失败。
 * @note telemetry 初始化失败时直接返回错误。IMU 初始化失败时释放已启动的
 *       telemetry 服务；若释放失败，仅记录日志，但启动仍按失败处理并返回错误。
 */
int main(void)
{
    rt_pin_mode(STATE_LED_PIN, PIN_MODE_OUTPUT);
    rt_kprintf("STM32F103 BMI088 telemetry startup\n");
    if (!telemetry_service_init()) {
        rt_kprintf("telemetry service init failed\n");
        return -RT_ERROR;
    }
    if (!imu_service_init()) {
        const bool telemetry_cleanup_ok = telemetry_service_deinit();

        if (!telemetry_cleanup_ok) {
            rt_kprintf("telemetry cleanup failed\n");
        }
        rt_kprintf("imu service init failed\n");
        return -RT_ERROR;
    }
    return RT_EOK;
}
