/**
 * @file imu_policy.h
 * @brief IMU 采样时序、姿态更新和状态策略接口。
 */

#ifndef IMU_POLICY_H
#define IMU_POLICY_H

#include "attitude/imu_calibration.h"
#include "attitude/mahony.h"
#include "bmi088/bmi088.h"
#include "imu/imu_snapshot.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief IMU 服务的生命周期状态。 */
typedef enum {
    /** @brief 正在初始化传感器。 */
    IMU_INITIALIZING,
    /** @brief 正在采集校准数据。 */
    IMU_CALIBRATING,
    /** @brief 已完成校准并正常运行。 */
    IMU_RUNNING,
    /** @brief 发生故障，等待重新初始化。 */
    IMU_FAULT_RETRY
} imu_state_t;

/** @brief 根据陀螺仪采样间隔选择姿态积分策略。 */
typedef enum {
    /** @brief 时间间隔在允许范围内，执行积分。 */
    IMU_DT_INTEGRATE,
    /** @brief 跳过本次积分，但保持估计器有效性。 */
    IMU_DT_SKIP_KEEP_VALID,
    /** @brief 时间间隔过长，跳过积分并使估计器失效。 */
    IMU_DT_SKIP_INVALIDATE
} imu_dt_action_t;

/** @brief 运行态陀螺仪处理的结果。 */
typedef struct {
    /** @brief 本次时间间隔是否被拒绝。 */
    bool rejected_dt;
    /** @brief 本次时间间隔是否构成长时间间隔。 */
    bool long_gap;
    /** @brief 姿态估计器是否完成更新。 */
    bool update_succeeded;
} imu_running_gyro_result_t;

/** @brief 校准样本是否允许进入校准器的判定结果。 */
typedef enum {
    /** @brief 时序和样本关联条件满足，允许交由校准器继续校验。 */
    IMU_CALIBRATION_ADMISSION_ACCEPT,
    /** @brief 等待未消费的加速度样本。 */
    IMU_CALIBRATION_ADMISSION_SKIP_PENDING_ACCEL,
    /** @brief 当前条件无效，重置校准器。 */
    IMU_CALIBRATION_ADMISSION_RESET,
} imu_calibration_admission_t;

/** @brief 状态指示灯一个阶段的持续时间和下一阶段。 */
typedef struct {
    /** @brief 当前阶段持续时间，单位为毫秒。 */
    uint16_t duration_ms;
    /** @brief 当前阶段结束后进入的阶段编号。 */
    uint8_t next_phase;
    /** @brief 当前阶段是否点亮指示灯。 */
    bool on;
} imu_led_step_t;

/** @brief 结合数据锁存器变化和总线读取结果得到的读取分类。 */
typedef enum {
    /** @brief 读取成功且读取期间无新样本。 */
    IMU_SAMPLE_READ_CONSISTENT_SUCCESS,
    /** @brief 读取成功但读取期间产生了新样本。 */
    IMU_SAMPLE_READ_CHANGED_LATCH,
    /** @brief 读取失败且读取期间无新样本。 */
    IMU_SAMPLE_READ_CONSISTENT_FAILURE,
    /** @brief 读取失败且读取期间产生了新样本。 */
    IMU_SAMPLE_READ_CHANGED_LATCH_FAILURE
} imu_sample_read_result_t;

/**
 * @brief 判断采样前后的数据锁存序列号是否一致。
 *
 * @param before_sequence 读取前的序列号。
 * @param after_sequence 读取后的序列号。
 * @return 序列号一致时为 true。
 */
static inline bool imu_latch_sequence_consistent(uint32_t before_sequence,
                                                 uint32_t after_sequence)
{
    return (uint32_t)(after_sequence - before_sequence) == 0u;
}

/**
 * @brief 根据锁存器是否变化和读取结果分类一次采样读取。
 *
 * @param before_sequence 读取前的序列号。
 * @param after_sequence 读取后的序列号。
 * @param read_succeeded 底层传感器读取是否成功。
 * @return 采样读取分类结果。
 */
imu_sample_read_result_t imu_classify_sample_read(uint32_t before_sequence,
                                                  uint32_t after_sequence,
                                                  bool read_succeeded);

