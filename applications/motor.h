#ifndef __MOTOR_H
#define __MOTOR_H

#include <board.h>
#include <rtdevice.h>
#include <rtthread.h>

// ===================== 极简配置宏 =====================
#define CAN_RX_BUF_SIZE 		8
#define READ_ANGLE_PERIOD_MS 	11 // 角度查询周期 10ms
#define MOTOR_QUERY_DELAY_MS 	5  // 双电机查询间隔
#define TASK_DELAY_5MS 			5	// CAN发送任务调度间隔
#define TASK_DELAY_2MS 			2	// CAN发送任务调度间隔

// ===================== 电机控制命令字 =====================
#define MOTOR_CMD_DISABLE 	0x80
#define MOTOR_CMD_STOP 		0x81
#define MOTOR_CMD_ENABLE 	0x88

/************************ 电机基本配置 ************************/
#define MAX_MOTOR_NUM   	2      		// 本系统管理的电机数量（Yaw + Pitch）
#define MAX_SPEED_LIMIT 	10000    		// 速度指令最大值（单位：0.01 dps，即 5.00 dps）

/// 以下没有用到
#define ANGLE_SCALE     	100    		// 角度单位换算因子（内部存储为 0.01°，显示/输入为 1°，乘以100转换）
#define ENC_TO_ANGLE_SCALE 	65535.0f  	// 编码器单圈最大值（用于归一化，保留浮点）
#define ENC_ANGLE_RANGE 	3600.0f		// 单圈对应角度范围（360.00° = 3600 * 0.01°）
/// 以上没有用到

/************************ 电机索引与 CAN ID ************************/
#define MOTOR_YAW_NO    	0      // Yaw 电机在数组中的索引（0）
#define MOTOR_PITCH_NO  	1      // Pitch 电机在数组中的索引（1）
#define MOTOR_YAW_CAN_ID   	0x141  // Yaw 电机的 CAN 标识符
#define MOTOR_PITCH_CAN_ID 	0x142  // Pitch 电机的 CAN 标识符

/************************ 机械零位偏移（绝对→相对转换基准） ************************/
// 第一组偏移（默认模式，通常用于初始校准或特定工况）
#define MOTOR_CENTER_YAW_ANGLE    0    // Yaw 轴机械零位偏移（单位：0.01°），当前为 0°
#define MOTOR_CENTER_PITCH_ANGLE  0    // Pitch 轴机械零位偏移（单位：0.01°），当前为 0°

// 第二组偏移（模式2，用于不同安装或补偿，Yaw=150.00°，Pitch=300.00°）
#define MOTOR_CENTER_YAW_ANGLE_1    15000   // 对应 150.00°
#define MOTOR_CENTER_PITCH_ANGLE_1  30000   // 对应 300.00°

/************************ 绝对角度 ↔ 相对角度转换宏（模式0） ************************/
// 功能：将电机反馈的绝对多圈角度（0.01°单位）转换为相对机械角度（0.1°单位？）
// 注意：此处公式为 (abs - center) ，返回单位为 0.1°（因为 ANGLE_SCALE=100，但实际使用中可能直接用作0.01°）
// 实际使用时，motor_angle_read 返回值除以10才得到度，所以该宏返回的是 0.1° 单位（精度0.1°）
#define MOTOR_REL_FROM_ABS(abs_angle)	(abs_angle / 10)

// 功能：将目标相对角度（0.1°单位）转换为绝对角度（0.01°单位）
// 输入 rel_angle 单位为 0.1°（因为乘以10），转换后为0.01°单位
#define MOTOR_ABS_FROM_REL(rel_angle)	(rel_angle * 10)

#define ANG_RAW(motor_no)	(motors[motor_no].multi_angle)					// 0.001°单位原始数据
#define ANG_RED10(motor_no)	((motors[motor_no].multi_angle+5)/10)				// 0.01°单位原始数据
#define ANG_ROUND(motor_no)	((motors[motor_no].multi_angle + 500)/1000)		// 1°单位四舍五入整形数据
#define ANG_FLOAT(motor_no)	((float)(motors[motor_no].multi_angle)*0.001f)	// 1°单位浮点两位小数数据

/************************ 电机参数结构体【精简版】 ************************/
typedef struct
{
    uint32_t		can_id;				// 电机 ID
    int32_t 		multi_angle; 		// 电机多圈角度 (单位：0.001°)
	int8_t  		temp;				// 电机温度
	int16_t 		current;			// 电机电流
	int16_t 		speed;				// 电机速度
	uint16_t		encoder;			// 电机位置
} mg4005_motor_t;
// 电机控制模式枚举
typedef enum {
    MOTOR_CTRL_IDLE = 0,	// 空闲模式：无任何下发指令，不主动发送控制报文
    MOTOR_CTRL_ANGLE,		// 多圈绝对角度闭环控制，下发0xA4指令，带目标角度+最大限速
    MOTOR_CTRL_SPEED,		// 转速闭环控制，下发0xA2指令，设置目标匀速
    MOTOR_CTRL_READ_STATE,	// 请求读取电机多圈编码器角度，下发0x92查询指令，收到反馈后切回IDLE
} motor_ctrl_mode_e;
// 全局电机实时控制结构体
typedef struct
{
    motor_ctrl_mode_e	ctrl_mode;		// 电机当前控制模式，决定本次CAN报文发送类型（角度/速度/使能/读角度等）
    int32_t				target_angle;	// 角度闭环目标值，单位0.01°，MOTOR_CTRL_ANGLE模式专用
    int32_t				target_speed;	// 速度闭环目标转速，单位0.01dps，MOTOR_CTRL_SPEED模式专用
    uint16_t			max_speed;		// 角度模式下最大运动限速，限制电机转动快慢
} motor_real_ctrl_t;

/************************ 全局变量声明 ************************/
extern uint8_t can_init_flag;
extern mg4005_motor_t motors[MAX_MOTOR_NUM];
/************************ 函数声明 ************************/
rt_err_t motor_set_multi_angle(uint8_t motor_no, int32_t target_angle, uint16_t max_speed);
rt_err_t motor_set_speed(uint8_t motor_no, int32_t speed_dps01);
rt_err_t motor_state_read(uint8_t motor_no);
int32_t  motor_angle_read(uint8_t motor_no);

int can_init(void);

#endif
