#include "fake_foxglove_uart_device.h"

#include <assert.h>
#include <string.h>

static fake_foxglove_uart_device_t *active_fake;

void fake_foxglove_uart_device_init(fake_foxglove_uart_device_t *fake)
{
    *fake = (fake_foxglove_uart_device_t){
        .open_result = RT_EOK,
        .write_result = 104,
        .close_result = RT_EOK,
    };
    fake->find_result = &fake->device;
    active_fake = fake;
}

rt_device_t fake_foxglove_uart_device_handle(
    fake_foxglove_uart_device_t *fake)
{
    return &fake->device;
}

void fake_foxglove_uart_device_set_find_result(
    fake_foxglove_uart_device_t *fake, rt_device_t device)
{
    fake->find_result = device;
}

void fake_foxglove_uart_device_set_open_result(
    fake_foxglove_uart_device_t *fake, rt_err_t result)
{
    fake->open_result = result;
}

void fake_foxglove_uart_device_set_write_result(
    fake_foxglove_uart_device_t *fake, rt_ssize_t result)
{
    fake->write_result = result;
}

void fake_foxglove_uart_device_set_close_result(
    fake_foxglove_uart_device_t *fake, rt_err_t result)
{
    fake->close_result = result;
}

void fake_foxglove_uart_device_set_open_state(
    fake_foxglove_uart_device_t *fake, rt_uint16_t open_flag,
    rt_uint8_t ref_count)
{
    fake->device.open_flag = open_flag;
    fake->device.ref_count = ref_count;
}

void fake_foxglove_uart_device_set_console_device(
    fake_foxglove_uart_device_t *fake, rt_device_t device)
{
    fake->console_device = device;
}

rt_device_t rt_device_find(const char *name)
{
    (void)name;
    ++active_fake->find_calls;
    return active_fake->find_result;
}

rt_err_t rt_device_open(rt_device_t device, rt_uint16_t oflag)
{
    rt_uint16_t effective_flags = oflag;

    ++active_fake->open_calls;
    active_fake->last_open_device = device;
    active_fake->last_open_flags = oflag;
    if (active_fake->open_result != RT_EOK) {
        return active_fake->open_result;
    }
    if (device->open_flag & RT_DEVICE_FLAG_STREAM) {
        effective_flags |= RT_DEVICE_FLAG_STREAM;
    }
    device->open_flag = effective_flags | RT_DEVICE_OFLAG_OPEN;
    ++device->ref_count;
    return RT_EOK;
}

rt_ssize_t rt_device_write(rt_device_t device, rt_off_t position,
                           const void *buffer, rt_size_t size)
{
    rt_size_t index;

    ++active_fake->write_calls;
    active_fake->last_write_device = device;
    active_fake->last_write_position = position;
    active_fake->last_write_buffer = buffer;
    active_fake->last_write_size = size;
    active_fake->write_data_size = 0u;
    for (index = 0u; index < size; ++index) {
        const uint8_t value = ((const uint8_t *)buffer)[index];

        if ((device->open_flag & RT_DEVICE_FLAG_STREAM) && value == '\n') {
            active_fake->write_data[active_fake->write_data_size++] = '\r';
        }
        active_fake->write_data[active_fake->write_data_size++] = value;
    }
    return active_fake->write_result;
}

rt_err_t rt_device_close(rt_device_t device)
{
    ++active_fake->close_calls;
    active_fake->last_close_device = device;
    if (device->ref_count != 0u) {
        --device->ref_count;
        if (device->ref_count == 0u && active_fake->close_result == RT_EOK) {
            device->open_flag = 0u;
        }
    }
    return active_fake->close_result;
}

rt_device_t rt_console_get_device(void)
{
    return active_fake->console_device;
}

rt_base_t rt_enter_critical(void)
{
    ++active_fake->critical_enter_calls;
    ++active_fake->critical_depth;
    return 0;
}

void rt_exit_critical(void)
{
    assert(active_fake->critical_depth != 0u);
    ++active_fake->critical_exit_calls;
    --active_fake->critical_depth;
}
