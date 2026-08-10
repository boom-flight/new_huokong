#ifndef __HOSTPC_H
#define __HOSTPC_H

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stm32f4xx_hal.h>
#include <string.h> // 提前引入，解决memset隐式声明
#include "dianhuo.h"
/************************ 平衡抛通信协议 - 宏定义 ************************/
#define HOSTPC_FRAME_SIZE			12		// 标准控制帧长度
#define HOSTPC_FEEDBACK_FRAME_SIZE  16      // 角度反馈帧长度
#define HOSTPC_POSTURE_FRAME_SIZE	21		// 姿态反馈帧长度（新增）
#define HOSTPC_MAX_FRAME_SIZE       HOSTPC_POSTURE_FRAME_SIZE  // 取最大值21

#define FRAME_HEAD					0xA5	// 固定帧头
#define FRAME_TYPE_CTRL				0x00	// 控制指令
#define FRAME_TYPE_FEEDBACK			0x01	// 状态反馈
#define HOSTPC_TX_BUF_CNT			8 		// 缓存8帧（节省内存，因为21字节帧更大）
#define HOSTPC_TX_BUF_LEN			(HOSTPC_MAX_FRAME_SIZE * HOSTPC_TX_BUF_CNT)

/************************ 协议指令码定义 ************************/
#define CMD_YAW_CTRL			0xA4	// Yaw单轴角度控制
#define CMD_PITCH_CTRL			0xA6	// Pitch单轴角度控制
#define CMD_MID_BACK			0x00	// 一键回中指令
#define CMD_DATA_YAW			0x06	// 相对正东航向角	
#define CMD_READ_MOTOR_STATUS	0xAA	// 上位机→下位机：读取电机状态


#define CMD_FEEDBACK_POSTURE	0x91	// 下位机→上位机
#define CMD_FEEDBACK_ANGLE		0x92	// 下位机→上位机：反馈挂架角度指令码
#define CMD_HEADING_ANGLE		0x93    // 上位机反馈帧：航向角+时间戳

/************************ 全局结构体-HOSTPC控制指令集 ************************/
typedef struct
{
    uint8_t		cmd_code;		// 当前解析到的指令码
    uint16_t	speed_val;		// 速度协议值(uint16 小端) 4-5字节
    int32_t		yaw_angle_val;	// Yaw角度协议值(int32 小端)6-9字节
    int32_t		pitch_angle_val;// Pitch角度协议值(int32 小端)6-9字节
    uint8_t		fire_id;		// 火控蛋ID(保留位3)
    uint8_t		fire_state;		// 火控状态字(四位代表四个蛋状态)
	float		cmd_yaw;		// 飞机发来的正东航向角
	float		cmd_yaw_time;	// 飞机上正东航向角对应时刻
	float		cmd_rec_time;	// 飞机发过来航向角的时刻
	float		cmd_delta_time; // 时间戳对齐增量
	uint8_t		cmd_time_flag;	// 时间对齐完成标志位
    rt_bool_t	cmd_valid;		// 指令有效标志位
} hostpc_cmd_t;

/************************ 全局变量声明 ************************/
extern hostpc_cmd_t hostpc_cmd;
extern uint8_t Weapon_Sta; // 武器状态寄存器 低四位对应1-4号炮位状态：1=有弹，0=无弹

/************************ 函数声明 ************************/
uint8_t	hostpc_calc_checksum(uint8_t *buf, uint8_t len);
void hostpc_cmd_exec_task(void *parameter);
void hostpc_tx_send_task(void *parameter);
rt_err_t hostpc_send_frame(uint8_t *frame_buf);
rt_err_t hostpc_send_fire_state(fire_state_feedback_t *fire_feedback);
rt_err_t hostpc_send_angle_feedback(int32_t pitch_angle, int32_t yaw_angle);
rt_err_t hostpc_pack_tx_frame(uint8_t *tx_buf, uint8_t frame_type, uint8_t cmd_code, uint8_t *data, uint8_t data_len);
#endif
