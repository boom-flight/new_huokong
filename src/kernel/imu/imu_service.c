/**
 * @file imu_service.c
 * @brief IMU 采样线程、姿态更新和快照发布的运行时实现。
 */

#include "imu/imu_service.h"

#include "attitude/imu_calibration.h"
#include "attitude/mahony.h"
#include "bmi088/bmi088.h"
#include "devices/bmi088_stm32.h"
#include "imu/imu_policy.h"
#include "logging/imu_log_event.h"
#include "logging/imu_log_service.h"
#include "service_lifecycle.h"
#include "time/monotonic_clock_stm32.h"

#include <board.h>
#include <drv_gpio.h>
#include <rtthread.h>

#include <stddef.h>
#include <stdint.h>

#define DEG_TO_RAD 0.01745329251994329577f
#define IMU_THREAD_PRIORITY 5u
#define IMU_THREAD_STACK_SIZE 768u
#define IMU_EVENT_ACCEL (1u << 0)
#define IMU_EVENT_GYRO (1u << 1)
#define STATE_LED_PIN GET_PIN(B, 6)

typedef struct {
    bmi088_t sensor;
    mahony_t estimator;
    imu_calibration_t calibration;
    imu_snapshot_t snapshot;
    imu_state_t state;
    imu_vec3f_t newest_accel_g;
    uint32_t newest_accel_timestamp_us;
    uint32_t consumed_accel_sequence;
    uint32_t consumed_gyro_sequence;
    uint32_t gyro_baseline_us;
    uint32_t last_gyro_timestamp_us;
    rt_tick_t state_entered_tick;
    rt_tick_t led_deadline;
    rt_tick_t diagnostics_deadline;
    uint16_t state_conditions;
    uint16_t accel_conditions;
    uint16_t gyro_conditions;
    uint8_t consecutive_read_failures;
    uint8_t led_phase;
    bool newest_accel_valid;
    bool gyro_baseline_valid;
    bool last_gyro_timestamp_valid;
    bool gyro_expired;
    bool led_output_valid;
    bool led_on;
    bool ids_logged;
    bool calibration_logged;
} imu_runtime_t;

static struct rt_event imu_event;
static struct rt_thread imu_thread;
rt_align(RT_ALIGN_SIZE)
static rt_uint8_t imu_stack[IMU_THREAD_STACK_SIZE];
static imu_snapshot_t snapshots[2];
static volatile uint8_t active_snapshot;
static uint32_t telemetry_drop_count;
static bool service_started;
static volatile bool thread_should_run;
static volatile bool thread_stopped;
static bool imu_thread_detached;

static void publish_runtime_snapshot(imu_runtime_t *runtime);
static bool service_gyro_expiry(imu_runtime_t *runtime, uint32_t now_us);

/**
 * @brief 脱离已停止的 IMU 线程，并处理 RT-Thread 的僵尸线程对象。
 *
 * @return 线程脱离成功时为 true。
 */
static bool detach_imu_thread(void)
{
    if (rt_thread_detach(&imu_thread) != RT_EOK) {
        return false;
    }
    rt_defunct_execute();
    return true;
}

/**
 * @brief 释放 IMU 服务占用的传感器、事件和时钟资源。
 *
 * @return 资源清理完成时为 true。
 */
static bool cleanup_imu_resources(void)
{
    bmi088_stm32_deinit();
    (void)rt_event_detach(&imu_event);
    monotonic_clock_stm32_deinit();
    return true;
}

/**
 * @brief 将传感器数据就绪通知转换为 IMU 线程事件。
 *
 * @param context 指向 IMU 事件对象的上下文。
 * @param event_mask 传感器驱动报告的数据就绪位掩码。
 */
static void notify_data_ready(void *context, uint32_t event_mask)
{
    struct rt_event *event = context;
    uint32_t events = 0u;

    if ((event_mask & BMI088_DRDY_EVENT_ACCEL) != 0u) {
        events |= IMU_EVENT_ACCEL;
    }
    if ((event_mask & BMI088_DRDY_EVENT_GYRO) != 0u) {
        events |= IMU_EVENT_GYRO;
    }
    if (events != 0u) {
        (void)rt_event_send(event, events);
    }
}

/**
 * @brief 判断当前 tick 是否已到达截止 tick。
 *
 * @param now 当前 tick。
 * @param deadline 截止 tick。
 * @return 已到达或超过截止时间时为 true。
 */
static bool tick_reached(rt_tick_t now, rt_tick_t deadline)
{
    return (rt_int32_t)(now - deadline) >= 0;
}

