/**
 * @file foxglove_debug.c
 * @brief Optional low-priority Foxglove debug snapshot service.
 */

#include "rtconfig.h"
#include "foxglove_debug.h"

#if defined(HUOKONG_FOXGLOVE_DEBUG)

#include "foxglove_debug_frame.h"
#include "imu/imu_service.h"
#include "service_lifecycle.h"
#include "transport/foxglove_debug_uart.h"

#include <rtthread.h>

enum {
    FOXGLOVE_DEBUG_THREAD_PRIORITY = 25u,
    FOXGLOVE_DEBUG_THREAD_STACK_SIZE = 512u,
    FOXGLOVE_DEBUG_PERIOD_TICKS = 20u,
};

static struct rt_thread debug_thread;
rt_align(FOXGLOVE_DEBUG_THREAD_STACK_SIZE)
static rt_uint8_t debug_thread_stack[FOXGLOVE_DEBUG_THREAD_STACK_SIZE];
static uint8_t debug_frame[FOXGLOVE_DEBUG_FRAME_SIZE];
static uint16_t sequence;
static uint32_t drop_count;
static bool service_started;
static volatile bool thread_should_run;
static volatile bool thread_stopped;

static void increment_drop_count(void)
{
    if (drop_count != UINT32_MAX) {
        ++drop_count;
    }
}

static void foxglove_debug_thread_entry(void *parameter)
{
    rt_tick_t wake_tick = rt_tick_get();

    (void)parameter;
    for (;;) {
        imu_snapshot_t snapshot;

        (void)rt_thread_delay_until(&wake_tick, FOXGLOVE_DEBUG_PERIOD_TICKS);
        if (!thread_should_run) {
            break;
        }
        if (!imu_snapshot_read(&snapshot)) {
            increment_drop_count();
            continue;
        }

        foxglove_debug_frame_encode(sequence, &snapshot, debug_frame);
        ++sequence;
        if (foxglove_debug_uart_send(
                debug_frame, FOXGLOVE_DEBUG_FRAME_SIZE) !=
            FOXGLOVE_DEBUG_UART_OK) {
            increment_drop_count();
        }
    }
    thread_stopped = true;
    (void)rt_thread_suspend(&debug_thread);
}

bool foxglove_debug_service_init(void)
{
    rt_err_t result;

    if (service_started) {
        return thread_should_run;
    }

    sequence = 0u;
    drop_count = 0u;
    thread_should_run = false;
    thread_stopped = false;
    if (!foxglove_debug_uart_init()) {
        return false;
    }
    result = rt_thread_init(
        &debug_thread, "foxglove", foxglove_debug_thread_entry, NULL,
        debug_thread_stack, FOXGLOVE_DEBUG_THREAD_STACK_SIZE,
        FOXGLOVE_DEBUG_THREAD_PRIORITY, 10u);
    if (result != RT_EOK) {
        foxglove_debug_uart_deinit();
        return false;
    }

    thread_should_run = true;
    result = rt_thread_startup(&debug_thread);
    if (result != RT_EOK) {
        thread_should_run = false;
        (void)rt_thread_detach(&debug_thread);
        rt_defunct_execute();
        foxglove_debug_uart_deinit();
        return false;
    }

    service_started = true;
    return true;
}

bool foxglove_debug_service_deinit(void)
{
    if (!service_started) {
        return true;
    }

    thread_should_run = false;
    if (!service_wait_for_thread_stop(&thread_stopped)) {
        return false;
    }
    (void)rt_thread_detach(&debug_thread);
    rt_defunct_execute();
    foxglove_debug_uart_deinit();
    service_started = false;
    thread_stopped = false;
    return true;
}

uint32_t foxglove_debug_drop_count(void)
{
    return drop_count;
}

#else

typedef int foxglove_debug_disabled_translation_unit_t;

#endif
