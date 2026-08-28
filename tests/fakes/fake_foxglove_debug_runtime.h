#ifndef FAKE_FOXGLOVE_DEBUG_RUNTIME_H
#define FAKE_FOXGLOVE_DEBUG_RUNTIME_H

#include "imu/imu_snapshot.h"
#include "rtthread.h"
#include "transport/foxglove_debug_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool uart_init_result;
    foxglove_debug_uart_result_t uart_send_result;
    bool snapshot_result;
    imu_snapshot_t snapshot;
    rt_err_t thread_init_result;
    rt_err_t thread_startup_result;
    bool run_worker_on_delay;
    bool stop_on_send;
    bool stop_on_snapshot_read;
    unsigned uart_init_calls;
    unsigned uart_deinit_calls;
    unsigned uart_send_calls;
    size_t last_send_length;
    uint8_t last_frame[FOXGLOVE_DEBUG_UART_FRAME_SIZE];
    unsigned snapshot_read_calls;
    unsigned thread_init_calls;
    unsigned thread_startup_calls;
    unsigned thread_detach_calls;
    unsigned thread_suspend_calls;
    unsigned delay_until_calls;
    rt_tick_t last_delay_increment;
    unsigned delay_calls;
    unsigned defunct_execute_calls;
    rt_uint32_t last_stack_size;
    rt_uint8_t last_priority;
    void *last_stack_start;
    char operations[8];
    unsigned operation_count;
} fake_foxglove_debug_runtime_t;

void fake_foxglove_debug_runtime_reset(
    fake_foxglove_debug_runtime_t *runtime);
void fake_foxglove_debug_runtime_run_thread(void);
void fake_foxglove_debug_runtime_set_active(
    fake_foxglove_debug_runtime_t *runtime);

#endif
