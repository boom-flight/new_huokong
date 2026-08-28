#ifndef FOXGLOVE_DEBUG_H
#define FOXGLOVE_DEBUG_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the optional Foxglove debug snapshot service.
 * @return true when the service is enabled and started, or when debug is off.
 */
bool foxglove_debug_service_init(void);

/**
 * @brief Stop the optional Foxglove debug snapshot service.
 * @return true when the service is stopped, or when debug is off.
 */
bool foxglove_debug_service_deinit(void);

/**
 * @brief Read the number of debug snapshots dropped by the service.
 * @return Saturating debug snapshot drop count.
 */
uint32_t foxglove_debug_drop_count(void);

#endif
