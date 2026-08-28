#!/usr/bin/env sh
set -eu

fail() {
    printf 'IMU platform boundary check failed: %s\n' "$1" >&2
    exit 1
}

imu_service=src/kernel/imu/imu_service.c
board=src/platform/board/stm32f103c8/board.c
main=src/app/main.c
status_led=src/platform/indicators/status_led.h
bmi088_contract=src/platform/devices/bmi088.h
clock_contract=src/platform/time/monotonic_clock.h
bmi088_adapter=src/platform/devices/bmi088_stm32.c
clock_adapter=src/platform/time/monotonic_clock_stm32.c
debug_adapter=src/platform/transport/foxglove_debug_uart_stm32.c

if rg -n '\bUSART1_IRQHandler\b|HAL_UART_(TxCplt|Error)Callback|HAL_UART_Msp(Init|DeInit)' "$debug_adapter"; then
    fail 'Foxglove UART adapter owns a forbidden USART1 IRQ or HAL callback'
fi

if rg -n '#include\s+[<"](board\.h|drv_gpio\.h)[>"]|GET_PIN\(|rt_pin_(mode|write)\(' "$imu_service"; then
    fail 'IMU service contains board or GPIO implementation details'
fi
if rg -n '#include\s+[<"][^>"]*_stm32\.h[>"]|\b(bmi088|monotonic_clock)_stm32_' "$imu_service"; then
    fail 'IMU service references a concrete STM32 adapter'
fi

test -f "$status_led" || fail 'status LED contract is missing'
test -f "$bmi088_contract" || fail 'generic BMI088 contract is missing'
test -f "$clock_contract" || fail 'generic clock contract is missing'
rg -Fq 'void status_led_init(void);' "$status_led" \
    || fail 'status LED init contract is missing'
rg -Fq 'void status_led_set(bool on);' "$status_led" \
    || fail 'status LED set contract is missing'
rg -Fq 'bool bmi088_platform_init(bmi088_drdy_notify_fn notify, void *context);' \
    "$bmi088_contract" || fail 'generic BMI088 init contract is missing'
rg -Fq 'bmi088_bus_t bmi088_platform_bus(void);' "$bmi088_contract" \
    || fail 'generic BMI088 bus contract is missing'
rg -Fq 'bool monotonic_clock_init(void);' "$clock_contract" \
    || fail 'generic clock init contract is missing'
rg -Fq 'uint32_t monotonic_clock_now_us(void);' "$clock_contract" \
    || fail 'generic clock read contract is missing'
rg -Fq 'rt_uint32_t received = 0u;' "$imu_service" \
    || fail 'IMU event receiver does not use the RT-Thread event type'
rg -Fq 'bool bmi088_platform_init(' "$bmi088_adapter" \
    || fail 'STM32 BMI088 adapter does not implement the generic contract'
rg -Fq 'uint32_t monotonic_clock_now_us(void)' "$clock_adapter" \
    || fail 'STM32 clock adapter does not implement the generic contract'
rg -Fq '#include "indicators/status_led.h"' "$imu_service" \
    || fail 'IMU service does not include the status LED contract'
rg -Fq '#include "devices/bmi088.h"' "$imu_service" \
    || fail 'IMU service does not include the generic BMI088 contract'
rg -Fq '#include "time/monotonic_clock.h"' "$imu_service" \
    || fail 'IMU service does not include the generic clock contract'

rg -Fq '#include "indicators/status_led.h"' "$board" \
    || fail 'board does not implement the status LED contract'
rg -Fq 'GET_PIN(B, 6)' "$board" \
    || fail 'board does not own the status LED pin'
rg -Fq 'status_led_init();' "$main" \
    || fail 'application does not initialize the status LED contract'

printf 'IMU platform boundary checks passed\n'
