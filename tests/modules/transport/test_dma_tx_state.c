#include "transport/dma_tx_state.h"

#include <assert.h>

static void test_idle_reserve_marks_state_busy(void)
{
    dma_tx_state_t state;

    dma_tx_state_reset(&state);
    assert(dma_tx_state_reserve(&state));
    assert(dma_tx_state_busy(&state));
}

static void test_busy_reserve_is_rejected(void)
{
    dma_tx_state_t state;

    dma_tx_state_reset(&state);
    assert(dma_tx_state_reserve(&state));
    assert(!dma_tx_state_reserve(&state));
    assert(dma_tx_state_busy(&state));
}

static void test_normal_release_clears_busy_without_failure(void)
{
    dma_tx_state_t state;

    dma_tx_state_reset(&state);
    assert(dma_tx_state_reserve(&state));
    dma_tx_state_release(&state);

    assert(!dma_tx_state_busy(&state));
    assert(!dma_tx_state_take_failure(&state));
}

static void test_synchronous_start_failure_release_clears_busy_without_failure(void)
{
    dma_tx_state_t state;

    dma_tx_state_reset(&state);
    assert(dma_tx_state_reserve(&state));
    dma_tx_state_release(&state);

    assert(!dma_tx_state_busy(&state));
    assert(!dma_tx_state_take_failure(&state));
}

static void test_async_error_releases_busy_and_latches_one_failure(void)
{
    dma_tx_state_t state;

    dma_tx_state_reset(&state);
    assert(dma_tx_state_reserve(&state));
    assert(dma_tx_state_busy(&state));

    dma_tx_state_async_error(&state);
    assert(!dma_tx_state_busy(&state));
    assert(dma_tx_state_take_failure(&state));
    assert(!dma_tx_state_take_failure(&state));
    assert(dma_tx_state_reserve(&state));
}

static void test_idle_async_error_does_not_invent_a_failed_transfer(void)
{
    dma_tx_state_t state;

    dma_tx_state_reset(&state);
    dma_tx_state_async_error(&state);

    assert(!dma_tx_state_busy(&state));
    assert(!dma_tx_state_take_failure(&state));
}

int main(void)
{
    test_idle_reserve_marks_state_busy();
    test_busy_reserve_is_rejected();
    test_normal_release_clears_busy_without_failure();
    test_synchronous_start_failure_release_clears_busy_without_failure();
    test_async_error_releases_busy_and_latches_one_failure();
    test_idle_async_error_does_not_invent_a_failed_transfer();
    return 0;
}