/**
 * @brief 按当前 IMU 状态推进状态指示灯的阶段机。
 *
 * @param runtime IMU 运行时状态。
 * @param now 当前 RT-Thread tick。
 */
static void update_state_led(imu_runtime_t *runtime, rt_tick_t now)
{
    while (tick_reached(now, runtime->led_deadline)) {
        const imu_led_step_t step =
            imu_led_step(runtime->state, runtime->led_phase);

        if (!runtime->led_output_valid || runtime->led_on != step.on) {
            rt_pin_write(STATE_LED_PIN, step.on ? PIN_HIGH : PIN_LOW);
            runtime->led_output_valid = true;
            runtime->led_on = step.on;
        }
        runtime->led_phase = step.next_phase;
        runtime->led_deadline += rt_tick_from_millisecond(step.duration_ms);
    }
}

/**
 * @brief 初始化状态相关的指示灯、诊断截止时间并记录初始状态。
 *
 * @param runtime IMU 运行时状态。
 */
static void log_initial_state(imu_runtime_t *runtime)
{
    const rt_tick_t now = rt_tick_get();

    runtime->state_entered_tick = now;
    runtime->led_deadline = now;
    runtime->diagnostics_deadline = now + RT_TICK_PER_SECOND;
    runtime->led_phase = 0u;
    (void)imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_INITIAL_STATE,
        .state = runtime->state,
    });
    update_state_led(runtime, now);
}

/**
 * @brief 更新 IMU 状态并重置对应的指示灯阶段。
 *
 * @param runtime IMU 运行时状态。
 * @param next 要切换到的目标状态。
 */
static void transition_state(imu_runtime_t *runtime, imu_state_t next)
{
    const imu_state_t previous = runtime->state;
    const rt_tick_t now = rt_tick_get();

    if (previous == next) {
        return;
    }
    runtime->state = next;
    runtime->state_entered_tick = now;
    runtime->led_deadline = now;
    runtime->led_phase = 0u;
    (void)imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_STATE,
        .previous_state = previous,
        .state = next,
    });
    update_state_led(runtime, now);
}

/**
 * @brief 只提交一次 BMI088 芯片 ID 日志。
 *
 * @param runtime IMU 运行时状态。
 * @param accel_id 加速度计芯片 ID。
 * @param gyro_id 陀螺仪芯片 ID。
 */
static void log_ids_once(imu_runtime_t *runtime, uint8_t accel_id,
                         uint8_t gyro_id)
{
    if (runtime->ids_logged) {
        return;
    }
    runtime->ids_logged = true;
    (void)imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_IDS,
        .accel_id = accel_id,
        .gyro_id = gyro_id,
    });
}

/**
 * @brief 只提交一次校准完成日志。
 *
 * @param runtime IMU 运行时状态。
 */
static void log_calibration_complete_once(imu_runtime_t *runtime)
{
    if (runtime->calibration_logged) {
        return;
    }
    runtime->calibration_logged = true;
    (void)imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_CALIBRATION_COMPLETE,
    });
}

/**
 * @brief 原子读取遥测丢弃计数。
 *
 * @return 当前遥测丢弃总数。
 */
static uint32_t telemetry_drops_read(void)
{
    const rt_base_t level = rt_hw_interrupt_disable();
    const uint32_t drops = telemetry_drop_count;

    rt_hw_interrupt_enable(level);
    return drops;
}

/**
 * @brief 到期时提交包含最新遥测丢弃数的诊断日志。
 *
 * @param runtime IMU 运行时状态。
 * @param now 当前 RT-Thread tick。
 */
static void log_diagnostics_if_due(imu_runtime_t *runtime, rt_tick_t now)
{
    imu_diagnostics_t diagnostics = runtime->snapshot.diagnostics;

    if (!tick_reached(now, runtime->diagnostics_deadline)) {
        return;
    }
    runtime->diagnostics_deadline = now + RT_TICK_PER_SECOND;
    diagnostics.telemetry_drops = telemetry_drops_read();
    (void)imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_DIAGNOSTICS,
        .diagnostics = diagnostics,
    });
}

/**
 * @brief 执行陀螺仪过期检测、指示灯更新和周期诊断日志处理。
 *
 * @param runtime IMU 运行时状态。
 */
static void service_housekeeping(imu_runtime_t *runtime)
{
    const rt_tick_t now = rt_tick_get();

    if (service_gyro_expiry(runtime, monotonic_clock_stm32_now_us())) {
        publish_runtime_snapshot(runtime);
    }
    update_state_led(runtime, now);
    log_diagnostics_if_due(runtime, now);
}

/**
 * @brief 计算 IMU 线程下一次唤醒所需等待的最小 tick 数。
 *
 * @param runtime IMU 运行时状态。
 * @return 指示灯、诊断日志或陀螺仪过期截止时间中的最短等待时间。
 */
