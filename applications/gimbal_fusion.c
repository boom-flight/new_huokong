#include "gimbal_fusion.h"
#include "hostpc.h"
#include "motor.h"
#include "imu.h"
#include "zengwen.h"
#include <rtthread.h>
#include <string.h>

// 全局姿态双缓冲实例
GimbalDoubleBuf_t 		g_gimbal_db;
Ballistic_Prameter_t 	ballistic_prames[200];
float aba;
float abb;

// 线程资源
static uint8_t gimbal_fusion_stack[512];
static struct rt_thread gimbal_fusion_tcb;

/* ======================================================================
 * 角度限幅函数
 * ====================================================================== */
float AngleWrap180(float ang) {
    while (ang > 180.0f) ang -= 360.0f;
    while (ang <= -180.0f) ang += 360.0f;
    return ang;
}

/* ======================================================================
 * 向量旋转辅助函数（与原有实现一致）
 * ====================================================================== */
void RotateX(float roll, float x, float y, float z, float *ox, float *oy, float *oz) {
    float rad = roll * M_PI / 180.0f;
    float cr = cosf(rad);
    float sr = sinf(rad);
    *ox = x;
    *oy = y * cr - z * sr;
    *oz = y * sr + z * cr;
}

void RotateY(float pitch, float x, float y, float z, float *ox, float *oy, float *oz) {
    float rad = pitch * M_PI / 180.0f;
    float cp = cosf(rad);
    float sp = sinf(rad);
    *ox = x * cp + z * sp;
    *oy = y;
    *oz = -x * sp + z * cp;
}

void RotateZ(float yaw, float x, float y, float z, float *ox, float *oy, float *oz) {
    float rad = yaw * M_PI / 180.0f;
    float cy = cosf(rad);
    float sy = sinf(rad);
    *ox = x * cy - y * sy;
    *oy = x * sy + y * cy;
    *oz = z;
}

/* ======================================================================
 * 核心解算：计算载荷相对机头的水平偏航角 δψ
 * 公式严格按照文档 "云台姿态角坐标系方案2.pdf"
 * ====================================================================== */
static float CalcDeltaPsi(const GimbalAtt_t *att) {
    float vx, vy, vz;   // 机头向量在PB系下的坐标
    float vLx, vLy, vLz; // 机头向量在PL系下的坐标

    // 步骤A：机头向量 [1,0,0]^T 从GB系转换到PB系（逆变换）
    //        顺序：R_GB_to_PB = Rx(-alpha_roll) * Ry(-alpha_pitch) * Rz(-alpha_yaw)
    RotateZ(-att->alpha_yaw, 1.0f, 0.0f, 0.0f, &vx, &vy, &vz);
    RotateY(-att->alpha_pitch, vx, vy, vz, &vx, &vy, &vz);
    RotateX(-att->alpha_roll, vx, vy, vz, &vx, &vy, &vz);

    // 步骤B：利用IMU重力向量将PB向量拉平到水平面（PL系）
    //        R_PB_to_PL = Ry(beta_pitch) * Rx(beta_roll)
    RotateX(att->beta_roll, vx, vy, vz, &vLx, &vLy, &vLz);
    RotateY(att->beta_pitch, vLx, vLy, vLz, &vLx, &vLy, &vLz);

    // 步骤C：计算水平偏航角 δψ = -atan2(vLy, vLx)
    float delta = -atan2f(vLy, vLx) * 180.0f / M_PI;
    return AngleWrap180(delta);
}

/* ======================================================================
 * Yaw轴PI互补滤波（融合绝对航向基准与陀螺积分）
 * ====================================================================== */
static void Yaw_ComplementaryFilter(YawFilter_t *filt, float psi_abs, float gyro_z, float dt)
{
    float err = AngleWrap180(psi_abs - filt->psi_fused);
    
    // 积分项累加并限幅
    filt->err_integral += err * dt;
    if (filt->err_integral > INTEGRAL_LIMIT)
        filt->err_integral = INTEGRAL_LIMIT;
    if (filt->err_integral < -INTEGRAL_LIMIT)
        filt->err_integral = -INTEGRAL_LIMIT;

    float comp = Kp_YAW * err + Ki_YAW * filt->err_integral;
    float psi_dot = gyro_z + comp;
    filt->psi_fused = AngleWrap180(filt->psi_fused + psi_dot * dt);
}

/* ======================================================================
 * 主更新函数：读取传感器数据，解算并更新姿态（10ms周期调用）
 * 参数 drone_yaw_enu：无人机机头相对地理东向的航向角（度）
 * ====================================================================== */
