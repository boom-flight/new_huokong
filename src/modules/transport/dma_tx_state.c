/**
 * @file dma_tx_state.c
 * @brief DMA 发送状态机的占用和异步错误处理实现。
 */

#include "transport/dma_tx_state.h"

void dma_tx_state_reset(dma_tx_state_t *state)
{
    *state = (dma_tx_state_t){0};
}

bool dma_tx_state_reserve(dma_tx_state_t *state)
{
    if (state->busy) {
        return false;
    }
    state->busy = true;
    return true;
}

void dma_tx_state_release(dma_tx_state_t *state)
{
    state->busy = false;
}

void dma_tx_state_async_error(dma_tx_state_t *state)
{
    if (!state->busy) {
        return;
    }
    state->busy = false;
    state->failure_pending = true;
}

bool dma_tx_state_busy(const dma_tx_state_t *state)
{
    return state->busy;
}

bool dma_tx_state_take_failure(dma_tx_state_t *state)
{
    const bool failed = state->failure_pending;

    state->failure_pending = false;
    return failed;
}