static rt_tick_t housekeeping_wait(const imu_runtime_t *runtime)
{
    const rt_tick_t now = rt_tick_get();
    rt_tick_t wait = (rt_tick_t)imu_housekeeping_wait_ticks(
        (uint32_t)now, (uint32_t)runtime->led_deadline,
        (uint32_t)runtime->diagnostics_deadline);
    const bmi088_drdy_latch_t gyro_latch = bmi088_stm32_gyro_latch();

    if (runtime->state == IMU_RUNNING && !runtime->gyro_expired &&
        gyro_latch.sequence == runtime->consumed_gyro_sequence) {
        const uint32_t delay_us = imu_gyro_expiry_delay_us(
            runtime->last_gyro_timestamp_valid,
            monotonic_clock_stm32_now_us(),
            runtime->last_gyro_timestamp_us);

        if (delay_us != UINT32_MAX) {
            const rt_tick_t expiry_wait = rt_tick_from_millisecond(
                (rt_int32_t)((delay_us + 999u) / 1000u));
            if (expiry_wait < wait) {
                wait = expiry_wait;
            }
        }
    }
    return wait;
}

/**
 * @brief 对无符号诊断计数执行饱和递增。
 *
 * @param[in,out] value 要递增的计数器。
 */
static void increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX) {
        ++*value;
    }
}

/**
 * @brief 按比例缩放三轴浮点向量。
 *
 * @param value 输入向量。
 * @param scale 缩放系数。
 * @return 缩放后的向量。
 */
static imu_vec3f_t scale_vec(imu_vec3f_t value, float scale)
{
    return (imu_vec3f_t){value.x * scale, value.y * scale, value.z * scale};
}

/**
 * @brief 从姿态估计器刷新快照中的四元数和欧拉角。
 *
 * @param runtime IMU 运行时状态。
 */
static void update_attitude_fields(imu_runtime_t *runtime)
{
    runtime->snapshot.quaternion = mahony_quaternion(&runtime->estimator);
    runtime->snapshot.euler_deg = mahony_euler_deg(&runtime->estimator);
}

/**
 * @brief 将快照写入非活动缓冲区后原子切换活动快照索引。
 *
 * @param value 要发布的快照。
 */
static void publish_snapshot(const imu_snapshot_t *value)
{
    const uint8_t next = (uint8_t)(active_snapshot ^ 1u);
    rt_base_t level;

    snapshots[next] = *value;
    level = rt_hw_interrupt_disable();
    snapshots[next].diagnostics.telemetry_drops = telemetry_drop_count;
    active_snapshot = next;
    rt_hw_interrupt_enable(level);
}

/**
 * @brief 计算并发布运行时状态对应的 IMU 快照。
 *
 * @param runtime IMU 运行时状态。
 */
static void publish_runtime_snapshot(imu_runtime_t *runtime)
{
    runtime->snapshot.status =
        imu_service_status(runtime->state, runtime->estimator.valid,
                           runtime->state_conditions,
                           runtime->accel_conditions,
                           runtime->gyro_conditions);
    update_attitude_fields(runtime);
    publish_snapshot(&runtime->snapshot);
}

/**
 * @brief 检查陀螺仪样本是否已过期，并在需要时使估计器失效。
 *
 * @param runtime IMU 运行时状态。
 * @param now_us 当前时间，单位为微秒。
 * @return 过期状态变化需要发布快照时为 true。
 */
static bool service_gyro_expiry(imu_runtime_t *runtime, uint32_t now_us)
{
    const bmi088_drdy_latch_t gyro_latch = bmi088_stm32_gyro_latch();

    return imu_gyro_expiry_transition(
        runtime->state,
        gyro_latch.sequence != runtime->consumed_gyro_sequence,
        now_us, runtime->last_gyro_timestamp_valid,
        runtime->last_gyro_timestamp_us, &runtime->gyro_expired,
        &runtime->gyro_conditions, &runtime->estimator);
}

bool imu_snapshot_read(imu_snapshot_t *out)
{
    rt_base_t level;

    if (out == NULL) {
        return false;
    }
    level = rt_hw_interrupt_disable();
    *out = snapshots[active_snapshot];
    rt_hw_interrupt_enable(level);
    return true;
}

void imu_service_record_telemetry_drop(void)
{
    const rt_base_t level = rt_hw_interrupt_disable();

    if (telemetry_drop_count != UINT32_MAX) {
        ++telemetry_drop_count;
    }
    rt_hw_interrupt_enable(level);
}