/**
 * @brief 根据陀螺仪时间间隔选择姿态更新动作。
 *
 * @param delta_us 当前与上一基线之间的时间间隔，单位为微秒。
 * @return 对应的姿态更新动作。
 */
imu_dt_action_t imu_classify_gyro_delta_us(uint32_t delta_us);

/**
 * @brief 接受新的陀螺仪时间戳并清除过期状态。
 *
 * @param timestamp_us 新样本时间戳，单位为微秒。
 * @param[out] last_gyro_timestamp_us 保存最后一次陀螺仪时间戳。
 * @param[out] last_gyro_timestamp_valid 标记时间戳是否有效。
 * @param[out] gyro_expired 标记陀螺仪是否过期。
 */
void imu_accept_new_gyro_sample(uint32_t timestamp_us,
                                uint32_t *last_gyro_timestamp_us,
                                bool *last_gyro_timestamp_valid,
                                bool *gyro_expired);

/**
 * @brief 计算当前陀螺仪时间戳状态条件。
 *
 * @param had_baseline 是否已有上一采样基线。
 * @param action 当前时间间隔对应的处理动作。
 * @return 时间戳无效时包含 IMU_STATUS_TIMESTAMP_INVALID，否则为 0。
 */
uint16_t imu_gyro_timestamp_condition(bool had_baseline,
                                      imu_dt_action_t action);

/**
 * @brief 按时间间隔策略更新运行态姿态估计器。
 *
 * @param estimator 要更新的姿态估计器。
 * @param had_baseline 是否已有上一陀螺仪时间基线。
 * @param action 时间间隔处理动作。
 * @param gyro_rad_s 去偏后的角速度，单位为弧度/秒。
 * @param accel_g 用于校正的加速度，单位为 g。
 * @param correction_valid 当前加速度是否可用于校正。
 * @param dt_s 积分时间间隔，单位为秒。
 * @return 姿态更新处理结果。
 */
imu_running_gyro_result_t imu_apply_running_gyro_timing(
    mahony_t *estimator,
    bool had_baseline,
    imu_dt_action_t action,
    imu_vec3f_t gyro_rad_s,
    imu_vec3f_t accel_g,
    bool correction_valid,
    float dt_s);

/**
 * @brief 计算陀螺仪样本距离过期阈值的剩余时间。
 *
 * @param has_gyro_timestamp 是否存在有效的最后陀螺仪时间戳。
 * @param now_us 当前时间，单位为微秒。
 * @param last_gyro_timestamp_us 最后陀螺仪样本时间戳，单位为微秒。
 * @return 剩余时间，单位为微秒；尚无有效时间戳时返回 UINT32_MAX。
 */
uint32_t imu_gyro_expiry_delay_us(bool has_gyro_timestamp,
                                  uint32_t now_us,
                                  uint32_t last_gyro_timestamp_us);

/**
 * @brief 判断最近的加速度样本能否用于陀螺仪姿态校正。
 *
 * @param newest_accel_g 最近的加速度值，单位为 g。
 * @param newest_accel_sequence 最近加速度样本的序列号。
 * @param consumed_accel_sequence 已消费加速度样本的序列号。
 * @param newest_accel_timestamp_us 加速度样本时间戳，单位为微秒。
 * @param gyro_timestamp_us 陀螺仪样本时间戳，单位为微秒。
 * @return 加速度样本满足序列、时间和幅值约束时为 true。
 */
bool imu_accel_correction_valid(imu_vec3f_t newest_accel_g,
                                uint32_t newest_accel_sequence,
                                uint32_t consumed_accel_sequence,
                                uint32_t newest_accel_timestamp_us,
                                uint32_t gyro_timestamp_us);

/**
 * @brief 判定校准样本应接受、跳过还是重置校准器。
 *
 * @param has_gyro_baseline 是否已有陀螺仪时间基线。
 * @param dt_action 陀螺仪时间间隔处理动作。
 * @param gyro_overrun 本次陀螺仪事件是否发生覆盖。
 * @param has_accel_sample 是否有可用加速度样本。
 * @param newest_accel_sequence 最近加速度样本的序列号。
 * @param consumed_accel_sequence 已消费加速度样本的序列号。
 * @param consumed_accel_timestamp_us 加速度样本时间戳，单位为微秒。
 * @param gyro_timestamp_us 陀螺仪样本时间戳，单位为微秒。
 * @return 校准准入判定结果。
 */
