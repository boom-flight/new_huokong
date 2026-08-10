#include <stdlib.h>
#include <math.h>
#include "gimbal_fusion.h"
#include "zengwen.h"
#include "hostpc.h"
#include "motor.h"
#include "imu.h"
#include "pid.h"

#define PITCH_ARRIVED_THRESHOLD	300				// 到达判定阈值（0.1°）
#define YAW_ARRIVED_THRESHOLD	10				// 到达判定阈值（0.1°）

AxisPosParam_t axis[MAX_MOTOR_NUM];				
volatile int32_t pitch_target_angle_abs	= 0;	// 目标绝对角度（0.01°）
volatile int32_t yaw_target_angle_abs	= 0;	// 目标绝对角度（0.01°）
// 线程资源
static uint8_t motor_correction_stack[512];
static struct rt_thread motor_correction_tcb;
//////////////////////////////////////////////////////////////////////////////////////////
void motor_correction_pitch(void)
{	
	//=========== 1.标定模式 ===========//
	axis[PITCH_NO].ctrl_mode = AXIS_MODE_CALIBRATION;

    float imu_roll_avg = 0.0f;			// 陀螺仪均值累计
    int32_t motor_pos = 0;				// 上电时陀螺仪编码器值
    const uint8_t SAMPLE_NUM = 25;   	// 采样次数
    const int32_t SPEED_SLOW = 200;   	// 慢速逼近速度（0.01dps）
    
    // ========== 阶段1：等待 IMU 数据稳定 ==========
    while (imu_jy62.data_valid == 0) {
        rt_thread_mdelay(10);
    }
	// 获取电机编码器上电初始值
	motor_pos = ANG_RAW(PITCH_NO);
    // 采样平均 IMU Roll，消除瞬时噪声
    for (uint8_t i = 0; i < SAMPLE_NUM; i++) {
        imu_roll_avg += imu_jy62.roll;
        rt_thread_mdelay(5);
    }
    imu_roll_avg /= SAMPLE_NUM;  // 单位：度
    // 计算目标位置（单位：0.01°）
    int32_t target_deg01 = (motor_pos-(int32_t)(imu_roll_avg * 1000.0f)+5)/10;
    
    // ========== 阶段2：先慢速定位到目标附近 ==========
    motor_set_multi_angle(MOTOR_PITCH_NO, target_deg01, SPEED_SLOW);
	while (abs(target_deg01 - ANG_RED10(PITCH_NO)) >= 500){
		rt_thread_mdelay(10);
	}
	// ========== 阶段3：进入 PID 自稳模式，微调锁定 ==========
    while (imu_jy62.roll > 0.05f || imu_jy62.roll < -0.05f) {    // 等待接近目标（误差 < 0.1°）
		rt_thread_mdelay(10);
		float speed = IncPID_Calc(&mpu_pid_pitch, 0, imu_jy62.roll);
		if(speed > 700)speed = 700;
		if(speed <-700)speed =-700;
		motor_set_speed(MOTOR_PITCH_NO, (int32_t)(speed * 100));
    }
    motor_set_multi_angle(MOTOR_PITCH_NO, ANG_RED10(PITCH_NO), SPEED_SLOW);
	axis[PITCH_NO].enc_zero_raw = ANG_RED10(PITCH_NO);
	axis[PITCH_NO].calib_ok_flag = 1;
	rt_thread_mdelay(200);
}
void motor_correction_yaw(void)
{	
	//=========== 1.标定模式 ===========//
	axis[YAW_NO].ctrl_mode = AXIS_MODE_CALIBRATION;

	uint16_t timeout = 0;
	int32_t location_temp = ANG_RED10(YAW_NO);
	// 找左限位
	while(motors[0].current < 70 && timeout < 2000)
	{	
		location_temp+=45;
		motor_set_multi_angle(YAW_NO, location_temp, 300);
		rt_thread_mdelay(15);
		timeout++;
	}
	axis[YAW_NO].enc_left_limit_raw = ANG_RED10(YAW_NO);
	timeout = 0;
	// 找右限位
	while(motors[0].current > -70 && timeout < 2000)
	{	
		location_temp-=45;
		motor_set_multi_angle(YAW_NO, location_temp, 300);
		rt_thread_mdelay(15);
		timeout++;
	}
	axis[YAW_NO].enc_right_limit_raw = ANG_RED10(YAW_NO);
	// 正确居中
	axis[YAW_NO].enc_zero_raw = (axis[YAW_NO].enc_left_limit_raw
								+axis[YAW_NO].enc_right_limit_raw)/2;
	motor_set_multi_angle(MOTOR_YAW_NO, axis[MOTOR_YAW_NO].enc_zero_raw, 500);
	
	rt_thread_mdelay(2000);
	axis[YAW_NO].imu_zero_raw = imu_jy62.yaw;
	axis[YAW_NO].calib_ok_flag = 1;
}
////////////////////////////////////////////////////////////////////////////////////////////
// 线程入口
static void motor_correction_task(void *parameter)
{
    uint8_t pitch_fade_cnt = 0;
    float pitch_final_target = 0.0f;
    float pitch_setpoint_now = 0.0f;

    uint8_t yaw_fade_cnt = 0;
    float yaw_final_target = 0.0f;
    float yaw_setpoint_now = 0.0f;

    while (1)
    {
        rt_thread_mdelay(10);
		
		const GimbalAtt_t *att = Gimbal_GetAtt();   // 新增：获取最新融合姿态
        // ==================== 1. Pitch 轴处理 ====================
        if (axis[PITCH_NO].calib_ok_flag == 1)
        {	
            // ---- 粗定位阶段 ----
            if (axis[PITCH_NO].ctrl_mode == AXIS_MODE_POSITION)
            {
                int32_t diff = motors[PITCH_NO].multi_angle/10 - pitch_target_angle_abs;
                if (diff < 0) diff = -diff;

                if (diff < PITCH_ARRIVED_THRESHOLD)
                {
                    pitch_final_target = ((float)hostpc_cmd.pitch_angle_val) / 100.0f;
                    pitch_setpoint_now = imu_jy62.roll;
                    mpu_pid_pitch.set_point = pitch_setpoint_now;
                    mpu_pid_pitch.error[0] = 0.0f;
                    mpu_pid_pitch.error[1] = 0.0f;
                    mpu_pid_pitch.error[2] = 0.0f;
                    mpu_pid_pitch.output = 0.0f;
                    pitch_fade_cnt = 20;
                    axis[PITCH_NO].ctrl_mode = AXIS_MODE_STABILIZE;
                }
                else
                {
                    // 未到达，本周期跳过 Pitch 的 PID
                    goto yaw_part; // 用 goto 或条件跳过
                }
            }

            // ---- 精调/自稳模式 ----
            if (axis[PITCH_NO].ctrl_mode == AXIS_MODE_STABILIZE)
            {
                if (pitch_fade_cnt > 0)
                {
                    float ratio = (float)(20 - pitch_fade_cnt) / 20.0f;
                    pitch_setpoint_now = imu_jy62.roll * (1.0f - ratio) + pitch_final_target * ratio;
                    mpu_pid_pitch.set_point = pitch_setpoint_now;
                    pitch_fade_cnt--;
                }
                else
                {
                    mpu_pid_pitch.set_point = pitch_final_target;
                }

                float speed = IncPID_Calc(&mpu_pid_pitch, mpu_pid_pitch.set_point, imu_jy62.roll);
				// 在函数外部或静态变量保存上一次的方向
				static int8_t last_dir_speed_set0 = 0; 
				int8_t dir_speed_set;
				float speed_temp = speed;
				int8_t dir_speed_temp = speed_temp == 0 ? 0 : (speed_temp > 0 ? 1 : -1);

				if(abs(motors[1].current) > 88)
				{
					// 过流时，不更新方向，强制速度为0
					speed = 0;
					// dir_speed_set 沿用上一次的旧方向（保持不变）
					dir_speed_set = last_dir_speed_set0; 
					// 清空PID...
				}else{
					// 正常时，更新方向为当前请求方向
					dir_speed_set = dir_speed_temp;
					last_dir_speed_set0 = dir_speed_set; // 保存当前方向
				}
				// 此时比较才有意义：当前请求方向(新) vs 过流前锁定的方向(旧)
				if(dir_speed_set != dir_speed_temp && dir_speed_temp != 0)
				{
					// 如果新请求的方向与过流时锁定的方向相反，恢复速度
					speed = speed_temp;
				}
                motor_set_speed(MOTOR_PITCH_NO, (int32_t)(speed * 100));
            }
        }
yaw_part:
        // ==================== 2. Yaw 轴处理 ====================
        if (axis[YAW_NO].calib_ok_flag == 1)
        {
            // ---- 粗定位阶段 ----
            if (axis[YAW_NO].ctrl_mode == AXIS_MODE_POSITION)
            {
                int32_t diff = motors[MOTOR_YAW_NO].multi_angle/10 - yaw_target_angle_abs;
                if (diff < 0) diff = -diff;

                if (diff < YAW_ARRIVED_THRESHOLD)
                {
                    yaw_final_target = ((float)hostpc_cmd.yaw_angle_val) / 100.0f; // 目标角度（度）
                    // 使用编码器当前角度作为软启动初始值（度）
                    yaw_setpoint_now = ((float)motor_angle_read(MOTOR_YAW_NO)) / 10.0f;
                    mpu_pid_yaw.set_point = yaw_setpoint_now;
                    mpu_pid_yaw.error[0] = 0.0f;
                    mpu_pid_yaw.error[1] = 0.0f;
                    mpu_pid_yaw.error[2] = 0.0f;
                    mpu_pid_yaw.output = 0.0f;
                    yaw_fade_cnt = 20;
                    axis[YAW_NO].ctrl_mode = AXIS_MODE_STABILIZE;
                }
                else
                {
                    continue; // 未到达，跳过本周期
                }
            }

            // ---- 精调/自稳模式（使用编码器反馈） ----
            if (axis[YAW_NO].ctrl_mode == AXIS_MODE_STABILIZE)
            {
                if (yaw_fade_cnt > 0)
                {
                    float ratio = (float)(20 - yaw_fade_cnt) / 20.0f;
                    float start_deg = motor_angle_read(MOTOR_YAW_NO) / 10.0f;
                    yaw_setpoint_now = start_deg * (1.0f - ratio) + yaw_final_target * ratio;
                    mpu_pid_yaw.set_point = yaw_setpoint_now;
                    yaw_fade_cnt--;
                }
                else
                {
                    mpu_pid_yaw.set_point = yaw_final_target;
                }

//				float feedback = imu_jy62.yaw - axis[YAW_NO].imu_zero_raw;
				float feedback = att->yaw_enu;
				float speed = -IncPID_Calc(&mpu_pid_yaw, mpu_pid_yaw.set_point, feedback);

				// 在函数外部或静态变量保存上一次的方向
				static int8_t last_dir_speed_set = 0; 
				int8_t dir_speed_set;
				float speed_temp = speed;
				int8_t dir_speed_temp = speed_temp == 0 ? 0 : (speed_temp > 0 ? 1 : -1);

				if(abs(motors[0].current) > 88)
				{
					// 过流时，不更新方向，强制速度为0
					speed = 0;
					// dir_speed_set 沿用上一次的旧方向（保持不变）
					dir_speed_set = last_dir_speed_set; 
					// 清空PID...
				}else{
					// 正常时，更新方向为当前请求方向
					dir_speed_set = dir_speed_temp;
					last_dir_speed_set = dir_speed_set; // 保存当前方向
				}
				// 此时比较才有意义：当前请求方向(新) vs 过流前锁定的方向(旧)
				if(dir_speed_set != dir_speed_temp && dir_speed_temp != 0)
				{
					// 如果新请求的方向与过流时锁定的方向相反，恢复速度
					speed = speed_temp;
				}
				motor_set_speed(MOTOR_YAW_NO, (int32_t)(speed*100));
				
            }
        }
    }
}
void cheack_curr(float speed_set,int16_t motor_curr)
{
	float speed_temp = speed_set;
	uint8_t dir_speed_set 	= speed_set 	> 0 ? 0 : 1;
	uint8_t dir_speed_temp 	= speed_temp> 0 ? 0 : 1;
	if(motor_curr > 70)
	{
		speed_set = 0;
	}
	if(dir_speed_set != dir_speed_temp)
	{
		speed_set = speed_temp;
	}
	motor_set_speed(MOTOR_YAW_NO, (int32_t)(speed_set*100));
}
// 初始化导出
static int motor_correction_init(void)
{
    rt_thread_init(&motor_correction_tcb,
                   "motor_corr",
                   motor_correction_task,
                   RT_NULL,
                   motor_correction_stack,
                   sizeof(motor_correction_stack),
                   13,
                   10);
    rt_thread_startup(&motor_correction_tcb);
    return RT_EOK;
}
INIT_APP_EXPORT(motor_correction_init);
