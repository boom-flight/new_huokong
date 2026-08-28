#include "rtconfig.h"
#include "transport/foxglove_debug_uart.h"

#if defined(HUOKONG_FOXGLOVE_DEBUG)

#include <rtthread.h>

static rt_device_t uart_device;
static rt_uint16_t previous_open_flag;
static bool previous_state_saved;

bool foxglove_debug_uart_init(void)
{
    rt_device_t device;

    if (uart_device != NULL) {
        return true;
    }

    device = rt_device_find("uart1");
    if (device == NULL) {
        return false;
    }

    rt_enter_critical();
    previous_open_flag = device->open_flag;
    previous_state_saved = true;
    device->open_flag &= (rt_uint16_t)~RT_DEVICE_FLAG_STREAM;
    rt_exit_critical();
    if (rt_device_open(device, RT_DEVICE_OFLAG_WRONLY) != RT_EOK) {
        rt_enter_critical();
        device->open_flag = previous_open_flag;
        previous_state_saved = false;
        rt_exit_critical();
        return false;
    }

    uart_device = device;
    return true;
}

void foxglove_debug_uart_deinit(void)
{
    if (uart_device != NULL) {
        (void)rt_device_close(uart_device);
        rt_enter_critical();
        if (previous_state_saved) {
            uart_device->open_flag = previous_open_flag;
        }
        rt_exit_critical();
        uart_device = NULL;
        previous_state_saved = false;
    }
}

foxglove_debug_uart_result_t foxglove_debug_uart_send(const uint8_t *frame,
                                                      size_t length)
{
    rt_ssize_t written;

    if (frame == NULL || length != FOXGLOVE_DEBUG_UART_FRAME_SIZE ||
        uart_device == NULL) {
        return FOXGLOVE_DEBUG_UART_FAILED;
    }

    written = rt_device_write(uart_device, 0, frame, length);
    return written == (rt_ssize_t)length ? FOXGLOVE_DEBUG_UART_OK
                                         : FOXGLOVE_DEBUG_UART_FAILED;
}

#endif
