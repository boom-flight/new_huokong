#ifndef __IMU_H
#define __IMU_H

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
//================= 宏定义区 ================//
#define IMU_RX_BUF_SIZE 33

//================= 枚举体区 ================//


//================= 结构体区 ================//
typedef struct {
    volatile uint8_t data_valid; // 数据有效标志

    volatile float acc_x; // X轴加速度 (g)
    volatile float acc_y; // Y轴加速度 (g)
    volatile float acc_z; // Z轴加速度 (g)

    volatile float temp; // 温度 (℃)

    volatile float omega_x; // X轴角速度 (°/s)
    volatile float omega_y; // Y轴角速度 (°/s)
    volatile float omega_z; // Z轴角速度 (°/s)

    volatile float roll;  // 横滚角 (°)
    volatile float pitch; // 俯仰角 (°)
    volatile float yaw;   // 航向角 (°)
} imu_jy62_ts;

//================= 外部变量区 ================//
extern imu_jy62_ts imu_jy62;

//================= 外部函数区 ================//
int imu_init(void);

#endif