/**
 * @brief 根据事件序列号判断是否有未消费的样本被覆盖。
 *
 * @param sequence 最新事件序列号。
 * @param consumed_sequence 已消费事件序列号。
 * @param[out] overrun_count 覆盖计数器。
 * @return 序列号间隔超过一个样本时为 true。
 */
static bool record_overrun(uint32_t sequence, uint32_t consumed_sequence,
                           uint32_t *overrun_count)
{
    if ((uint32_t)(sequence - consumed_sequence) <= 1u) {
        return false;
    }
    increment_saturating(overrun_count);
    return true;
}

/**
 * @brief 记录一次采样读取失败并在连续失败过多时切换到重试状态。
 *
 * @param runtime IMU 运行时状态。
 * @param timestamp_us 失败采样对应的时间戳，单位为微秒。
 */
static void publish_read_failure(imu_runtime_t *runtime, uint32_t timestamp_us)
{
    increment_saturating(&runtime->snapshot.diagnostics.spi_errors);
    if (runtime->consecutive_read_failures != UINT8_MAX) {
        ++runtime->consecutive_read_failures;
    }
    if (runtime->state == IMU_CALIBRATING) {
        imu_calibration_init(&runtime->calibration);
    }

    runtime->snapshot.timestamp_us = timestamp_us;
    if (runtime->consecutive_read_failures >= 3u) {
        mahony_invalidate(&runtime->estimator);
        runtime->newest_accel_valid = false;
        runtime->gyro_baseline_valid = false;
        transition_state(runtime, IMU_FAULT_RETRY);
    }
    publish_runtime_snapshot(runtime);
}

/**
 * @brief 在传感器初始化成功后重置估计、校准和数据就绪基线。
 *
 * @param runtime IMU 运行时状态。
 */
static void reset_after_sensor_init(imu_runtime_t *runtime)
{
    const imu_diagnostics_t diagnostics = runtime->snapshot.diagnostics;
    const imu_vec3f_t last_accel_g = runtime->snapshot.accel_g;

    runtime->estimator = (mahony_t){
        .q = {1.0f, 0.0f, 0.0f, 0.0f},
    };
    imu_calibration_init(&runtime->calibration);
    runtime->snapshot = (imu_snapshot_t){
        .quaternion = {1.0f, 0.0f, 0.0f, 0.0f},
        .accel_g = last_accel_g,
        .diagnostics = diagnostics,
    };
    runtime->newest_accel_g = (imu_vec3f_t){0.0f, 0.0f, 0.0f};
    runtime->newest_accel_timestamp_us = 0u;
    runtime->gyro_baseline_us = 0u;
    runtime->last_gyro_timestamp_us = 0u;
    runtime->state_conditions = 0u;
    runtime->accel_conditions = IMU_STATUS_ACCEL_CORRECTION_INVALID;
    runtime->gyro_conditions = IMU_STATUS_TIMESTAMP_INVALID;
    runtime->consecutive_read_failures = 0u;
    runtime->newest_accel_valid = false;
    runtime->gyro_baseline_valid = false;
    runtime->last_gyro_timestamp_valid = false;
    runtime->gyro_expired = false;
    (void)rt_event_recv(&imu_event, IMU_EVENT_ACCEL | IMU_EVENT_GYRO,
                        RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0u, NULL);
    {
        const bmi088_drdy_latch_t current_accel_latch =
            bmi088_stm32_accel_latch();
        const bmi088_drdy_latch_t current_gyro_latch =
            bmi088_stm32_gyro_latch();

        runtime->consumed_accel_sequence = current_accel_latch.sequence;
        runtime->consumed_gyro_sequence = current_gyro_latch.sequence;
    }
    transition_state(runtime, IMU_CALIBRATING);
    runtime->snapshot.timestamp_us = monotonic_clock_stm32_now_us();
    publish_runtime_snapshot(runtime);
}

/**
 * @brief 初始化 BMI088，并根据结果进入校准或故障重试状态。
 *
 * @param runtime IMU 运行时状态。
 */
static void initialize_sensor(imu_runtime_t *runtime)
{
    uint8_t accel_id = 0u;
    uint8_t gyro_id = 0u;
    const bmi088_result_t result =
        bmi088_init(&runtime->sensor, bmi088_stm32_bus(), BMI088_AXIS_MAP,
                    &accel_id, &gyro_id);

    if (result != BMI088_OK) {
        mahony_invalidate(&runtime->estimator);
        runtime->newest_accel_valid = false;
        runtime->gyro_baseline_valid = false;
        transition_state(runtime, IMU_FAULT_RETRY);
        runtime->state_conditions = IMU_STATUS_BMI_INIT_FAILED;
        runtime->accel_conditions = 0u;
        runtime->gyro_conditions = IMU_STATUS_TIMESTAMP_INVALID;
        runtime->snapshot.timestamp_us = monotonic_clock_stm32_now_us();
        publish_runtime_snapshot(runtime);
        return;
    }

    log_ids_once(runtime, accel_id, gyro_id);
    reset_after_sensor_init(runtime);
}

