/**
 * @file foxglove_debug_frame.c
 * @brief Pure C Foxglove debug snapshot frame encoder.
 */

#include "foxglove_debug_frame.h"
#include "rtconfig.h"

#include <string.h>

#if defined(HUOKONG_FOXGLOVE_DEBUG)

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void put_f32(uint8_t *out, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    put_u32(out, bits);
}

uint16_t foxglove_debug_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u
                      ? (uint16_t)((crc << 1) ^ 0x1021u)
                      : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void foxglove_debug_frame_encode(
    uint16_t sequence,
    const imu_snapshot_t *snapshot,
    uint8_t frame[FOXGLOVE_DEBUG_FRAME_SIZE])
{
    frame[0] = 0xD3u;
    frame[1] = 0x91u;
    frame[2] = 1u;
    frame[3] = 1u;
    put_u16(&frame[4], FOXGLOVE_DEBUG_PAYLOAD_SIZE);
    put_u16(&frame[6], sequence);
    put_u32(&frame[8], snapshot->timestamp_us);
    put_u16(&frame[12], snapshot->status);

    put_f32(&frame[14], snapshot->euler_deg.x);
    put_f32(&frame[18], snapshot->euler_deg.y);
    put_f32(&frame[22], snapshot->euler_deg.z);
    put_f32(&frame[26], snapshot->gyro_dps.x);
    put_f32(&frame[30], snapshot->gyro_dps.y);
    put_f32(&frame[34], snapshot->gyro_dps.z);
    put_f32(&frame[38], snapshot->accel_g.x);
    put_f32(&frame[42], snapshot->accel_g.y);
    put_f32(&frame[46], snapshot->accel_g.z);
    put_f32(&frame[50], snapshot->quaternion.w);
    put_f32(&frame[54], snapshot->quaternion.x);
    put_f32(&frame[58], snapshot->quaternion.y);
    put_f32(&frame[62], snapshot->quaternion.z);

    put_u32(&frame[66], snapshot->diagnostics.accel_samples);
    put_u32(&frame[70], snapshot->diagnostics.gyro_samples);
    put_u32(&frame[74], snapshot->diagnostics.accel_overruns);
    put_u32(&frame[78], snapshot->diagnostics.gyro_overruns);
    put_u32(&frame[82], snapshot->diagnostics.spi_errors);
    put_u32(&frame[86], snapshot->diagnostics.rejected_dt);
    put_u32(&frame[90], snapshot->diagnostics.long_gaps);
    put_u32(&frame[94], snapshot->diagnostics.sensor_reinitializations);
    put_u32(&frame[98], snapshot->diagnostics.telemetry_drops);
    put_u16(&frame[102], foxglove_debug_crc16_ccitt_false(&frame[2], 100u));
}

#else

typedef int foxglove_debug_frame_disabled_translation_unit_t;

#endif
