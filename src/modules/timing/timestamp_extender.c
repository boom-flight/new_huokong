/**
 * @file timestamp_extender.c
 * @brief 16 位计数器回绕后的时间戳合成实现。
 */

#include "timing/timestamp_extender.h"

uint32_t timestamp_extender_compose(uint16_t high_word, uint16_t counter,
                                    bool update_pending)
{
    if (update_pending && counter < 0x8000u) {
        ++high_word;
    }
    return ((uint32_t)high_word << 16) | counter;
}
