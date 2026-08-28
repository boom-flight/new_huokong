#include "fake_foxglove_debug_runtime.h"

#include "foxglove_debug.h"

#include <string.h>

static fake_foxglove_debug_runtime_t *active_runtime;
static struct rt_thread *started_thread;
static bool worker_running;

static void record_operation(char operation)
{
    if (active_runtime->operation_count < sizeof active_runtime->operations - 1u) {
        active_runtime->operations[active_runtime->operation_count++] = operation;
        active_runtime->operations[active_runtime->operation_count] = '\0';
    }
}

void fake_foxglove_debug_runtime_reset(
    fake_foxglove_debug_runtime_t *runtime)
{
    *runtime = (fake_foxglove_debug_runtime_t){
        .uart_init_result = true,
        .uart_send_result = FOXGLOVE_DEBUG_UART_OK,
        .snapshot_result = true,
        .thread_init_result = RT_EOK,
        .thread_startup_result = RT_EOK,
    };
    active_runtime = runtime;
    started_thread = NULL;
    worker_running = false;
}

void fake_foxglove_debug_runtime_set_active(
    fake_foxglove_debug_runtime_t *runtime)
{
    active_runtime = runtime;
}

void fake_foxglove_debug_runtime_run_thread(void)
{
    if (started_thread != NULL && !worker_running) {
        struct rt_thread *thread = started_thread;

        worker_running = true;
        thread->entry(thread->parameter);
        worker_running = false;
    }
}

bool foxglove_debug_uart_init(void)
{
    ++active_runtime->uart_init_calls;
    return active_runtime->uart_init_result;
}

void foxglove_debug_uart_deinit(void)
{
    ++active_runtime->uart_deinit_calls;
    record_operation('U');
}

foxglove_debug_uart_result_t foxglove_debug_uart_send(
    const uint8_t *frame, size_t length)
{
    ++active_runtime->uart_send_calls;
    active_runtime->last_send_length = length;
    if (length <= sizeof active_runtime->last_frame) {
        memcpy(active_runtime->last_frame, frame, length);
    }
    if (active_runtime->stop_on_send) {
        active_runtime->stop_on_send = false;
        (void)foxglove_debug_service_deinit();
    }
    return active_runtime->uart_send_result;
}

bool imu_snapshot_read(imu_snapshot_t *out)
{
    ++active_runtime->snapshot_read_calls;
    if (active_runtime->stop_on_snapshot_read) {
        active_runtime->stop_on_snapshot_read = false;
        (void)foxglove_debug_service_deinit();
    }
    if (!active_runtime->snapshot_result) {
        return false;
    }
    *out = active_runtime->snapshot;
    return true;
}

rt_err_t rt_thread_init(struct rt_thread *thread, const char *name,
                        void (*entry)(void *parameter), void *parameter,
                        void *stack_start, rt_uint32_t stack_size,
                        rt_uint8_t priority, rt_uint32_t tick)
{
    (void)name;
    (void)tick;
    ++active_runtime->thread_init_calls;
    active_runtime->last_stack_start = stack_start;
    active_runtime->last_stack_size = stack_size;
    active_runtime->last_priority = priority;
    if (active_runtime->thread_init_result != RT_EOK) {
        return active_runtime->thread_init_result;
    }
    thread->entry = entry;
    thread->parameter = parameter;
    thread->state = FAKE_RT_THREAD_INIT;
    return RT_EOK;
}

rt_err_t rt_thread_startup(struct rt_thread *thread)
{
    ++active_runtime->thread_startup_calls;
    if (active_runtime->thread_startup_result != RT_EOK) {
        return active_runtime->thread_startup_result;
    }
    thread->state = FAKE_RT_THREAD_RUNNING;
    started_thread = thread;
    return RT_EOK;
}

rt_err_t rt_thread_detach(struct rt_thread *thread)
{
    ++active_runtime->thread_detach_calls;
    record_operation('D');
    thread->state = FAKE_RT_THREAD_DEFUNCT;
    if (started_thread == thread) {
        started_thread = NULL;
    }
    return RT_EOK;
}

rt_err_t rt_thread_suspend(struct rt_thread *thread)
{
    ++active_runtime->thread_suspend_calls;
    record_operation('S');
    thread->state = FAKE_RT_THREAD_SUSPENDED;
    return RT_EOK;
}

rt_err_t rt_thread_delay_until(rt_tick_t *tick, rt_tick_t increment)
{
    ++active_runtime->delay_until_calls;
    active_runtime->last_delay_increment = increment;
    *tick += increment;
    return RT_EOK;
}

rt_err_t rt_thread_delay(rt_tick_t ticks)
{
    ++active_runtime->delay_calls;
    while (ticks != 0u) {
        if (active_runtime->run_worker_on_delay && started_thread != NULL) {
            started_thread->entry(started_thread->parameter);
        }
        --ticks;
    }
    return RT_EOK;
}

rt_tick_t rt_tick_get(void)
{
    return 0u;
}

void rt_defunct_execute(void)
{
    ++active_runtime->defunct_execute_calls;
    record_operation('X');
}

int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}
