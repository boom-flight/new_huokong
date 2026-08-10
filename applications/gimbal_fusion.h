#ifndef __GIMBAL_FUSION_H
#define __GIMBAL_FUSION_H

#include "stm32f4xx.h"
#include <math.h>
#include <stdint.h>

//==================== 滤波参数 ====================
#define Kp_YAW     		1.2f      // 提高响应速度
#define Ki_YAW     		0.01f     // 足够消除静差
#define DT         		0.01f
#define M_PI       		3.14159265358979323846f
#define INTEGRAL_LIMIT 100.0f // 防止积分饱和

//==================== 结构体声明 ====================
typedef struct {
    float psi_fused;    // 最终融合后的绝对航向（度）
    float err_integral; // 积分项
} YawFilter_t;

typedef struct {
    // ----- 输入数据（由外部更新） -----
    float alpha_yaw;      // 电机Yaw编码器角度（度）
    float alpha_pitch;    // 电机Pitch编码器角度（度）
    float alpha_roll;     // 恒为0（无Roll电机）
    float beta_pitch;     // IMU俯仰角（度）
    float beta_roll;      // IMU横滚角（度）
    float gyro_z_world;   // 大地坐标系Z轴角速度（度/秒）

    // ----- 中间计算结果 -----
    float psi_delta;      // 载荷相对机头的水平偏航角 δψ（度）

    // ----- 最终输出（ENU绝对姿态） -----
    float yaw_enu;        // 地理航向（度）
    float pitch_enu;      // 地理俯仰（= beta_pitch）
    float roll_enu;       // 地理横滚（= beta_roll）

    // ----- 滤波状态 -----
    YawFilter_t yaw_filter;
} GimbalAtt_t;

// 双缓冲结构体（用于无锁读取）
typedef struct {
    GimbalAtt_t att[2];
    volatile uint8_t active_idx;
} GimbalDoubleBuf_t;

typedef struct {
	float yaw_enu;		// 地理航向（度）
    float pitch_enu;		// 地理俯仰（= beta_pitch）
    float roll_enu;		// 地理横滚（= beta_roll）

	float time_flag;		// 时间戳
} Ballistic_Prameter_t;

// 全局变量声明
extern GimbalDoubleBuf_t g_gimbal_db;

// 对外接口
void Gimbal_Init(void);
void Gimbal_Update(float drone_yaw_enu);
const GimbalAtt_t* Gimbal_GetAtt(void);

// 工具函数
float AngleWrap180(float ang);

// 向量旋转辅助函数（内部使用）
void RotateX(float roll, float x, float y, float z, float *ox, float *oy, float *oz);
void RotateY(float pitch, float x, float y, float z, float *ox, float *oy, float *oz);
void RotateZ(float yaw, float x, float y, float z, float *ox, float *oy, float *oz);

#endif