/**
 * @brief 读取并处理一个加速度事件，更新最新加速度和诊断信息。
 *
 * @param runtime IMU 运行时状态。
 */
static void process_accel(imu_runtime_t *runtime)
{
    const bmi088_drdy_latch_t before_latch = bmi088_stm32_accel_latch();
    bmi088_raw_sample_t raw;
    imu_vec3f_t accel_g;
    bmi088_result_t result;
    imu_sample_read_result_t sample_result;

    runtime->accel_conditions = 0u;

    if (record_overrun(before_latch.sequence, runtime->consumed_accel_sequence,
                       &runtime->snapshot.diagnostics.accel_overruns)) {
        runtime->accel_conditions |= IMU_STATUS_EVENT_OVERRUN;
        if (runtime->state == IMU_CALIBRATING) {
            imu_calibration_init(&runtime->calibration);
        }
    }

    result = bmi088_read_accel(&runtime->sensor, &raw, &accel_g);
    const bmi088_drdy_latch_t after_latch = bmi088_stm32_accel_latch();
    sample_result = imu_classify_sample_read(
        before_latch.sequence, after_latch.sequence, result == BMI088_OK);
    if (sample_result == IMU_SAMPLE_READ_CHANGED_LATCH ||
        sample_result == IMU_SAMPLE_READ_CHANGED_LATCH_FAILURE) {
        increment_saturating(&runtime->snapshot.diagnostics.accel_overruns);
        runtime->accel_conditions |= IMU_STATUS_EVENT_OVERRUN |
                                     IMU_STATUS_ACCEL_CORRECTION_INVALID;
        runtime->newest_accel_valid = false;
        if (runtime->state == IMU_CALIBRATING) {
            imu_calibration_init(&runtime->calibration);
        }
        if (sample_result == IMU_SAMPLE_READ_CHANGED_LATCH_FAILURE) {
            runtime->accel_conditions |= IMU_STATUS_SPI_ERROR;
            publish_read_failure(runtime, before_latch.timestamp_us);
        } else {
            publish_runtime_snapshot(runtime);
        }
        return;
    }

    runtime->consumed_accel_sequence = before_latch.sequence;
    if (sample_result == IMU_SAMPLE_READ_CONSISTENT_FAILURE) {
        runtime->newest_accel_valid = false;
        runtime->accel_conditions |= IMU_STATUS_ACCEL_CORRECTION_INVALID |
                                     IMU_STATUS_SPI_ERROR;
        publish_read_failure(runtime, before_latch.timestamp_us);
        return;
    }

    runtime->consecutive_read_failures = 0u;
    runtime->newest_accel_g = accel_g;
    runtime->newest_accel_timestamp_us = before_latch.timestamp_us;
    runtime->newest_accel_valid = true;
    runtime->snapshot.timestamp_us = before_latch.timestamp_us;
    runtime->snapshot.accel_g = accel_g;
    increment_saturating(&runtime->snapshot.diagnostics.accel_samples);
    if (!imu_accel_correction_valid(accel_g, before_latch.sequence,
                                    before_latch.sequence,
                                    before_latch.timestamp_us,
                                    before_latch.timestamp_us)) {
        runtime->accel_conditions |= IMU_STATUS_ACCEL_CORRECTION_INVALID;
    }
    (void)service_gyro_expiry(runtime, monotonic_clock_stm32_now_us());
    publish_runtime_snapshot(runtime);
}

/**
 * @brief 使用校准结果初始化姿态估计器并切换到运行态。
 *
 * @param runtime IMU 运行时状态。
 */
static void complete_calibration(imu_runtime_t *runtime)
{
    mahony_init_from_gravity(&runtime->estimator,
                             runtime->calibration.gravity_g, 0.2f, 0.0f);
    log_calibration_complete_once(runtime);
    transition_state(runtime, IMU_RUNNING);
    update_attitude_fields(runtime);
}

/**
 * @brief 更新加速度校正有效性对应的状态位。
 *
 * @param runtime IMU 运行时状态。
 * @param valid 当前加速度是否有效。
 */
static void set_accel_correction_condition(imu_runtime_t *runtime, bool valid)
{
    runtime->accel_conditions &=
        (uint16_t)~IMU_STATUS_ACCEL_CORRECTION_INVALID;
    if (!valid) {
        runtime->accel_conditions |= IMU_STATUS_ACCEL_CORRECTION_INVALID;
    }
}

