#ifndef TIMESTAMP_EXTENDER_H
#define TIMESTAMP_EXTENDER_H

#include <stdbool.h>
#include <stdint.h>

uint32_t timestamp_extender_compose(uint16_t high_word, uint16_t counter,
                                    bool update_pending);

#endif
