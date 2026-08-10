#include "pid.h"
#include "motor.h"
#include "imu.h"
#include "math.h"

IncPID mpu_pid_yaw;
IncPID mpu_pid_pitch;

/**
 * @brief 位置式PID计算
 */
float PosPID_Calc(IncPID *pid, float feedback) 
{
    if (pid == NULL) return 0.0f;
    pid->feedback = feedback;
    pid->error[1] = pid->error[0];
    pid->error[0] = pid->set_point - pid->feedback;
    if (pid->ki > 0.0001f) {
        pid->error[2] += pid->error[0];
        float integral_limit = (pid->out_max - pid->out_min) / pid->ki;
        if (pid->error[2] > integral_limit) pid->error[2] = integral_limit;
        if (pid->error[2] < -integral_limit) pid->error[2] = -integral_limit;
    } else {
        pid->error[2] = 0.0f;
    }
    pid->output = pid->kp * pid->error[0]
                + pid->ki * pid->error[2]
                + pid->kd * (pid->error[0] - pid->error[1]);
    pid->output = (pid->output > pid->out_max) ? pid->out_max : pid->output;
    pid->output = (pid->output < pid->out_min) ? pid->out_min : pid->output;
    return pid->output;
}

/**
 * @brief 增量式PID计算
 */
float IncPID_Calc(IncPID *pid, float set_point, float feedback) 
{
    if (pid == NULL) return 0.0f;
    pid->feedback = feedback;
	pid->set_point = set_point;
    pid->error[0] = pid->set_point - pid->feedback;
    pid->increment = pid->kp * (pid->error[0] - pid->error[1])
                   + pid->ki * pid->error[0]
                   + pid->kd * (pid->error[0] - 2 * pid->error[1] + pid->error[2]);
    pid->output += pid->increment;
    pid->output = (pid->output > pid->out_max) ? pid->out_max : pid->output;
    pid->output = (pid->output < pid->out_min) ? pid->out_min : pid->output;
    pid->error[2] = pid->error[1];
    pid->error[1] = pid->error[0];
    return pid->output;
}

/**
 * @brief PID初始化
 */
void PID_Init(IncPID *pid, float kp, float ki, float kd, float max, float min)
{
    if (pid == NULL) return;
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_max = max;
    pid->out_min = min;
    pid->set_point = 0.0f;
    pid->feedback = 0.0f;
    pid->error[0] = pid->error[1] = pid->error[2] = 0.0f;
    pid->increment = 0.0f;
    pid->output = 0.0f;
}