/**
 * @brief 根据时间、覆盖和加速度关联条件处理校准阶段的陀螺仪样本。
 *
 * @param runtime IMU 运行时状态。
 * @param latch 当前陀螺仪数据就绪锁存信息。
 * @param gyro_dps 陀螺仪样本，单位为度/秒。
 * @param had_baseline 是否已有上一陀螺仪时间基线。
 * @param action 当前陀螺仪时间间隔处理动作。
 * @param overrun 本次陀螺仪事件是否发生覆盖。
 */
static void process_calibration_gyro(imu_runtime_t *runtime,
                                     const bmi088_drdy_latch_t *latch,
                                     imu_vec3f_t gyro_dps,
                                     bool had_baseline,
                                     imu_dt_action_t action,
                                     bool overrun)
{
    const bmi088_drdy_latch_t newest_latch = bmi088_stm32_accel_latch();
    const bool accel_valid =
        runtime->newest_accel_valid &&
        imu_accel_correction_valid(
            runtime->newest_accel_g, newest_latch.sequence,
            runtime->consumed_accel_sequence,
            runtime->newest_accel_timestamp_us, latch->timestamp_us);
    const imu_calibration_admission_t admission = imu_calibration_admission(
        had_baseline, action, overrun, runtime->newest_accel_valid,
        newest_latch.sequence,
        runtime->consumed_accel_sequence,
        runtime->newest_accel_timestamp_us, latch->timestamp_us);
    imu_calibration_step_t step;

    set_accel_correction_condition(runtime, accel_valid);
    if (!imu_calibration_apply_admission(
            &runtime->calibration, admission, runtime->newest_accel_g,
            gyro_dps, &step)) {
        return;
    }
    if (step == IMU_CALIBRATION_COMPLETE) {
        runtime->snapshot.gyro_dps = imu_apply_gyro_bias(
            gyro_dps, runtime->calibration.gyro_bias_dps);
        complete_calibration(runtime);
    }
}

/**
 * @brief 在运行态应用陀螺仪零偏并按有效时间间隔更新姿态估计器。
 *
 * @param runtime IMU 运行时状态。
 * @param latch 当前陀螺仪数据就绪锁存信息。
 * @param gyro_dps 陀螺仪样本，单位为度/秒。
 * @param had_baseline 是否已有上一陀螺仪时间基线。
 * @param action 当前陀螺仪时间间隔处理动作。
 * @param delta_us 当前时间间隔，单位为微秒。
 */
static void process_running_gyro(imu_runtime_t *runtime,
                                 const bmi088_drdy_latch_t *latch,
                                 imu_vec3f_t gyro_dps,
                                 bool had_baseline,
                                 imu_dt_action_t action,
                                 uint32_t delta_us)
{
    bool correction_valid = false;

    runtime->snapshot.gyro_dps = imu_apply_gyro_bias(
        gyro_dps, runtime->calibration.gyro_bias_dps);

    if (had_baseline && action == IMU_DT_INTEGRATE) {
        const bmi088_drdy_latch_t newest_latch = bmi088_stm32_accel_latch();

        correction_valid =
            runtime->newest_accel_valid &&
            imu_accel_correction_valid(
                runtime->newest_accel_g, newest_latch.sequence,
                runtime->consumed_accel_sequence,
                runtime->newest_accel_timestamp_us, latch->timestamp_us);
        set_accel_correction_condition(runtime, correction_valid);
    }

    const imu_running_gyro_result_t result = imu_apply_running_gyro_timing(
        &runtime->estimator,
        had_baseline,
        action,
        scale_vec(runtime->snapshot.gyro_dps, DEG_TO_RAD),
        runtime->newest_accel_g, correction_valid,
        (float)delta_us * 0.000001f);
    if (result.rejected_dt) {
        increment_saturating(&runtime->snapshot.diagnostics.rejected_dt);
    }
    if (result.long_gap) {
        increment_saturating(&runtime->snapshot.diagnostics.long_gaps);
    }
}

/**
 * @brief 读取并处理一个陀螺仪事件，分派到校准或运行流程。
 *
 * @param runtime IMU 运行时状态。
 */
