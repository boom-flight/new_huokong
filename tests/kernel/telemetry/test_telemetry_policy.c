#include "telemetry/telemetry_policy.h"

#include <assert.h>
#include <stdint.h>

static void test_attempt_sequence_advances_while_drop_stays_sticky_until_queue(void)
{
    telemetry_attempt_state_t state = {0};

    assert(telemetry_attempt_begin(&state) == 0u);
    telemetry_attempt_dropped(&state);
    assert(state.next_sequence == 1u);
    assert(state.drops == 1u);
    assert(state.drop_sticky);

    assert(telemetry_attempt_begin(&state) == 1u);
    assert(state.drop_sticky);
    telemetry_attempt_dropped(&state);
    assert(state.next_sequence == 2u);
    assert(state.drops == 2u);
    assert(state.drop_sticky);

    assert(telemetry_attempt_begin(&state) == 2u);
    assert(state.drop_sticky);
    telemetry_attempt_queued(&state);
    assert(state.next_sequence == 3u);
    assert(state.drops == 2u);
    assert(!state.drop_sticky);
}

static void test_attempt_sequence_wraps_after_uint16_max(void)
{
    telemetry_attempt_state_t state = {.next_sequence = UINT16_MAX};

    assert(telemetry_attempt_begin(&state) == UINT16_MAX);
    telemetry_attempt_queued(&state);
    assert(state.next_sequence == 0u);
}

int main(void)
{
    test_attempt_sequence_advances_while_drop_stays_sticky_until_queue();
    test_attempt_sequence_wraps_after_uint16_max();
    return 0;
}
