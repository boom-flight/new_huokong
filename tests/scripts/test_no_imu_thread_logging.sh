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
