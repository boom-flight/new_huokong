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

rg -Fq 'telemetry_uart_stm32_init()' "$telemetry_service" \
    || fail 'telemetry service does not initialize telemetry UART'
rg -Fq 'telemetry_uart_stm32_deinit()' "$telemetry_service" \
    || fail 'telemetry service does not release telemetry UART on startup failure'
