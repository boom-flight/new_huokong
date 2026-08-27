/**
 * @file telemetry_service.c
 * @brief 周期读取 IMU 快照并通过 UART 异步发送遥测帧。
 */

#include "telemetry/telemetry_service.h"
#include "service_lifecycle.h"

#include "imu/imu_service.h"
#include "imu/imu_snapshot.h"
#include "imu_telemetry/imu_telemetry.h"
#include "telemetry/telemetry_policy.h"
#include "transport/telemetry_uart.h"

#include <rtthread.h>

#include <stdint.h>

static struct rt_thread telemetry_thread;
rt_align(RT_ALIGN_SIZE)
static rt_uint8_t telemetry_stack[TELEMETRY_THREAD_STACK_SIZE];
static uint8_t telemetry_tx_buffers[2][IMU_TELEMETRY_FRAME_SIZE];
static telemetry_attempt_state_t telemetry_attempt;
static uint8_t next_buffer;
static bool service_started;
static volatile bool thread_should_run;
static volatile bool thread_stopped;

/**
 * @brief 记录一次遥测发送丢弃，并同步更新 IMU 对外诊断计数。
 */
static void record_drop(void)
{
    telemetry_attempt_dropped(&telemetry_attempt);
    imu_service_record_telemetry_drop();
}

/**
 * @brief 遥测周期线程入口，编码并提交最新 IMU 快照。
 *
 * @param parameter RT-Thread 线程参数，当前未使用。
 */
static void telemetry_thread_entry(void *parameter)
{
    rt_tick_t wake_tick = rt_tick_get();

    (void)parameter;
    for (;;) {
        imu_snapshot_t snapshot;
        imu_telemetry_sample_t sample;
        uint8_t *frame;
        uint16_t sequence;

        (void)rt_thread_delay_until(&wake_tick, 5u);
        if (!thread_should_run) {
            break;
        }
        if (!imu_snapshot_read(&snapshot)) {
            snapshot = (imu_snapshot_t){
                .status = IMU_STATUS_TIMESTAMP_INVALID,
            };
        }

        sequence = telemetry_attempt_begin(&telemetry_attempt);
        sample = (imu_telemetry_sample_t){
            .timestamp_us = snapshot.timestamp_us,
            .status = (uint16_t)(
                snapshot.status |
                (telemetry_attempt.drop_sticky
                     ? IMU_STATUS_TELEMETRY_DROPPED
                     : 0u)),
            .euler_deg = snapshot.euler_deg,
            .gyro_dps = snapshot.gyro_dps,
            .accel_g = snapshot.accel_g,
            .quaternion = snapshot.quaternion,
        };
        frame = telemetry_tx_buffers[next_buffer];
        imu_telemetry_encode(sequence, &sample, frame);
        if (telemetry_uart_send(
                frame, IMU_TELEMETRY_FRAME_SIZE) ==
            TELEMETRY_UART_SEND_STARTED) {
            next_buffer ^= 1u;
            telemetry_attempt_queued(&telemetry_attempt);
        } else {
            record_drop();
        }
    }
    thread_stopped = true;
    (void)rt_thread_suspend(&telemetry_thread);
}

bool telemetry_service_init(void)
{
    rt_err_t result;

    if (service_started) {
        return thread_should_run;
    }

    telemetry_attempt = (telemetry_attempt_state_t){0};
    next_buffer = 0u;
    thread_should_run = false;
    thread_stopped = false;
    if (!telemetry_uart_init()) {
        return false;
    }
    result = rt_thread_init(
        &telemetry_thread, "telemetry", telemetry_thread_entry, RT_NULL,
        telemetry_stack, TELEMETRY_THREAD_STACK_SIZE,
        TELEMETRY_THREAD_PRIORITY, 10u);
    if (result != RT_EOK) {
        telemetry_uart_deinit();
        return false;
    }
    thread_should_run = true;
    result = rt_thread_startup(&telemetry_thread);
    if (result != RT_EOK) {
        thread_should_run = false;
        (void)rt_thread_detach(&telemetry_thread);
        rt_defunct_execute();
        telemetry_uart_deinit();
        return false;
    }

    service_started = true;
    return true;
}

bool telemetry_service_deinit(void)
{
    if (!service_started) {
        return true;
    }

    thread_should_run = false;
    if (!service_wait_for_thread_stop(&thread_stopped)) {
        return false;
    }
    (void)rt_thread_detach(&telemetry_thread);
    rt_defunct_execute();
    telemetry_uart_deinit();
    service_started = false;
    return true;
}
