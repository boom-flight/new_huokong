#!/usr/bin/env sh
set -eu

fail() {
    printf 'service ownership check failed: %s\n' "$1" >&2
    exit 1
}

imu_service=src/kernel/imu/imu_service.c
telemetry_service=src/kernel/telemetry/telemetry_service.c

if rg -n 'telemetry_uart_stm32_(init|deinit)' "$imu_service"; then
    fail 'IMU service owns telemetry UART lifecycle'
fi

rg -Fq '#include "transport/telemetry_uart.h"' "$telemetry_service" \
    || fail 'telemetry service does not include the generic telemetry UART contract'
if rg -n 'telemetry_uart_stm32\.h|telemetry_uart_stm32_' "$telemetry_service"; then
    fail 'telemetry service depends on the concrete STM32 telemetry UART adapter'
fi
rg -Fq 'telemetry_uart_init()' "$telemetry_service" \
    || fail 'telemetry service does not initialize telemetry UART through the generic contract'
rg -Fq 'telemetry_uart_deinit()' "$telemetry_service" \
    || fail 'telemetry service does not release telemetry UART on startup failure'
rg -Fq 'telemetry_uart_send(' "$telemetry_service" \
    || fail 'telemetry service does not send through the generic telemetry UART contract'
