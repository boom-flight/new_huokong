#include <board.h>
#include <drv_gpio.h>
#include <rtthread.h>

#include "imu_service.h"
#include "telemetry_service.h"

#define STATE_LED_PIN GET_PIN(B, 6)

int main(void)
{
    rt_pin_mode(STATE_LED_PIN, PIN_MODE_OUTPUT);
    rt_kprintf("STM32F103 BMI088 telemetry startup\n");
    if (!imu_service_init()) {
        rt_kprintf("imu service init failed\n");
        return -RT_ERROR;
    }
    if (!telemetry_service_init()) {
        rt_kprintf("telemetry service init failed\n");
        return -RT_ERROR;
    }
    return RT_EOK;
}
