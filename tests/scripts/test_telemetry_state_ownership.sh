#!/usr/bin/env sh
set -eu

fail() {
    printf 'telemetry state ownership check failed: %s\n' "$1" >&2
    exit 1
}

policy_header=src/kernel/telemetry/telemetry_policy.h
log_event_header=src/kernel/logging/imu_log_event.h
log_event_source=src/kernel/logging/imu_log_event.c

if rg -n 'uint32_t drops|state->drops' "$policy_header" src/kernel/telemetry/telemetry_policy.c; then
    fail 'telemetry attempt state stores a duplicate drop counter'
fi

if rg -n 'uint32_t telemetry_drops' "$log_event_header"; then
    fail 'log event stores telemetry drops outside diagnostics'
fi
rg -Fq 'event->diagnostics.telemetry_drops' "$log_event_source" \
    || fail 'diagnostic formatter does not use the diagnostic drop counter'