imu_calibration_admission_t imu_calibration_admission(
    bool has_gyro_baseline,
    imu_dt_action_t dt_action,
    bool gyro_overrun,
    bool has_accel_sample,
    uint32_t newest_accel_sequence,
    uint32_t consumed_accel_sequence,
    uint32_t consumed_accel_timestamp_us,
    uint32_t gyro_timestamp_us);

/**
 * @brief 根据准入判定将样本提交给校准器。
 *
 * @param calibration 要更新的校准器。
 * @param admission 校准准入判定结果。
 * @param accel_g 加速度样本，单位为 g。
 * @param gyro_dps 陀螺仪样本，单位为度/秒。
 * @param[out] step 接收校准器处理步骤。
 * @return 实际执行校准器处理时为 true；需要等待未消费加速度时为 false。
 */
bool imu_calibration_apply_admission(
    imu_calibration_t *calibration,
    imu_calibration_admission_t admission,
    imu_vec3f_t accel_g,
    imu_vec3f_t gyro_dps,
    imu_calibration_step_t *step);

/**
 * @brief 判断陀螺仪原始读数是否接近饱和边界。
 *
 * @param raw 陀螺仪原始采样值。
 * @return 任一轴达到饱和判定阈值时为 true。
 */
bool imu_gyro_saturated(bmi088_raw_sample_t raw);

/**
 * @brief 从角速度测量值中扣除校准得到的零偏。
 *
 * @param measured 原始角速度，单位为度/秒。
 * @param bias 校准零偏，单位为度/秒。
 * @return 去除零偏后的角速度，单位为度/秒。
 */
imu_vec3f_t imu_apply_gyro_bias(imu_vec3f_t measured, imu_vec3f_t bias);

/**
 * @brief 汇总 IMU 状态、估计器和各数据源的状态条件。
 *
 * @param state IMU 服务状态。
 * @param estimator_valid 姿态估计器是否有效。
 * @param state_conditions 服务级状态条件。
 * @param accel_conditions 加速度数据状态条件。
 * @param gyro_conditions 陀螺仪数据状态条件。
 * @return 汇总后的状态位掩码。
 */
uint16_t imu_service_status(imu_state_t state,
                            bool estimator_valid,
                            uint16_t state_conditions,
                            uint16_t accel_conditions,
                            uint16_t gyro_conditions);

/**
 * @brief 在陀螺仪无新样本且达到超时后转换为过期状态。
 *
 * @param state 当前 IMU 服务状态。
 * @param gyro_pending 是否仍有待处理的陀螺仪事件。
 * @param now_us 当前时间，单位为微秒。
 * @param has_last_gyro_timestamp 是否存在最后陀螺仪时间戳。
 * @param last_gyro_timestamp_us 最后陀螺仪样本时间戳，单位为微秒。
 * @param[out] gyro_expired 陀螺仪过期状态。
 * @param[out] gyro_conditions 陀螺仪状态条件。
 * @param[out] estimator 姿态估计器。
 * @return 状态变化需要发布新快照时为 true。
 */
bool imu_gyro_expiry_transition(imu_state_t state,
                                bool gyro_pending,
                                uint32_t now_us,
                                bool has_last_gyro_timestamp,
                                 uint32_t last_gyro_timestamp_us,
                                 bool *gyro_expired,
                                 uint16_t *gyro_conditions,
                                 mahony_t *estimator);

/**
 * @brief 根据 IMU 状态和阶段生成状态指示灯动作。
 *
 * @param state 当前 IMU 服务状态。
 * @param phase 当前指示灯阶段编号。
 * @return 当前阶段的持续时间、点亮状态和下一阶段编号。
 */
imu_led_step_t imu_led_step(imu_state_t state, uint8_t phase);

/**
 * @brief 计算下一次状态指示灯或诊断日志处理前的等待 tick 数。
 *
 * @param now 当前 tick。
 * @param led_deadline 指示灯处理截止 tick。
 * @param diagnostics_deadline 诊断日志处理截止 tick。
 * @return 到较早截止时间的等待 tick 数。
 */
uint32_t imu_housekeeping_wait_ticks(uint32_t now,
                                     uint32_t led_deadline,
                                     uint32_t diagnostics_deadline);

#endif
