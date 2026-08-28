#!/usr/bin/env python3
"""Decode Foxglove BMI088 debug frames and publish them to Foxglove."""

from __future__ import annotations

import argparse
import math
import struct
import time
from typing import Any

FRAME_SIZE = 104
SYNC = b"\xd3\x91"
RAD_PER_DEG = 3.141592653589793 / 180.0
M_S2_PER_G = 9.80665
TIMESTAMP_MASK = 0xFFFFFFFF

_DIAGNOSTIC_NAMES = (
    "accel_samples",
    "gyro_samples",
    "accel_overruns",
    "gyro_overruns",
    "spi_errors",
    "rejected_dt",
    "long_gaps",
    "sensor_reinitializations",
    "telemetry_drops",
)


def crc16_ccitt_false(data: bytes) -> int:
    """Return CRC-16/CCITT-FALSE for *data*."""
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _finite_float(value: float) -> float | None:
    return value if math.isfinite(value) else None


def _float_values(frame: bytes, offset: int, count: int) -> list[float | None]:
    return [_finite_float(value) for value in struct.unpack_from(f"<{count}f", frame, offset)]


def decode_frame(frame: bytes) -> dict[str, Any] | None:
    """Decode one validated 104-byte frame, or return ``None`` if invalid."""
    if len(frame) != FRAME_SIZE or frame[:2] != SYNC:
        return None
    if frame[2] != 1 or frame[3] != 1 or struct.unpack_from("<H", frame, 4)[0] != 96:
        return None
    expected_crc = struct.unpack_from("<H", frame, 102)[0]
    if crc16_ccitt_false(frame[2:102]) != expected_crc:
        return None

    diagnostics = struct.unpack_from("<9I", frame, 66)
    return {
        "sequence": struct.unpack_from("<H", frame, 6)[0],
        "timestamp_us": struct.unpack_from("<I", frame, 8)[0],
        "status": struct.unpack_from("<H", frame, 12)[0],
        "euler_deg": _float_values(frame, 14, 3),
        "gyro_dps": _float_values(frame, 26, 3),
        "accel_g": _float_values(frame, 38, 3),
        "quaternion": _float_values(frame, 50, 4),
        "diagnostics": dict(zip(_DIAGNOSTIC_NAMES, diagnostics)),
    }


