#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "imu_telemetry/imu_telemetry.h"
#include "test_common.h"

static void test_crc_matches_standard_check_vector(void)
{
    static const uint8_t check[] = "123456789";

    assert(imu_telemetry_crc16_ccitt_false(check, 9u) == 0x29B1u);
}

static void test_encode_writes_fixed_frame_layout(void)
{
    static const uint8_t expected[IMU_TELEMETRY_FRAME_SIZE] = {
        0xA5u, 0x5Au, 0x01u, 0x1Au, 0xCDu, 0xABu, 0x12u, 0x34u,
        0x56u, 0x78u, 0xFFu, 0x01u, 0x2Du, 0xBAu, 0xD3u, 0x45u,
        0xD3u, 0x04u, 0xFFu, 0x7Fu, 0x00u, 0x80u, 0x0Du, 0x00u,
        0xFFu, 0x7Fu, 0x00u, 0x80u, 0xE9u, 0x03u, 0x9Bu, 0xAFu,
    };
    const imu_telemetry_sample_t sample = {
        .timestamp_us = 0x78563412u,
        .status = 0xFFFFu,
        .euler_deg = {181.25f, -181.25f, 12.345f},
        .gyro_dps = {4000.0f, -4000.0f, 1.25f},
        .accel_g = {40.0f, -40.0f, 1.001f},
    };
    uint8_t frame[IMU_TELEMETRY_FRAME_SIZE] = {0};

    imu_telemetry_encode(0xABCDu, &sample, frame);

    assert(sizeof frame == 32u);
    assert(memcmp(frame, expected, sizeof expected) == 0);
    assert(frame[0] == 0xA5u && frame[1] == 0x5Au);
    assert(frame[2] == 1u && frame[3] == 26u);
    assert(test_le16(&frame[4]) == 0xABCDu);
    assert(test_le32(&frame[6]) == 0x78563412u);
    assert(test_le16(&frame[10]) == 0x01FFu);
    assert((int16_t)test_le16(&frame[12]) == -17875);
    assert((int16_t)test_le16(&frame[14]) == 17875);
    assert((int16_t)test_le16(&frame[16]) == 1235);
    assert((int16_t)test_le16(&frame[18]) == INT16_MAX);
    assert((int16_t)test_le16(&frame[20]) == INT16_MIN);
    assert((int16_t)test_le16(&frame[22]) == 13);
    assert((int16_t)test_le16(&frame[24]) == INT16_MAX);
    assert((int16_t)test_le16(&frame[26]) == INT16_MIN);
    assert((int16_t)test_le16(&frame[28]) == 1001);
    assert(test_le16(&frame[30]) == 0xAF9Bu);
}

static void test_encode_wraps_and_saturates_exact_boundaries(void)
{
    const imu_telemetry_sample_t sample = {
        .euler_deg = {-180.0f, 180.0f, 0.0f},
        .gyro_dps = {3276.7f, -3276.8f, 0.0f},
        .accel_g = {32.767f, -32.768f, 0.0f},
    };
    uint8_t frame[IMU_TELEMETRY_FRAME_SIZE] = {0};

    imu_telemetry_encode(0u, &sample, frame);

    assert((int16_t)test_le16(&frame[12]) == -18000);
    assert((int16_t)test_le16(&frame[14]) == -18000);
    assert((int16_t)test_le16(&frame[18]) == INT16_MAX);
    assert((int16_t)test_le16(&frame[20]) == INT16_MIN);
    assert((int16_t)test_le16(&frame[24]) == INT16_MAX);
    assert((int16_t)test_le16(&frame[26]) == INT16_MIN);
}

static void test_encode_maps_non_finite_values_to_zero(void)
{
    const imu_telemetry_sample_t sample = {
        .euler_deg = {NAN, INFINITY, -INFINITY},
        .gyro_dps = {NAN, INFINITY, -INFINITY},
        .accel_g = {NAN, INFINITY, -INFINITY},
    };
    uint8_t frame[IMU_TELEMETRY_FRAME_SIZE] = {0};

    imu_telemetry_encode(0u, &sample, frame);

    assert(test_le16(&frame[12]) == 0u);
    assert(test_le16(&frame[14]) == 0u);
    assert(test_le16(&frame[16]) == 0u);
    assert(test_le16(&frame[18]) == 0u);
    assert(test_le16(&frame[20]) == 0u);
    assert(test_le16(&frame[22]) == 0u);
    assert(test_le16(&frame[24]) == 0u);
    assert(test_le16(&frame[26]) == 0u);
    assert(test_le16(&frame[28]) == 0u);
}

static void test_crc_covers_version_through_final_acceleration_byte(void)
{
    const imu_telemetry_sample_t sample = {0};
    uint8_t frame[IMU_TELEMETRY_FRAME_SIZE] = {0};

    imu_telemetry_encode(0u, &sample, frame);
    assert(test_le16(&frame[30]) == 0x4FB6u);

    frame[29] ^= 0x01u;
    assert(imu_telemetry_crc16_ccitt_false(&frame[2], 28u) == 0x5F97u);
    assert(imu_telemetry_crc16_ccitt_false(&frame[2], 28u) !=
           test_le16(&frame[30]));

    frame[29] ^= 0x01u;
    frame[1] ^= 0x01u;
    assert(imu_telemetry_crc16_ccitt_false(&frame[2], 28u) == 0x4FB6u);
    assert(imu_telemetry_crc16_ccitt_false(&frame[2], 28u) ==
           test_le16(&frame[30]));
}

int main(void)
{
    test_crc_matches_standard_check_vector();
    test_encode_writes_fixed_frame_layout();
    test_encode_wraps_and_saturates_exact_boundaries();
    test_encode_maps_non_finite_values_to_zero();
    test_crc_covers_version_through_final_acceleration_byte();
    return 0;
}
