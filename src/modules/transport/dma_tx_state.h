#ifndef DMA_TX_STATE_H
#define DMA_TX_STATE_H

#include <stdbool.h>

typedef struct {
    bool busy;
    bool failure_pending;
} dma_tx_state_t;

void dma_tx_state_reset(dma_tx_state_t *state);
bool dma_tx_state_reserve(dma_tx_state_t *state);
void dma_tx_state_cancel(dma_tx_state_t *state);
void dma_tx_state_complete(dma_tx_state_t *state);
void dma_tx_state_async_error(dma_tx_state_t *state);
bool dma_tx_state_busy(const dma_tx_state_t *state);
bool dma_tx_state_take_failure(dma_tx_state_t *state);

#endif
