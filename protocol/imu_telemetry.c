#include "imu_telemetry.h"

#include <math.h>

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

static float wrap_deg(float angle_deg)
{
    if (!isfinite(angle_deg)) {
        return angle_deg;
    }

    float wrapped = fmodf(angle_deg + 180.0f, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped - 180.0f;
}

static int16_t encode_i16(float value, float scale)
{
    if (!isfinite(value)) {
        return 0;
    }

    const float scaled = value * scale;
    if (scaled >= (float)INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled <= (float)INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)lroundf(scaled);
}

uint16_t imu_telemetry_crc16_ccitt_false(const uint8_t *data, size_t length)
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

void imu_telemetry_encode(uint16_t sequence,
                          const imu_telemetry_sample_t *sample,
                          uint8_t frame[IMU_TELEMETRY_FRAME_SIZE])
{
    frame[0] = 0xA5u;
    frame[1] = 0x5Au;
    frame[2] = IMU_TELEMETRY_VERSION;
    frame[3] = 26u;
    put_u16(&frame[4], sequence);
    put_u32(&frame[6], sample->timestamp_us);
    put_u16(&frame[10], (uint16_t)(sample->status & 0x01FFu));
    put_u16(&frame[12], (uint16_t)encode_i16(wrap_deg(sample->euler_deg.x), 100.0f));
    put_u16(&frame[14], (uint16_t)encode_i16(wrap_deg(sample->euler_deg.y), 100.0f));
    put_u16(&frame[16], (uint16_t)encode_i16(wrap_deg(sample->euler_deg.z), 100.0f));
    put_u16(&frame[18], (uint16_t)encode_i16(sample->gyro_dps.x, 10.0f));
    put_u16(&frame[20], (uint16_t)encode_i16(sample->gyro_dps.y, 10.0f));
    put_u16(&frame[22], (uint16_t)encode_i16(sample->gyro_dps.z, 10.0f));
    put_u16(&frame[24], (uint16_t)encode_i16(sample->accel_g.x, 1000.0f));
    put_u16(&frame[26], (uint16_t)encode_i16(sample->accel_g.y, 1000.0f));
    put_u16(&frame[28], (uint16_t)encode_i16(sample->accel_g.z, 1000.0f));
    put_u16(&frame[30], imu_telemetry_crc16_ccitt_false(&frame[2], 28u));
}
