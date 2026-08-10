#ifndef _ZENGWEN_H
#define _ZENGWEN_H
#include <stm32f4xx_hal.h>

#define YAW_NO    	0      // Yaw 电机在数组中的索引（0）
#define PITCH_NO  	1      // Pitch 电机在数组中的索引（1）

/**
 * @brief 云台轴控制模式枚举
 * @note 仅用于单轴(如Pitch/Yaw)的控制状态切换
 */
typedef enum {
    AXIS_MODE_STABILIZE = 0,   // 自稳模式：保持当前姿态，抑制外部扰动
    AXIS_MODE_POSITION,        // 角度定位模式：运动到指定目标角度
    AXIS_MODE_CALIBRATION,     // 标定模式：寻找机械限位，计算零点
} AxisCtrlMode_t;

// 单轴电机位置、限位、标定、控制参数结构体
typedef struct{
    // 实时状态标志（中断/多任务会改写，加volatile）
    volatile uint8_t calib_ok_flag;		// 轴标定完成标志 0:未标定 1:已标定
	volatile uint8_t ctrl_switch;		// 启动自稳标志位（挂架锁定不动）
    AxisCtrlMode_t ctrl_mode;			// 控制模式 0:自稳模式 1:角度定位模式 2:标定模式

    // 标定相关参数（上电标定后固定不变）
	int32_t imu_zero_raw;				// 标定零点陀螺仪值
    int32_t enc_zero_raw;				// 标定零点原始编码器值（原error_position）
    int32_t enc_left_limit_raw;			// 左机械限位编码器原始值
    int32_t enc_right_limit_raw;		// 右机械限位编码器原始值
} AxisPosParam_t;

extern AxisPosParam_t axis[2];				

extern volatile int32_t pitch_target_angle_abs;

extern volatile int32_t yaw_target_angle_abs;

void motor_correction_yaw(void);
void motor_correction_pitch(void);

#endif