void Gimbal_Update(float drone_yaw_enu) {
    // 使用非活跃缓冲区进行写入
    uint8_t write_idx = (g_gimbal_db.active_idx + 1) & 1;
    GimbalAtt_t *att = &g_gimbal_db.att[write_idx];

    // 1. 读取编码器角度（单位：度），并扣除零点偏移
    //    ANG_FLOAT 返回浮点度，axis[].enc_zero_raw 单位是 0.01°
    
	#if 1  // 如果定义了倒置宏，或者直接启用
		att->alpha_yaw   = -(ANG_FLOAT(MOTOR_YAW_NO) - (float)(axis[YAW_NO].enc_zero_raw + 50) / 100.0f);
		att->alpha_pitch = -(ANG_FLOAT(MOTOR_PITCH_NO) - (float)(axis[PITCH_NO].enc_zero_raw) / 100.0f);
	#else
		att->alpha_yaw   = ANG_FLOAT(MOTOR_YAW_NO) - (float)(axis[YAW_NO].enc_zero_raw + 50) / 100.0f;
		att->alpha_pitch = ANG_FLOAT(MOTOR_PITCH_NO) - (float)(axis[PITCH_NO].enc_zero_raw) / 100.0f;
	#endif
	att->alpha_roll  = 0.0f;   // 无Roll电机
    att->alpha_yaw   = AngleWrap180(att->alpha_yaw);
    att->alpha_pitch = AngleWrap180(att->alpha_pitch);

    // 2. 从IMU获取绝对俯仰/横滚以及大地系Z轴角速度
	#if 0  // 如果定义了倒置宏，或者直接启用
		// 因为你的IMU roll对应云台pitch，倒置后云台pitch方向相反，所以取反
		att->beta_pitch = -imu_jy62.roll;   
		att->beta_roll  = -imu_jy62.pitch;  // 横滚也一并取反（如果横滚有控制的话）
	#else
		att->beta_pitch = imu_jy62.roll;
		att->beta_roll  = imu_jy62.pitch;
	#endif
    att->gyro_z_world = imu_jy62.omega_z;

    // 3. 计算 δψ
    att->psi_delta = CalcDeltaPsi(att);

    // 4. 计算绝对航向基准 = 无人机航向 + δψ
    float yaw_abs_ref = AngleWrap180(drone_yaw_enu + att->psi_delta);

    // 5. 互补滤波融合，得到平滑的绝对航向
    Yaw_ComplementaryFilter(&att->yaw_filter, yaw_abs_ref, att->gyro_z_world, DT);

    // 6. 最终ENU姿态
    att->yaw_enu   = att->yaw_filter.psi_fused;
    att->pitch_enu = att->beta_pitch;   // 直接使用IMU俯仰
    att->roll_enu  = att->beta_roll;    // 直接使用IMU横滚

    // 7. 原子切换：让这份数据变为有效
    rt_enter_critical();
    g_gimbal_db.active_idx = write_idx;
    rt_exit_critical();
}

/* ======================================================================
 * 获取当前有效姿态指针（供PID任务使用）
 * ====================================================================== */
const GimbalAtt_t* Gimbal_GetAtt(void) {
    // 直接返回当前有效索引指向的结构体
    return &g_gimbal_db.att[g_gimbal_db.active_idx];
}
/**
调用示例
  定义	const GimbalAtt_t  *att = Gimbal_GetAtt();
		att->yaw_enu		这个就是我最终的值
*/
/* ======================================================================
 * 初始化函数（在main中调用）
 * ====================================================================== */
void Gimbal_Init(void) {
    // 先读取当前传感器数据（需要临时变量）
    float alpha_yaw = ANG_FLOAT(MOTOR_YAW_NO) - (float)(axis[YAW_NO].enc_zero_raw + 50) / 100.0f;
    float alpha_pitch = ANG_FLOAT(MOTOR_PITCH_NO) - (float)(axis[PITCH_NO].enc_zero_raw) / 100.0f;
    float beta_pitch = imu_jy62.pitch;
    float beta_roll = imu_jy62.roll;

    // 构造临时 att 用于计算 delta
    GimbalAtt_t temp_att;
    temp_att.alpha_yaw = alpha_yaw;
    temp_att.alpha_pitch = alpha_pitch;
    temp_att.alpha_roll = 0.0f;
    temp_att.beta_pitch = beta_pitch;
    temp_att.beta_roll = beta_roll;
    float delta = CalcDeltaPsi(&temp_att);

    float initial_psi_abs = AngleWrap180(hostpc_cmd.cmd_yaw + delta);

    // 两个缓冲区都用这个值
    g_gimbal_db.att[0].yaw_filter.psi_fused = initial_psi_abs;
    g_gimbal_db.att[1].yaw_filter.psi_fused = initial_psi_abs;
    g_gimbal_db.att[0].yaw_filter.err_integral = 0.0f;
    g_gimbal_db.att[1].yaw_filter.err_integral = 0.0f;
    g_gimbal_db.active_idx = 0;
}
//void Gimbal_Init(void) {
//    // 清空两路缓冲区
//    memset((void*)&g_gimbal_db.att[0], 0, sizeof(GimbalAtt_t));
//    memset((void*)&g_gimbal_db.att[1], 0, sizeof(GimbalAtt_t));
//    g_gimbal_db.active_idx = 0;
//    // 初始航向设为0（避免随机值）
//    g_gimbal_db.att[0].yaw_filter.psi_fused = 0.0f;
//    g_gimbal_db.att[1].yaw_filter.psi_fused = 0.0f;
//}

/* ======================================================================
 * 10ms周期任务（线程函数）
 * ====================================================================== */
static void gimbal_fusion_task(void *parameter) {
    // 无人机航向变量由外部更新（例如在 hostpc 接收飞控数据时写入）
    while (1) {
        // 注意：drone_yaw_enu 应在中断或低优先级任务中更新，此处直接读取
//		static uint32_t last_tick = 0;
//		uint32_t now = rt_tick_get();
//		if (now - last_tick > 10) rt_kprintf("fusion delay: %d ms\n", now - last_tick);
//		last_tick = now;
        Gimbal_Update(hostpc_cmd.cmd_yaw);
        rt_thread_mdelay(2);   // 周期 10ms
    }
}

/* ======================================================================
 * 线程初始化（系统启动时自动调用）
 * ====================================================================== */
static int gimbal_fusion_init(void) {
    rt_thread_init(&gimbal_fusion_tcb,
                   "gimbal_fusion",
                   gimbal_fusion_task,
                   RT_NULL,
                   gimbal_fusion_stack,
                   sizeof(gimbal_fusion_stack),
                   14,      // 优先级，可根据需要调整
                   2);
    rt_thread_startup(&gimbal_fusion_tcb);
    return RT_EOK;
}
INIT_APP_EXPORT(gimbal_fusion_init);
