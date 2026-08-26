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
        0xA5u, 0x5Au, 0x02u, 0x22u, 0xCDu, 0xABu, 0x12u, 0x34u,
        0x56u, 0x78u, 0xFFu, 0x01u, 0x2Du, 0xBAu, 0xD3u, 0x45u,
        0xD3u, 0x04u, 0xFFu, 0x7Fu, 0x00u, 0x80u, 0x0Du, 0x00u,
        0xFFu, 0x7Fu, 0x00u, 0x80u, 0xE9u, 0x03u, 0x00u, 0x40u,
        0x00u, 0xC0u, 0x82u, 0x5Au, 0x00u, 0x00u, 0x3Fu, 0x89u,
    };
    const imu_telemetry_sample_t sample = {
        .timestamp_us = 0x78563412u,
        .status = 0xFFFFu,
        .euler_deg = {181.25f, -181.25f, 12.345f},
        .gyro_dps = {4000.0f, -4000.0f, 1.25f},
        .accel_g = {40.0f, -40.0f, 1.001f},
        .quaternion = {0.5f, -0.5f, 0.70710678f, 0.0f},
    };
    uint8_t frame[IMU_TELEMETRY_FRAME_SIZE] = {0};

    imu_telemetry_encode(0xABCDu, &sample, frame);

    assert(sizeof frame == 40u);
    assert(memcmp(frame, expected, sizeof expected) == 0);
    assert(frame[0] == 0xA5u && frame[1] == 0x5Au);
    assert(frame[2] == 2u && frame[3] == 34u);
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
    assert((int16_t)test_le16(&frame[30]) == 16384);
    assert((int16_t)test_le16(&frame[32]) == -16384);
    assert((int16_t)test_le16(&frame[34]) == 23170);
    assert((int16_t)test_le16(&frame[36]) == 0);
    assert(test_le16(&frame[38]) == 0x893Fu);
}

static void test_encode_wraps_and_saturates_exact_boundaries(void)
{
    const imu_telemetry_sample_t sample = {
        .euler_deg = {-180.0f, 180.0f, 0.0f},
        .gyro_dps = {3276.7f, -3276.8f, 0.0f},
        .accel_g = {32.767f, -32.768f, 0.0f},
        .quaternion = {1.0f, -1.0f, 0.0f, 0.70710678f},
    };
    uint8_t frame[IMU_TELEMETRY_FRAME_SIZE] = {0};

    imu_telemetry_encode(0u, &sample, frame);

    assert((int16_t)test_le16(&frame[12]) == -18000);
    assert((int16_t)test_le16(&frame[14]) == -18000);
    assert((int16_t)test_le16(&frame[18]) == INT16_MAX);
    assert((int16_t)test_le16(&frame[20]) == INT16_MIN);
    assert((int16_t)test_le16(&frame[24]) == INT16_MAX);
    assert((int16_t)test_le16(&frame[26]) == INT16_MIN);
    assert((int16_t)test_le16(&frame[30]) == 32767);
    assert((int16_t)test_le16(&frame[32]) == -32767);
    assert((int16_t)test_le16(&frame[34]) == 0);
    assert((int16_t)test_le16(&frame[36]) == 23170);
}

static void test_encode_maps_non_finite_values_to_zero(void)
{
    const imu_telemetry_sample_t sample = {
        .euler_deg = {NAN, INFINITY, -INFINITY},
        .gyro_dps = {NAN, INFINITY, -INFINITY},
        .accel_g = {NAN, INFINITY, -INFINITY},
        .quaternion = {NAN, INFINITY, -INFINITY, NAN},
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
    assert(test_le16(&frame[30]) == 0u);
    assert(test_le16(&frame[32]) == 0u);
    assert(test_le16(&frame[34]) == 0u);
    assert(test_le16(&frame[36]) == 0u);
}

static void test_crc_covers_version_through_final_quaternion_byte(void)
{
    const imu_telemetry_sample_t zero_sample = {0};
    const imu_telemetry_sample_t changed_sample = {
        .quaternion.z = 256.0f / 32767.0f,
    };
    uint8_t zero_frame[IMU_TELEMETRY_FRAME_SIZE] = {0};
    uint8_t changed_frame[IMU_TELEMETRY_FRAME_SIZE] = {0};

    imu_telemetry_encode(0u, &zero_sample, zero_frame);
    imu_telemetry_encode(0u, &changed_sample, changed_frame);

    assert(test_le16(&zero_frame[38]) == 0x3AF1u);
    assert(memcmp(changed_frame, zero_frame, 37u) == 0);
    assert(changed_frame[36] == 0u);
    assert(zero_frame[37] == 0u);
    assert(changed_frame[37] == 1u);
    assert(changed_frame[37] != zero_frame[37]);
    assert(test_le16(&changed_frame[38]) != test_le16(&zero_frame[38]));

    uint8_t mutated_frame[IMU_TELEMETRY_FRAME_SIZE];
    memcpy(mutated_frame, zero_frame, sizeof mutated_frame);
    mutated_frame[37] ^= 0x01u;
    assert(imu_telemetry_crc16_ccitt_false(&mutated_frame[2], 36u) !=
           test_le16(&zero_frame[38]));

    mutated_frame[37] ^= 0x01u;
    mutated_frame[1] ^= 0x01u;
    assert(imu_telemetry_crc16_ccitt_false(&mutated_frame[2], 36u) ==
           test_le16(&zero_frame[38]));
}

int main(void)
{
    test_crc_matches_standard_check_vector();
    test_encode_writes_fixed_frame_layout();
    test_encode_wraps_and_saturates_exact_boundaries();
    test_encode_maps_non_finite_values_to_zero();
    test_crc_covers_version_through_final_quaternion_byte();
    return 0;
}
