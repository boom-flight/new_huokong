#include "fake_foxglove_uart_device.h"
#include "transport/foxglove_debug_uart.h"

#include <assert.h>
#include <string.h>
#include <stdint.h>

static uint8_t valid_frame[FOXGLOVE_DEBUG_UART_FRAME_SIZE];

static void test_open_console_stream_is_removed_and_restored(void)
{
    const rt_uint16_t console_open_flag =
        RT_DEVICE_OFLAG_RDWR | RT_DEVICE_OFLAG_OPEN | RT_DEVICE_FLAG_STREAM;
    fake_foxglove_uart_device_t fake;
    uint8_t frame[FOXGLOVE_DEBUG_UART_FRAME_SIZE] = {0};

    frame[17] = '\n';
    fake_foxglove_uart_device_init(&fake);
    fake_foxglove_uart_device_set_open_state(&fake, console_open_flag, 1u);
    fake_foxglove_uart_device_set_console_device(
        &fake, fake_foxglove_uart_device_handle(&fake));

    assert(foxglove_debug_uart_init());
    assert((fake.device.open_flag & RT_DEVICE_FLAG_STREAM) == 0u);
    assert(fake.device.ref_count == 2u);
    assert(foxglove_debug_uart_send(frame, sizeof frame) ==
           FOXGLOVE_DEBUG_UART_OK);
    assert(fake.write_data_size == sizeof frame);
    assert(memcmp(fake.write_data, frame, sizeof frame) == 0);

    foxglove_debug_uart_deinit();
    assert(fake.device.open_flag == console_open_flag);
    assert(fake.device.ref_count == 1u);
    assert(rt_console_get_device() == fake_foxglove_uart_device_handle(&fake));
    assert(fake.critical_enter_calls == 2u);
    assert(fake.critical_exit_calls == 2u);
    assert(fake.critical_depth == 0u);
}

static void test_valid_frame_uses_write_only_uart_device(void)
{
    fake_foxglove_uart_device_t fake;

    fake_foxglove_uart_device_init(&fake);
    assert(foxglove_debug_uart_init());
    assert(foxglove_debug_uart_send(valid_frame,
                                    FOXGLOVE_DEBUG_UART_FRAME_SIZE) ==
           FOXGLOVE_DEBUG_UART_OK);
    assert(fake.find_calls == 1u);
    assert(fake.open_calls == 1u);
    assert(fake.last_open_device == fake_foxglove_uart_device_handle(&fake));
    assert(fake.last_open_flags == RT_DEVICE_OFLAG_WRONLY);
    assert((fake.last_open_flags & RT_DEVICE_FLAG_STREAM) == 0u);
    assert(fake.write_calls == 1u);
    assert(fake.last_write_device == fake_foxglove_uart_device_handle(&fake));
    assert(fake.last_write_position == 0);
    assert(fake.last_write_buffer == valid_frame);
    assert(fake.last_write_size == FOXGLOVE_DEBUG_UART_FRAME_SIZE);

    foxglove_debug_uart_deinit();
    assert(fake.close_calls == 1u);
    assert(fake.critical_enter_calls == 2u);
    assert(fake.critical_exit_calls == 2u);
    assert(fake.critical_depth == 0u);
}

static void test_short_write_fails_and_deinit_closes_device_once(void)
{
    fake_foxglove_uart_device_t fake;

    fake_foxglove_uart_device_init(&fake);
    fake_foxglove_uart_device_set_write_result(
        &fake, FOXGLOVE_DEBUG_UART_FRAME_SIZE - 1u);
    assert(foxglove_debug_uart_init());
    assert(foxglove_debug_uart_send(valid_frame,
                                    FOXGLOVE_DEBUG_UART_FRAME_SIZE) ==
           FOXGLOVE_DEBUG_UART_FAILED);
    assert(fake.write_calls == 1u);

    foxglove_debug_uart_deinit();
    foxglove_debug_uart_deinit();
    assert(fake.close_calls == 1u);
}

static void test_invalid_frame_is_rejected_without_write(void)
{
    fake_foxglove_uart_device_t fake;

    fake_foxglove_uart_device_init(&fake);
    assert(foxglove_debug_uart_init());
    assert(foxglove_debug_uart_send(NULL,
                                    FOXGLOVE_DEBUG_UART_FRAME_SIZE) ==
           FOXGLOVE_DEBUG_UART_FAILED);
    assert(foxglove_debug_uart_send(valid_frame,
                                    FOXGLOVE_DEBUG_UART_FRAME_SIZE - 1u) ==
           FOXGLOVE_DEBUG_UART_FAILED);
    assert(foxglove_debug_uart_send(valid_frame,
                                    FOXGLOVE_DEBUG_UART_FRAME_SIZE + 1u) ==
           FOXGLOVE_DEBUG_UART_FAILED);
    assert(fake.write_calls == 0u);

    foxglove_debug_uart_deinit();
}

static void test_missing_or_unopenable_device_fails_without_state(void)
{
    const rt_uint16_t console_open_flag =
        RT_DEVICE_OFLAG_RDWR | RT_DEVICE_OFLAG_OPEN | RT_DEVICE_FLAG_STREAM;
    fake_foxglove_uart_device_t fake;

    fake_foxglove_uart_device_init(&fake);
    fake_foxglove_uart_device_set_find_result(&fake, NULL);
    assert(!foxglove_debug_uart_init());
    assert(fake.find_calls == 1u);
    assert(fake.open_calls == 0u);

    fake_foxglove_uart_device_init(&fake);
    fake_foxglove_uart_device_set_open_state(&fake, console_open_flag, 1u);
    fake_foxglove_uart_device_set_console_device(
        &fake, fake_foxglove_uart_device_handle(&fake));
    fake_foxglove_uart_device_set_open_result(&fake, RT_ERROR);
    assert(!foxglove_debug_uart_init());
    assert(fake.open_calls == 1u);
    assert(fake.write_calls == 0u);
    assert(fake.device.open_flag == console_open_flag);
    assert(fake.device.ref_count == 1u);
    assert(rt_console_get_device() == fake_foxglove_uart_device_handle(&fake));
    assert(fake.critical_enter_calls == 2u);
    assert(fake.critical_exit_calls == 2u);
    assert(fake.critical_depth == 0u);
    foxglove_debug_uart_deinit();
    assert(fake.close_calls == 0u);
}

static void test_close_error_does_not_repeat_close(void)
{
    fake_foxglove_uart_device_t fake;

    fake_foxglove_uart_device_init(&fake);
    fake_foxglove_uart_device_set_close_result(&fake, RT_ERROR);
    assert(foxglove_debug_uart_init());
    foxglove_debug_uart_deinit();
    foxglove_debug_uart_deinit();
    assert(fake.close_calls == 1u);
    assert(fake.critical_enter_calls == 2u);
    assert(fake.critical_exit_calls == 2u);
    assert(fake.critical_depth == 0u);
}

int main(void)
{
    test_open_console_stream_is_removed_and_restored();
    test_valid_frame_uses_write_only_uart_device();
    test_short_write_fails_and_deinit_closes_device_once();
    test_invalid_frame_is_rejected_without_write();
    test_missing_or_unopenable_device_fails_without_state();
    test_close_error_does_not_repeat_close();
    return 0;
}
