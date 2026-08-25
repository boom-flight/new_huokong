#include "telemetry_dma_state.h"

void telemetry_dma_state_reset(telemetry_dma_state_t *state)
{
    *state = (telemetry_dma_state_t){0};
}

bool telemetry_dma_state_reserve(telemetry_dma_state_t *state)
{
    if (state->busy) {
        return false;
    }
    state->busy = true;
    return true;
}

void telemetry_dma_state_cancel(telemetry_dma_state_t *state)
{
    state->busy = false;
}

void telemetry_dma_state_complete(telemetry_dma_state_t *state)
{
    state->busy = false;
}

void telemetry_dma_state_async_error(telemetry_dma_state_t *state)
{
    if (!state->busy) {
        return;
    }
    state->busy = false;
    state->failure_pending = true;
}

bool telemetry_dma_state_busy(const telemetry_dma_state_t *state)
{
    return state->busy;
}

bool telemetry_dma_state_take_failure(telemetry_dma_state_t *state)
{
    const bool failed = state->failure_pending;

    state->failure_pending = false;
    return failed;
}
