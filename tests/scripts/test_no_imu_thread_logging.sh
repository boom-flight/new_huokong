#!/usr/bin/env bash

if rg -n 'rt_kprintf' src/kernel/imu/imu_service.c; then
    printf 'IMU service must not call rt_kprintf\n' >&2
    exit 1
fi

for required in '异步' '队列' 'gyro_overrun'; do
    if ! rg -Fq "$required" docs/requirements/firmware-behavior.md; then
        printf 'logging requirement missing: %s\n' "$required" >&2
        exit 1
    fi
done

debug_console=src/debug/foxglove_debug_console.c
if ! rg -Fq '#if defined(HUOKONG_FOXGLOVE_DEBUG)' "$debug_console" \
    || ! rg -Fq 'int rt_kprintf(const char *format, ...)' "$debug_console"; then
    printf 'debug console override is missing or not debug-only\n' >&2
    exit 1
fi
for source in src/debug/*.c; do
    if [[ "$source" != "$debug_console" ]] && rg -n 'rt_kprintf' "$source"; then
        printf 'debug runtime must not call rt_kprintf outside the override\n' >&2
        exit 1
    fi
done
