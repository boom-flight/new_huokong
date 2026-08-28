#include "fake_foxglove_debug_runtime.h"
#include "foxglove_debug.h"

#include <assert.h>
#include <stdint.h>

#if defined(HUOKONG_FOXGLOVE_DEBUG)

static void test_init_opens_uart_and_starts_one_thread(void)
{
    fake_foxglove_debug_runtime_t runtime;

    fake_foxglove_debug_runtime_reset(&runtime);
    assert(foxglove_debug_service_init());
    assert(runtime.uart_init_calls == 1u);
    assert(runtime.thread_init_calls == 1u);
    assert(runtime.thread_startup_calls == 1u);
    assert(runtime.last_stack_size == 512u);
    assert(runtime.last_priority == 25u);
    assert(runtime.delay_until_calls == 0u);

    runtime.run_worker_on_delay = true;
    assert(foxglove_debug_service_deinit());
    assert(runtime.uart_deinit_calls == 1u);
}

static void test_thread_sends_snapshot_frame_at_debug_period(void)
{
    fake_foxglove_debug_runtime_t runtime;

    fake_foxglove_debug_runtime_reset(&runtime);
    runtime.snapshot.timestamp_us = 0x12345678u;
    runtime.run_worker_on_delay = true;
    runtime.stop_on_send = true;
    assert(foxglove_debug_service_init());
    fake_foxglove_debug_runtime_run_thread();

    assert(runtime.delay_until_calls >= 1u);
    assert(runtime.last_delay_increment == 20u);
    assert(runtime.snapshot_read_calls == 1u);
    assert(runtime.uart_send_calls == 1u);
    assert(runtime.last_send_length == 104u);
    assert(runtime.last_frame[8] == 0x78u);
    assert(runtime.last_frame[9] == 0x56u);
    assert(runtime.last_frame[10] == 0x34u);
    assert(runtime.last_frame[11] == 0x12u);
    assert(foxglove_debug_drop_count() == 0u);
    assert(runtime.uart_deinit_calls == 1u);
}

static void test_short_write_increments_only_debug_drop_count(void)
{
    fake_foxglove_debug_runtime_t runtime;

    fake_foxglove_debug_runtime_reset(&runtime);
    runtime.uart_send_result = FOXGLOVE_DEBUG_UART_FAILED;
    runtime.run_worker_on_delay = true;
    runtime.stop_on_send = true;
    assert(foxglove_debug_service_init());
    fake_foxglove_debug_runtime_run_thread();

    assert(runtime.uart_send_calls == 1u);
    assert(foxglove_debug_drop_count() == 1u);
    assert(runtime.uart_deinit_calls == 1u);
}

static void test_snapshot_failure_increments_debug_drop_without_send(void)
{
    fake_foxglove_debug_runtime_t runtime;

    fake_foxglove_debug_runtime_reset(&runtime);
    runtime.snapshot_result = false;
    runtime.run_worker_on_delay = true;
    runtime.stop_on_snapshot_read = true;
    assert(foxglove_debug_service_init());
    fake_foxglove_debug_runtime_run_thread();

    assert(runtime.snapshot_read_calls == 1u);
    assert(runtime.uart_send_calls == 0u);
    assert(foxglove_debug_drop_count() == 1u);
    assert(runtime.uart_deinit_calls == 1u);
}

static void test_deinit_waits_before_closing_uart(void)
{
    fake_foxglove_debug_runtime_t runtime;

    fake_foxglove_debug_runtime_reset(&runtime);
    assert(foxglove_debug_service_init());
    runtime.run_worker_on_delay = true;
    assert(foxglove_debug_service_deinit());

    assert(runtime.operation_count == 4u);
    assert(runtime.operations[0] == 'S');
    assert(runtime.operations[1] == 'D');
    assert(runtime.operations[2] == 'X');
    assert(runtime.operations[3] == 'U');
    assert(runtime.uart_deinit_calls == 1u);
}

static void test_deinit_timeout_keeps_uart_open(void)
{
    fake_foxglove_debug_runtime_t runtime;

    fake_foxglove_debug_runtime_reset(&runtime);
    assert(foxglove_debug_service_init());
    assert(!foxglove_debug_service_deinit());
    assert(runtime.uart_deinit_calls == 0u);

    runtime.run_worker_on_delay = true;
    assert(foxglove_debug_service_deinit());
    assert(runtime.uart_deinit_calls == 1u);
}

int main(void)
{
    test_init_opens_uart_and_starts_one_thread();
    test_thread_sends_snapshot_frame_at_debug_period();
    test_short_write_increments_only_debug_drop_count();
    test_snapshot_failure_increments_debug_drop_without_send();
    test_deinit_waits_before_closing_uart();
    test_deinit_timeout_keeps_uart_open();
    return 0;
}

#else

int main(void)
{
    fake_foxglove_debug_runtime_t runtime;

    fake_foxglove_debug_runtime_reset(&runtime);
    assert(foxglove_debug_service_init());
    assert(foxglove_debug_service_deinit());
    assert(foxglove_debug_drop_count() == 0u);
    assert(runtime.uart_init_calls == 0u);
    assert(runtime.uart_deinit_calls == 0u);
    assert(runtime.uart_send_calls == 0u);
    assert(runtime.snapshot_read_calls == 0u);
    assert(runtime.thread_init_calls == 0u);
    assert(runtime.thread_startup_calls == 0u);
    return 0;
}

#endif
