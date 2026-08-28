/**
 * @file imu_log_service.c
 * @brief IMU 日志事件的异步队列和日志线程实现。
 */

#include "rtconfig.h"
#include "logging/imu_log_service.h"

#if defined(HUOKONG_FOXGLOVE_DEBUG)

bool imu_log_service_init(void)
{
    return true;
}

bool imu_log_service_deinit(void)
{
    return true;
}

bool imu_log_submit(imu_log_event_t event)
{
    (void)event;
    return true;
}

uint32_t imu_log_drop_count(void)
{
    return 0u;
}

#else

#include "service_lifecycle.h"
#include <rtthread.h>

#include <stddef.h>
#include <stdint.h>

enum {
    IMU_LOG_THREAD_PRIORITY = 20u,
    IMU_LOG_THREAD_STACK_SIZE = 512u,
    IMU_LOG_QUEUE_CAPACITY = 8u,
    IMU_LOG_BUFFER_SIZE = 192u,
};

static struct rt_messagequeue log_queue;
static rt_uint8_t log_queue_buffer[RT_MQ_BUF_SIZE(
    sizeof(imu_log_event_t), IMU_LOG_QUEUE_CAPACITY)];
static struct rt_thread log_thread;
rt_align(RT_ALIGN_SIZE)
static rt_uint8_t log_thread_stack[IMU_LOG_THREAD_STACK_SIZE];
static uint32_t log_drop_count;
static bool service_initialized;
static bool service_enabled;
static bool queue_initialized;
static bool thread_started;
static volatile bool thread_should_run;
static volatile bool thread_stopped;

/**
 * @brief 饱和递增日志丢弃计数，避免计数回绕。
 */
static void increment_drop_count(void)
{
    if (log_drop_count != UINT32_MAX) {
        ++log_drop_count;
    }
}

/**
 * @brief 日志线程入口，取出事件并格式化后输出到 RT-Thread 控制台。
 *
 * @param parameter RT-Thread 线程参数，当前未使用。
 */
static void imu_log_thread_entry(void *parameter)
{
    (void)parameter;

    while (thread_should_run) {
        imu_log_event_t event;
        char buffer[IMU_LOG_BUFFER_SIZE];

        if (rt_mq_recv(&log_queue, &event, sizeof event,
                       1u) < 0) {
            continue;
        }
        if (!thread_should_run) {
            break;
        }
        if (imu_log_event_format(&event, buffer, sizeof buffer) != 0u) {
            rt_kprintf("%s", buffer);
        }
    }
    thread_stopped = true;
    (void)rt_thread_suspend(&log_thread);
}

bool imu_log_service_init(void)
{
    rt_err_t result;

    if (service_initialized) {
        return service_enabled;
    }
    log_drop_count = 0u;
    service_enabled = false;
    queue_initialized = false;
    thread_started = false;
    thread_should_run = false;
    thread_stopped = false;

    result = rt_mq_init(&log_queue, "imu_log", log_queue_buffer,
                        sizeof(imu_log_event_t), sizeof log_queue_buffer,
                        RT_IPC_FLAG_FIFO);
    if (result != RT_EOK) {
        return false;
    }
    queue_initialized = true;
    result = rt_thread_init(&log_thread, "imu_log", imu_log_thread_entry,
                            NULL, log_thread_stack,
                            IMU_LOG_THREAD_STACK_SIZE,
                            IMU_LOG_THREAD_PRIORITY, 10u);
    if (result != RT_EOK) {
        (void)rt_mq_detach(&log_queue);
        queue_initialized = false;
        return false;
    }
    thread_should_run = true;
    result = rt_thread_startup(&log_thread);
    if (result != RT_EOK) {
        thread_should_run = false;
        (void)rt_thread_detach(&log_thread);
        rt_defunct_execute();
        (void)rt_mq_detach(&log_queue);
        queue_initialized = false;
        return false;
    }

    thread_started = true;
    service_initialized = true;
    service_enabled = true;
    return true;
}

bool imu_log_service_deinit(void)
{
    if (!service_initialized) {
        return true;
    }

    service_enabled = false;
    thread_should_run = false;
    if (thread_started && !service_wait_for_thread_stop(&thread_stopped)) {
        return false;
    }
    if (thread_started) {
        (void)rt_thread_detach(&log_thread);
        rt_defunct_execute();
    }
    if (queue_initialized) {
        (void)rt_mq_detach(&log_queue);
    }

    service_initialized = false;
    queue_initialized = false;
    thread_started = false;
    thread_stopped = false;
    return true;
}

bool imu_log_submit(imu_log_event_t event)
{
    if (!service_enabled ||
        rt_mq_send(&log_queue, &event, sizeof event) != RT_EOK) {
        increment_drop_count();
        return false;
    }
    return true;
}

uint32_t imu_log_drop_count(void)
{
    return log_drop_count;
}

#endif
