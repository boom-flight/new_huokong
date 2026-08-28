#ifndef FAKE_FOXGLOVE_UART_DEVICE_H
#define FAKE_FOXGLOVE_UART_DEVICE_H

#include <rtthread.h>
#include "transport/foxglove_debug_uart.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    struct rt_device device;
    rt_device_t find_result;
    rt_err_t open_result;
    rt_ssize_t write_result;
    rt_err_t close_result;
    unsigned find_calls;
    unsigned open_calls;
    unsigned write_calls;
    unsigned close_calls;
    unsigned critical_enter_calls;
    unsigned critical_exit_calls;
    unsigned critical_depth;
    rt_device_t last_open_device;
    rt_uint16_t last_open_flags;
    rt_device_t last_write_device;
    rt_off_t last_write_position;
    const void *last_write_buffer;
    rt_size_t last_write_size;
    rt_device_t last_close_device;
    rt_device_t console_device;
    uint8_t write_data[FOXGLOVE_DEBUG_UART_FRAME_SIZE * 2u];
    rt_size_t write_data_size;
} fake_foxglove_uart_device_t;

void fake_foxglove_uart_device_init(fake_foxglove_uart_device_t *fake);
rt_device_t fake_foxglove_uart_device_handle(
    fake_foxglove_uart_device_t *fake);
void fake_foxglove_uart_device_set_find_result(
    fake_foxglove_uart_device_t *fake, rt_device_t device);
void fake_foxglove_uart_device_set_open_result(
    fake_foxglove_uart_device_t *fake, rt_err_t result);
void fake_foxglove_uart_device_set_write_result(
    fake_foxglove_uart_device_t *fake, rt_ssize_t result);
void fake_foxglove_uart_device_set_close_result(
    fake_foxglove_uart_device_t *fake, rt_err_t result);
void fake_foxglove_uart_device_set_open_state(
    fake_foxglove_uart_device_t *fake, rt_uint16_t open_flag,
    rt_uint8_t ref_count);
void fake_foxglove_uart_device_set_console_device(
    fake_foxglove_uart_device_t *fake, rt_device_t device);

#endif