class FrameParser:
    """Incrementally find, validate, decode, and account for debug frames."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self._last_sequence: int | None = None
        self.sequence_gaps = 0
        self.crc_errors = 0
        self.format_errors = 0
        self.dropped_frames = 0

    def feed(self, data: bytes) -> list[dict[str, Any]]:
        self._buffer.extend(data)
        frames: list[dict[str, Any]] = []

        while True:
            sync_offset = self._buffer.find(SYNC)
            if sync_offset < 0:
                # Keep a possible first sync byte split across reads.
                if self._buffer[-1:] == SYNC[:1]:
                    del self._buffer[:-1]
                else:
                    self._buffer.clear()
                break
            if sync_offset:
                del self._buffer[:sync_offset]
            if len(self._buffer) < FRAME_SIZE:
                break

            candidate = bytes(self._buffer[:FRAME_SIZE])
            if candidate[2] != 1 or candidate[3] != 1 or struct.unpack_from("<H", candidate, 4)[0] != 96:
                self.format_errors += 1
                del self._buffer[0]
                continue
            if crc16_ccitt_false(candidate[2:102]) != struct.unpack_from("<H", candidate, 102)[0]:
                self.crc_errors += 1
                del self._buffer[0]
                continue

            snapshot = decode_frame(candidate)
            if snapshot is None:
                self.format_errors += 1
                del self._buffer[0]
                continue

            sequence = snapshot["sequence"]
            if self._last_sequence is not None:
                missing = (
                    (sequence - self._last_sequence - 1) & 0xFFFF
                    if sequence != self._last_sequence
                    else 0
                )
                if missing:
                    self.sequence_gaps += 1
                    self.dropped_frames += missing
            self._last_sequence = sequence
            frames.append(snapshot)
            del self._buffer[:FRAME_SIZE]

        return frames


def timestamp_ns_for(timestamp_us: int, first_timestamp_us: int,
                     first_host_time_ns: int) -> int:
    """Map a uint32 MCU timestamp to the host timeline."""
    delta_us = (timestamp_us - first_timestamp_us) & TIMESTAMP_MASK
    return first_host_time_ns + delta_us * 1000


class TimestampAligner:
    """Align MCU timestamps to host nanoseconds using the first valid frame."""

    def __init__(self) -> None:
        self.first_timestamp_us: int | None = None
        self.first_host_time_ns: int | None = None

    def timestamp_ns(self, timestamp_us: int, host_time_ns: int | None = None) -> int:
        if self.first_timestamp_us is None:
            self.first_timestamp_us = timestamp_us
            self.first_host_time_ns = time.time_ns() if host_time_ns is None else host_time_ns
        assert self.first_host_time_ns is not None
        return timestamp_ns_for(timestamp_us, self.first_timestamp_us, self.first_host_time_ns)


def _convert(values: list[float | None], scale: float) -> list[float | None]:
    return [None if value is None else value * scale for value in values]


def make_imu_message(snapshot: dict[str, Any], log_time_ns: int) -> dict[str, Any]:
    """Build the JSON message for the ``/imu`` channel."""
    return {
        "timestamp_us": snapshot["timestamp_us"],
        "status": snapshot["status"],
        "euler_deg": snapshot["euler_deg"],
        "quaternion": snapshot["quaternion"],
        "gyro_dps": snapshot["gyro_dps"],
        "accel_g": snapshot["accel_g"],
        "gyro_rad_s": _convert(snapshot["gyro_dps"], RAD_PER_DEG),
        "accel_m_s2": _convert(snapshot["accel_g"], M_S2_PER_G),
    }


def make_diagnostics_message(snapshot: dict[str, Any], sequence_gaps: int,
                             crc_errors: int, format_errors: int,
                             dropped_frames: int) -> dict[str, int]:
    """Build the JSON message for the ``/imu/diagnostics`` channel."""
    return {
        "timestamp_us": snapshot["timestamp_us"],
        **snapshot["diagnostics"],
        "sequence_gaps": sequence_gaps,
        "crc_errors": crc_errors,
        "format_errors": format_errors,
        "dropped_frames": dropped_frames,
    }


def _close_channels(channels: list[Any]) -> None:
    first_error: BaseException | None = None
    for channel in channels:
        try:
            channel.close()
        except BaseException as error:
            if first_error is None:
                first_error = error
    if first_error is not None:
        raise first_error


class FoxglovePublisher:
    """Publish decoded snapshots through the optional Foxglove SDK."""

    def __init__(self, host: str, port: int) -> None:
        import foxglove

        self._foxglove = foxglove
        self._server = foxglove.start_server(host=host, port=port)
        created_channels: list[Any] = []
        try:
            self._imu_channel = foxglove.Channel("/imu", message_encoding="json")
            created_channels.append(self._imu_channel)
            self._diagnostics_channel = foxglove.Channel(
                "/imu/diagnostics", message_encoding="json"
            )
            created_channels.append(self._diagnostics_channel)
        except Exception:
            try:
                _close_channels(created_channels)
            finally:
                self._server.stop()
            raise
        self.sequence_gaps = 0
        self.crc_errors = 0
        self.format_errors = 0

    def publish(self, snapshot: dict[str, Any], log_time_ns: int, dropped: int) -> None:
        self._imu_channel.log(make_imu_message(snapshot, log_time_ns), log_time=log_time_ns)
        self._diagnostics_channel.log(
            make_diagnostics_message(
                snapshot,
                self.sequence_gaps,
                self.crc_errors,
                self.format_errors,
                dropped,
            ),
            log_time=log_time_ns,
        )

    def close(self) -> None:
        try:
            _close_channels([self._imu_channel, self._diagnostics_channel])
        finally:
            self._server.stop()


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True, help="serial device path")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_argument_parser().parse_args(argv)
    import serial

    serial_device = serial.Serial(args.device, baudrate=args.baudrate, timeout=1)
    publisher: FoxglovePublisher | None = None
    try:
        publisher = FoxglovePublisher(args.host, args.port)
        parser = FrameParser()
        aligner = TimestampAligner()
        while True:
            data = serial_device.read(FRAME_SIZE * 4)
            for snapshot in parser.feed(data):
                log_time_ns = aligner.timestamp_ns(snapshot["timestamp_us"])
                publisher.sequence_gaps = parser.sequence_gaps
                publisher.crc_errors = parser.crc_errors
                publisher.format_errors = parser.format_errors
                publisher.publish(snapshot, log_time_ns, parser.dropped_frames)
    except KeyboardInterrupt:
        return 0
    finally:
        try:
            serial_device.close()
        finally:
            if publisher is not None:
                publisher.close()


if __name__ == "__main__":
    raise SystemExit(main())
