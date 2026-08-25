#include "telemetry_service.h"

#include "drivers/bmi088_port.h"
#include "imu_service.h"
#include "imu_service_logic.h"
#include "imu_telemetry/imu_telemetry.h"

#include <rtthread.h>

#include <stdint.h>

static struct rt_thread telemetry_thread;
rt_align(RT_ALIGN_SIZE)
static rt_uint8_t telemetry_stack[TELEMETRY_THREAD_STACK_SIZE];
static uint8_t telemetry_tx_buffers[2][IMU_TELEMETRY_FRAME_SIZE];
static telemetry_attempt_state_t telemetry_attempt;
static uint8_t next_buffer;
static bool service_started;

static void record_drop(void)
{
    telemetry_attempt_dropped(&telemetry_attempt);
    imu_service_record_telemetry_drop();
}

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
        if (bmi088_port_telemetry_take_failure()) {
            record_drop();
        }
        if (!imu_snapshot_read(&snapshot)) {
            snapshot = (imu_snapshot_t){
                .status = IMU_STATUS_TIMESTAMP_INVALID,
            };
        }
        if (bmi088_port_telemetry_busy()) {
            (void)telemetry_attempt_begin(&telemetry_attempt);
            record_drop();
            continue;
        }
        if (bmi088_port_telemetry_take_failure()) {
            record_drop();
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
        };
        frame = telemetry_tx_buffers[next_buffer];
        imu_telemetry_encode(sequence, &sample, frame);
        if (bmi088_port_telemetry_try_start(
                frame, IMU_TELEMETRY_FRAME_SIZE)) {
            next_buffer ^= 1u;
            telemetry_attempt_queued(&telemetry_attempt);
        } else {
            record_drop();
        }
    }
}

bool telemetry_service_init(void)
{
    rt_err_t result;

    if (service_started) {
        return true;
    }

    telemetry_attempt = (telemetry_attempt_state_t){0};
    next_buffer = 0u;
    result = rt_thread_init(
        &telemetry_thread, "telemetry", telemetry_thread_entry, RT_NULL,
        telemetry_stack, TELEMETRY_THREAD_STACK_SIZE,
        TELEMETRY_THREAD_PRIORITY, 10u);
    if (result != RT_EOK) {
        return false;
    }
    result = rt_thread_startup(&telemetry_thread);
    if (result != RT_EOK) {
        (void)rt_thread_detach(&telemetry_thread);
        return false;
    }

    service_started = true;
    return true;
}