static void process_gyro(imu_runtime_t *runtime)
{
    const bmi088_drdy_latch_t before_latch = bmi088_stm32_gyro_latch();
    const bool newer_sample =
        before_latch.sequence != runtime->consumed_gyro_sequence;
    const bool had_baseline = runtime->gyro_baseline_valid;
    const uint32_t delta_us =
        (uint32_t)(before_latch.timestamp_us - runtime->gyro_baseline_us);
    const imu_dt_action_t action = imu_classify_gyro_delta_us(delta_us);
    bmi088_raw_sample_t raw;
    imu_vec3f_t gyro_dps;
    bool overrun;
    bmi088_result_t result;
    imu_sample_read_result_t sample_result;

    runtime->gyro_conditions =
        imu_gyro_timestamp_condition(had_baseline, action);

    overrun = record_overrun(before_latch.sequence, runtime->consumed_gyro_sequence,
                             &runtime->snapshot.diagnostics.gyro_overruns);
    if (overrun) {
        runtime->gyro_conditions |= IMU_STATUS_EVENT_OVERRUN;
    }

    result = bmi088_read_gyro(&runtime->sensor, &raw, &gyro_dps);
    const bmi088_drdy_latch_t after_latch = bmi088_stm32_gyro_latch();
    sample_result = imu_classify_sample_read(
        before_latch.sequence, after_latch.sequence, result == BMI088_OK);
    if (sample_result == IMU_SAMPLE_READ_CHANGED_LATCH ||
        sample_result == IMU_SAMPLE_READ_CHANGED_LATCH_FAILURE) {
        increment_saturating(&runtime->snapshot.diagnostics.gyro_overruns);
        runtime->gyro_conditions |= IMU_STATUS_EVENT_OVERRUN |
                                    IMU_STATUS_TIMESTAMP_INVALID;
        if (runtime->state == IMU_CALIBRATING) {
            imu_calibration_init(&runtime->calibration);
        }
        if (sample_result == IMU_SAMPLE_READ_CHANGED_LATCH_FAILURE) {
            runtime->gyro_conditions |= IMU_STATUS_SPI_ERROR;
            publish_read_failure(runtime, before_latch.timestamp_us);
        } else {
            publish_runtime_snapshot(runtime);
        }
        return;
    }

    runtime->gyro_baseline_us = before_latch.timestamp_us;
    runtime->gyro_baseline_valid = true;
    runtime->consumed_gyro_sequence = before_latch.sequence;
    if (sample_result == IMU_SAMPLE_READ_CONSISTENT_FAILURE) {
        runtime->gyro_conditions |= IMU_STATUS_SPI_ERROR;
        publish_read_failure(runtime, before_latch.timestamp_us);
        return;
    }

    runtime->consecutive_read_failures = 0u;
    if (newer_sample) {
        imu_accept_new_gyro_sample(
            before_latch.timestamp_us, &runtime->last_gyro_timestamp_us,
            &runtime->last_gyro_timestamp_valid, &runtime->gyro_expired);
    }
    runtime->snapshot.timestamp_us = before_latch.timestamp_us;
    runtime->snapshot.gyro_dps = gyro_dps;
    increment_saturating(&runtime->snapshot.diagnostics.gyro_samples);
    if (imu_gyro_saturated(raw)) {
        runtime->gyro_conditions |= IMU_STATUS_GYRO_SATURATED;
    }

    if (runtime->state == IMU_CALIBRATING) {
        process_calibration_gyro(runtime, &before_latch, gyro_dps, had_baseline,
                                 action, overrun);
    } else {
        process_running_gyro(runtime, &before_latch, gyro_dps, had_baseline, action,
                             delta_us);
    }

    publish_runtime_snapshot(runtime);
}

/**
 * @brief 在故障重试等待期间维持指示灯和诊断日志，并准备重新初始化。
 *
 * @param runtime IMU 运行时状态。
 */
static void wait_fault_retry(imu_runtime_t *runtime)
{
    const rt_tick_t retry_deadline =
        runtime->state_entered_tick + RT_TICK_PER_SECOND;

    for (;;) {
        const rt_tick_t now = rt_tick_get();
        rt_tick_t delay;

        if (!thread_should_run) {
            return;
        }
        update_state_led(runtime, now);
        log_diagnostics_if_due(runtime, now);
        if (tick_reached(now, retry_deadline)) {
            break;
        }

        delay = runtime->led_deadline - now;
        if ((rt_tick_t)(retry_deadline - now) < delay) {
            delay = retry_deadline - now;
        }
        if (delay == 0u) {
            continue;
        }
        rt_thread_delay(delay);
    }

    increment_saturating(
        &runtime->snapshot.diagnostics.sensor_reinitializations);
    (void)rt_event_recv(&imu_event, IMU_EVENT_ACCEL | IMU_EVENT_GYRO,
                        RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0u, NULL);
    {
        const bmi088_drdy_latch_t accel_latch = bmi088_stm32_accel_latch();
        const bmi088_drdy_latch_t gyro_latch = bmi088_stm32_gyro_latch();

        runtime->consumed_accel_sequence = accel_latch.sequence;
        runtime->consumed_gyro_sequence = gyro_latch.sequence;
    }
    transition_state(runtime, IMU_INITIALIZING);
}

