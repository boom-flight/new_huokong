#include "timestamp_extender.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

static void test_composes_high_word_and_counter(void)
{
    assert(timestamp_extender_compose(0x1234u, 0x5678u, false) ==
           0x12345678u);
}

static void test_pending_overflow_advances_only_a_post_wrap_counter(void)
{
    assert(timestamp_extender_compose(0x1234u, 0x0002u, true) ==
           0x12350002u);
    assert(timestamp_extender_compose(0x1234u, 0x8000u, true) ==
           0x12348000u);
}

static void test_high_word_wrap_is_natural_unsigned_wrap(void)
{
    const uint32_t before =
        timestamp_extender_compose(0xFFFFu, 0xFFFEu, false);
    const uint32_t after =
        timestamp_extender_compose(0xFFFFu, 0x0001u, true);

    assert(after == 0x00000001u);
    assert((uint32_t)(after - before) == 3u);
}

int main(void)
{
    test_composes_high_word_and_counter();
    test_pending_overflow_advances_only_a_post_wrap_counter();
    test_high_word_wrap_is_natural_unsigned_wrap();
    return 0;
}
