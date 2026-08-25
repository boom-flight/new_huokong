#ifndef TELEMETRY_POLICY_H
#define TELEMETRY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t next_sequence;
    uint32_t drops;
    bool drop_sticky;
} telemetry_attempt_state_t;

uint16_t telemetry_attempt_begin(telemetry_attempt_state_t *state);
void telemetry_attempt_dropped(telemetry_attempt_state_t *state);
void telemetry_attempt_queued(telemetry_attempt_state_t *state);

#endif
