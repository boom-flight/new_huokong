/**
 * @file telemetry_policy.c
 * @brief 遥测发送序列号和丢弃粘滞状态的策略实现。
 */

#include "telemetry/telemetry_policy.h"

uint16_t telemetry_attempt_begin(telemetry_attempt_state_t *state)
{
    const uint16_t sequence = state->next_sequence;

    state->next_sequence = (uint16_t)(sequence + 1u);
    return sequence;
}

void telemetry_attempt_dropped(telemetry_attempt_state_t *state)
{
    state->drop_sticky = true;
}

void telemetry_attempt_queued(telemetry_attempt_state_t *state)
{
    state->drop_sticky = false;
}
