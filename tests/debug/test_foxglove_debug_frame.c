#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "foxglove_debug_frame.h"

static uint16_t test_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static uint32_t test_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void test_crc_matches_standard_check_vector(void)
{
    static const uint8_t check[] = "123456789";

    assert(foxglove_debug_crc16_ccitt_false(check, 9u) == 0x29B1u);
}

static void test_encode_writes_exact_snapshot_frame(void)
{
    static const uint8_t expected[FOXGLOVE_DEBUG_FRAME_SIZE] = {
        0xD3u, 0x91u, 0x01u, 0x01u, 0x60u, 0x00u, 0xEFu, 0xBEu,
        0x78u, 0x56u, 0x34u, 0x12u, 0xA5u, 0x01u, 0x00u, 0x00u,
        0xC0u, 0x3Fu, 0x00u, 0x00u, 0x10u, 0xC0u, 0x00u, 0x00u,
        0xC8u, 0x42u, 0x00u, 0x00u, 0x00u, 0xBFu, 0x00u, 0x00u,
        0x50u, 0x40u, 0x00u, 0x00u, 0x20u, 0xC1u, 0x00u, 0x00u,
        0x80u, 0x3Eu, 0x00u, 0x00u, 0xC0u, 0xBFu, 0xCDu, 0xCCu,
        0x1Cu, 0x41u, 0x00u, 0x00u, 0x80u, 0x3Fu, 0x00u, 0x00u,
        0x00u, 0xBFu, 0x00u, 0x00u, 0x40u, 0x3Fu, 0x00u, 0x00u,
        0x00u, 0xC0u, 0x04u, 0x03u, 0x02u, 0x01u, 0x44u, 0x33u,
        0x22u, 0x11u, 0x05u, 0x00u, 0x00u, 0x00u, 0x06u, 0x00u,
        0x00u, 0x00u, 0x07u, 0x00u, 0x00u, 0x00u, 0x08u, 0x00u,
        0x00u, 0x00u, 0x09u, 0x00u, 0x00u, 0x00u, 0xDDu, 0xCCu,
        0xBBu, 0xAAu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x9Du, 0x04u,
    };
    const imu_snapshot_t snapshot = {
        .timestamp_us = 0x12345678u,
        .status = 0x01A5u,
        .quaternion = {1.0f, -0.5f, 0.75f, -2.0f},
        .euler_deg = {1.5f, -2.25f, 100.0f},
        .gyro_dps = {-0.5f, 3.25f, -10.0f},
        .accel_g = {0.25f, -1.5f, 9.8f},
        .diagnostics = {
            .accel_samples = 0x01020304u,
            .gyro_samples = 0x11223344u,
            .accel_overruns = 5u,
            .gyro_overruns = 6u,
            .spi_errors = 7u,
            .rejected_dt = 8u,
            .long_gaps = 9u,
            .sensor_reinitializations = 0xAABBCCDDu,
            .telemetry_drops = UINT32_MAX,
        },
    };
    uint8_t frame[FOXGLOVE_DEBUG_FRAME_SIZE] = {0};

    foxglove_debug_frame_encode(0xBEEFu, &snapshot, frame);

    assert(sizeof frame == 104u);
    assert(memcmp(frame, expected, sizeof expected) == 0);
    assert(frame[0] == 0xD3u && frame[1] == 0x91u);
    assert(frame[2] == 1u && frame[3] == 1u);
    assert(test_le16(&frame[4]) == 96u);
    assert(test_le16(&frame[6]) == 0xBEEFu);
    assert(frame[8] == 0x78u && frame[9] == 0x56u &&
           frame[10] == 0x34u && frame[11] == 0x12u);
    assert(test_le16(&frame[12]) == 0x01A5u);
    assert(test_le32(&frame[14]) == 0x3FC00000u);
    assert(test_le32(&frame[18]) == 0xC0100000u);
    assert(test_le32(&frame[22]) == 0x42C80000u);
    assert(test_le32(&frame[26]) == 0xBF000000u);
    assert(test_le32(&frame[30]) == 0x40500000u);
    assert(test_le32(&frame[34]) == 0xC1200000u);
    assert(test_le32(&frame[38]) == 0x3E800000u);
    assert(test_le32(&frame[42]) == 0xBFC00000u);
    assert(test_le32(&frame[46]) == 0x411CCCCDu);
    assert(test_le32(&frame[50]) == 0x3F800000u);
    assert(test_le32(&frame[54]) == 0xBF000000u);
    assert(test_le32(&frame[58]) == 0x3F400000u);
    assert(test_le32(&frame[62]) == 0xC0000000u);
    assert(test_le32(&frame[66]) == 0x01020304u);
    assert(test_le32(&frame[70]) == 0x11223344u);
    assert(test_le32(&frame[74]) == 5u);
    assert(test_le32(&frame[78]) == 6u);
    assert(test_le32(&frame[82]) == 7u);
    assert(test_le32(&frame[86]) == 8u);
    assert(test_le32(&frame[90]) == 9u);
    assert(test_le32(&frame[94]) == 0xAABBCCDDu);
    assert(test_le32(&frame[98]) == UINT32_MAX);
    assert(test_le16(&frame[102]) == 0x049Du);
}

static void test_encode_preserves_sequence_wrap_values(void)
{
    const imu_snapshot_t snapshot = {0};
    uint8_t before_wrap[FOXGLOVE_DEBUG_FRAME_SIZE] = {0};
    uint8_t after_wrap[FOXGLOVE_DEBUG_FRAME_SIZE] = {0};

    foxglove_debug_frame_encode(0xFFFFu, &snapshot, before_wrap);
    foxglove_debug_frame_encode(0x0000u, &snapshot, after_wrap);

    assert(test_le16(&before_wrap[6]) == 0xFFFFu);
    assert(test_le16(&after_wrap[6]) == 0x0000u);
    assert(memcmp(&before_wrap[8], &after_wrap[8],
                  FOXGLOVE_DEBUG_FRAME_SIZE - 8u) != 0);
}

int main(void)
{
    test_crc_matches_standard_check_vector();
    test_encode_writes_exact_snapshot_frame();
    test_encode_preserves_sequence_wrap_values();
    return 0;
}