/**
 * @brief IMU 工作线程入口，驱动采样、状态转换和快照发布。
 *
 * @param parameter RT-Thread 线程参数，当前未使用。
 */
static void imu_thread_entry(void *parameter)
{
    imu_runtime_t runtime = {
        .estimator = {.q = {1.0f, 0.0f, 0.0f, 0.0f}},
        .snapshot = {
            .quaternion = {1.0f, 0.0f, 0.0f, 0.0f},
        },
        .state = IMU_INITIALIZING,
        .gyro_conditions = IMU_STATUS_TIMESTAMP_INVALID,
    };
    (void)parameter;

    publish_runtime_snapshot(&runtime);
    log_initial_state(&runtime);
    while (thread_should_run) {
        uint32_t received = 0u;

        service_housekeeping(&runtime);
        if (runtime.state == IMU_INITIALIZING) {
            initialize_sensor(&runtime);
            continue;
        }
        if (runtime.state == IMU_FAULT_RETRY) {
            wait_fault_retry(&runtime);
            continue;
        }
        if (rt_event_recv(&imu_event, IMU_EVENT_ACCEL | IMU_EVENT_GYRO,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          housekeeping_wait(&runtime), &received) != RT_EOK) {
            continue;
        }
        if (!thread_should_run) {
            break;
        }
        if ((received & IMU_EVENT_GYRO) != 0u) {
            process_gyro(&runtime);
        }
        if (runtime.state != IMU_FAULT_RETRY &&
            (received & IMU_EVENT_ACCEL) != 0u) {
            process_accel(&runtime);
        }
    }
    thread_stopped = true;
    (void)rt_thread_suspend(&imu_thread);
}

bool imu_service_init(void)
{
    rt_err_t result;

    if (service_started) {
        return thread_should_run;
    }

    snapshots[0] = (imu_snapshot_t){0};
    snapshots[1] = (imu_snapshot_t){0};
    active_snapshot = 0u;
    telemetry_drop_count = 0u;
    thread_should_run = false;
    thread_stopped = false;
    imu_thread_detached = false;

    if (!monotonic_clock_stm32_init()) {
        return false;
    }
    result = rt_event_init(&imu_event, "imu_evt", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK) {
        monotonic_clock_stm32_deinit();
        return false;
    }
    result = rt_thread_init(&imu_thread, "imu", imu_thread_entry, NULL,
                            imu_stack, IMU_THREAD_STACK_SIZE,
                            IMU_THREAD_PRIORITY, 10u);
    if (result != RT_EOK) {
        (void)rt_event_detach(&imu_event);
        monotonic_clock_stm32_deinit();
        return false;
    }
    if (!bmi088_stm32_init(notify_data_ready, &imu_event)) {
        (void)rt_thread_detach(&imu_thread);
        imu_thread_detached = true;
        rt_defunct_execute();
        (void)rt_event_detach(&imu_event);
        monotonic_clock_stm32_deinit();
        return false;
    }
    if (!imu_log_service_init()) {
        bmi088_stm32_deinit();
        (void)rt_thread_detach(&imu_thread);
        imu_thread_detached = true;
        rt_defunct_execute();
        (void)rt_event_detach(&imu_event);
        monotonic_clock_stm32_deinit();
        return false;
    }
    thread_should_run = true;
    result = rt_thread_startup(&imu_thread);
    if (result != RT_EOK) {
        const bool logging_cleanup_ok = imu_log_service_deinit();

        thread_should_run = false;
        bmi088_stm32_deinit();
        (void)rt_thread_detach(&imu_thread);
        imu_thread_detached = true;
        rt_defunct_execute();
        (void)rt_event_detach(&imu_event);
        monotonic_clock_stm32_deinit();
        return logging_cleanup_ok && result == RT_EOK;
    }
    service_started = true;
    return true;
}

bool imu_service_deinit(void)
{
    if (!service_started) {
        return true;
    }

    thread_should_run = false;
    if (!thread_stopped) {
        (void)rt_event_send(&imu_event, IMU_EVENT_ACCEL | IMU_EVENT_GYRO);
        if (!service_wait_for_thread_stop(&thread_stopped)) {
            return false;
        }
    }
    return service_cleanup_child_then_parent(
        &service_started, &imu_thread_detached, imu_log_service_deinit,
        detach_imu_thread, cleanup_imu_resources);
}
