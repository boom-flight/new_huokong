# Foxglove Debug Protocol v1

The debug stream uses USART1 at 115200 baud, 8 data bits, no parity, and one
stop bit. Every integer and IEEE-754 single-precision floating-point value is
little-endian. Each frame is exactly 104 bytes.

| 偏移 | 长度 | 字段 | 编码 |
| ---: | ---: | --- | --- |
| 0..1 | 2 | `sync` | 固定 `0xD3 0x91`，不参与 CRC |
| 2 | 1 | `version` | 固定 `1` |
| 3 | 1 | `type` | 固定 `1`，表示 IMU snapshot |
| 4..5 | 2 | `payload_length` | 固定 `96` |
| 6..7 | 2 | `sequence` | 每次编码递增，`uint16_t` 自然回绕 |
| 8..11 | 4 | `timestamp_us` | `imu_snapshot_t.timestamp_us` |
| 12..13 | 2 | `status` | 完整 `uint16_t` 状态位掩码 |
| 14..25 | 12 | `euler_deg` | x、y、z，三个 `float32`，单位为度 |
| 26..37 | 12 | `gyro_dps` | x、y、z，三个 `float32`，单位为度/秒 |
| 38..49 | 12 | `accel_g` | x、y、z，三个 `float32`，单位为 g |
| 50..65 | 16 | `quaternion` | w、x、y、z，四个 `float32` |
| 66..101 | 36 | `diagnostics` | 九个 `uint32_t`，按结构体声明顺序 |
| 102..103 | 2 | `crc16` | CRC-16/CCITT-FALSE，小端序 |

`payload_length` counts from `sequence` through the end of diagnostics, for a
total of 96 bytes. CRC covers `frame[2..101]` and uses polynomial `0x1021`,
initial value `0xFFFF`, no input or output reflection, and XOR output `0x0000`.

Diagnostics are ordered as `accel_samples`, `gyro_samples`,
`accel_overruns`, `gyro_overruns`, `spi_errors`, `rejected_dt`, `long_gaps`,
`sensor_reinitializations`, and `telemetry_drops`.

The bridge treats a forward sequence discontinuity as a gap and counts the
missing frames. Sequence arithmetic is modulo 65536, so natural sequence
wraparound is valid. CRC, format, truncated, and unknown frames are discarded
without terminating the stream; parsing resumes at the next sync sequence.

The `/imu` custom JSON channel is authoritative. It contains `timestamp_us`,
`status`, `euler_deg`, `quaternion`, `gyro_dps`, `accel_g`, `gyro_rad_s`, and
`accel_m_s2`. The `/imu/diagnostics` custom JSON channel contains the nine
diagnostic counters plus bridge `sequence_gaps`, `crc_errors`,
`format_errors`, and `dropped_frames`. The custom JSON channels are used rather
than inventing a standard Foxglove IMU type because the Python SDK does not
provide a directly constructible `ImuChannel`.

Unit conversions are `gyro_dps * pi / 180` to radians per second and
`accel_g * 9.80665` to metres per second squared. Non-finite float values are
published as JSON `null`.
