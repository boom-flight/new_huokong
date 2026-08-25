#ifndef TELEMETRY_DMA_STATE_H
#define TELEMETRY_DMA_STATE_H

#include <stdbool.h>

typedef struct {
    bool busy;
    bool failure_pending;
} telemetry_dma_state_t;

void telemetry_dma_state_reset(telemetry_dma_state_t *state);
bool telemetry_dma_state_reserve(telemetry_dma_state_t *state);
void telemetry_dma_state_cancel(telemetry_dma_state_t *state);
void telemetry_dma_state_complete(telemetry_dma_state_t *state);
void telemetry_dma_state_async_error(telemetry_dma_state_t *state);
bool telemetry_dma_state_busy(const telemetry_dma_state_t *state);
bool telemetry_dma_state_take_failure(telemetry_dma_state_t *state);

#endif
