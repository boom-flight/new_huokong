#ifndef _PID_H_
#define _PID_H_

#include "stm32f4xx_hal.h"
/**
 * @brief 增量式PID结构体
 * @note  适用于电机速度控制，无积分累积溢出风险
 */
typedef struct {
    // 核心参数
    float set_point;    // 目标值(SP)
    float feedback;     // 反馈值(PV)
    float error[3];     // 偏差: error[0]=e(k), error[1]=e(k-1), error[2]=e(k-2)
    
    // PID系数
    float kp;           // 比例系数
    float ki;           // 积分系数
    float kd;           // 微分系数
    
    // 输出控制
    float increment;    // 本次增量输出Δu(k)
    float output;       // 累计输出值
    float out_max;      // 输出上限
    float out_min;      // 输出下限
} IncPID;

// 外部声明

extern IncPID mpu_pid_yaw;
extern IncPID mpu_pid_pitch;
void pid_init(void);
uint8_t motor_correction_general(int32_t location);


/**
 * @brief 位置式PID计算（适用于姿态角度控制）
 * @param pid PID结构体指针
 * @param feedback 反馈值
 * @return PID输出值
 */
float PosPID_Calc(IncPID *pid, float feedback);

/**
 * @brief 增量式PID计算（适用于电机速度控制）
 * @param pid PID结构体指针
 * @param feedback 反馈值
 * @return PID输出值
 */
float IncPID_Calc(IncPID *pid, float set_point, float feedback);
/**
 * @brief PID初始化函数
 * @param pid PID结构体指针
 * @param kp 比例系数
 * @param ki 积分系数
 * @param kd 微分系数
 * @param max 输出上限
 * @param min 输出下限
 */
void PID_Init(IncPID *pid, float kp, float ki, float kd, float max, float min);

#endif /* _PID_H_ */
