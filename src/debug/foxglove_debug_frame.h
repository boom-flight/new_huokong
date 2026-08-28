/**
 * @file foxglove_debug_frame.h
 * @brief Foxglove debug snapshot frame encoding interface.
 */

#ifndef FOXGLOVE_DEBUG_FRAME_H
#define FOXGLOVE_DEBUG_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "imu/imu_snapshot.h"

/** @brief Total Foxglove debug frame length in bytes. */
#define FOXGLOVE_DEBUG_FRAME_SIZE 104u
/** @brief Foxglove debug payload length in bytes. */
#define FOXGLOVE_DEBUG_PAYLOAD_SIZE 96u

/**
 * @brief Calculate a CRC-16/CCITT-FALSE checksum.
 * @param data Bytes to checksum.
 * @param length Number of bytes to checksum.
 * @return The CRC value with initial value 0xFFFF and polynomial 0x1021.
 */
uint16_t foxglove_debug_crc16_ccitt_false(const uint8_t *data, size_t length);

/**
 * @brief Encode an IMU snapshot as a fixed-length little-endian frame.
 * @param sequence Frame sequence number.
 * @param snapshot Snapshot to encode.
 * @param frame Output buffer containing FOXGLOVE_DEBUG_FRAME_SIZE bytes.
 */
void foxglove_debug_frame_encode(
    uint16_t sequence,
    const imu_snapshot_t *snapshot,
    uint8_t frame[FOXGLOVE_DEBUG_FRAME_SIZE]);

#endif
