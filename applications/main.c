/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-06     SummerGift   first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <board.h>
#include <math.h>
#include "gimbal_fusion.h"
#include "zengwen.h"
#include "hostpc.h"
#include "motor.h"
#include "imu.h"
#include "pid.h"

#define LED0_PIN    GET_PIN(A, 8)
#define LED1_PIN    GET_PIN(C, 9)
#define LED2_PIN    GET_PIN(C, 8)
#define LED3_PIN    GET_PIN(D, 15)

int main(void)
{
    int count = 1;
    /* set LED0 pin mode to output */
    rt_pin_mode(LED0_PIN, PIN_MODE_OUTPUT);
	rt_pin_mode(LED1_PIN, PIN_MODE_OUTPUT);
	rt_pin_mode(LED2_PIN, PIN_MODE_OUTPUT);
	rt_pin_mode(LED3_PIN, PIN_MODE_OUTPUT);
	// 参数可根据实际调试
	PID_Init(&mpu_pid_pitch, 200.0f, 2.0f, 5.0f, 4000.0f, -4000.0f);
	PID_Init(&mpu_pid_yaw, 100.0f, 2.0f, 5.0f, 4000.0f, -4000.0f); 
	rt_thread_mdelay(2000);		// 等待电机启动完成

	motor_correction_pitch();	// Pitch轴标定
	motor_correction_yaw();		// Yaw轴标定
	
	for(uint8_t i = 7; i > 0; i--){
		rt_pin_write(LED3_PIN, PIN_LOW); rt_thread_mdelay(100);
		rt_pin_write(LED3_PIN, PIN_HIGH);rt_thread_mdelay(100);
	}
	rt_thread_mdelay(500);
	axis[YAW_NO].ctrl_mode = AXIS_MODE_STABILIZE;
	axis[PITCH_NO].ctrl_mode = AXIS_MODE_STABILIZE;
	Gimbal_Init();

    while (count++)
    {
		rt_pin_write(LED3_PIN, PIN_LOW);
		rt_thread_mdelay(250);
		rt_pin_write(LED3_PIN, PIN_HIGH);
        rt_thread_mdelay(250);
    }

    return RT_EOK;
}
