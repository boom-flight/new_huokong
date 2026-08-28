#!/usr/bin/env python3
"""Dependency-free tests for the Foxglove debug bridge parser."""

from __future__ import annotations

import json
import math
import os
import shutil
import struct
import sys
import tempfile
import types
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.foxglove_debug_bridge import (  # noqa: E402
    FRAME_SIZE,
    FoxglovePublisher,
    SYNC,
    FrameParser,
    crc16_ccitt_false,
    decode_frame,
    make_imu_message,
    make_diagnostics_message,
    discover_serial_devices,
    open_serial_device,
    select_serial_device,
    timestamp_ns_for,
)


def make_frame(sequence: int = 10, timestamp_us: int = 1_000_000) -> bytes:
    frame = bytearray(FRAME_SIZE)
    frame[:8] = SYNC + struct.pack("<BBHH", 1, 1, 96, sequence)
    struct.pack_into("<I H", frame, 8, timestamp_us, 0xA5)
    struct.pack_into("<3f", frame, 14, 1.5, -2.25, 100.0)
    struct.pack_into("<3f", frame, 26, -0.5, 3.25, -10.0)
    struct.pack_into("<3f", frame, 38, 0.25, -1.5, 9.8)
    struct.pack_into("<4f", frame, 50, 1.0, -0.5, 0.75, -2.0)
    struct.pack_into("<9I", frame, 66, 1, 2, 3, 4, 5, 6, 7, 8, 9)
    struct.pack_into("<H", frame, 102, crc16_ccitt_false(bytes(frame[2:102])))
    return bytes(frame)


def test_decodes_protocol_fields_and_diagnostics() -> None:
    snapshot = decode_frame(make_frame())

    assert snapshot is not None
    assert snapshot["sequence"] == 10
    assert snapshot["timestamp_us"] == 1_000_000
    assert snapshot["status"] == 0xA5
    assert snapshot["euler_deg"] == [1.5, -2.25, 100.0]
    assert snapshot["gyro_dps"] == [-0.5, 3.25, -10.0]
    assert snapshot["accel_g"][:2] == [0.25, -1.5]
    assert math.isclose(snapshot["accel_g"][2], 9.8, rel_tol=1e-6)
    assert snapshot["quaternion"] == [1.0, -0.5, 0.75, -2.0]
    assert snapshot["diagnostics"] == {
        "accel_samples": 1,
        "gyro_samples": 2,
        "accel_overruns": 3,
        "gyro_overruns": 4,
        "spi_errors": 5,
        "rejected_dt": 6,
        "long_gaps": 7,
        "sensor_reinitializations": 8,
        "telemetry_drops": 9,
    }


def test_parser_handles_incremental_input_garbage_and_truncation() -> None:
    valid_frame = make_frame()
    parser = FrameParser()

    assert len(parser.feed(valid_frame)) == 1
    assert len(parser.feed(valid_frame[:7])) == 0
    assert len(parser.feed(valid_frame[7:])) == 1
    assert len(parser.feed(b"garbage" + valid_frame)) == 1
    assert len(parser.feed(bytes(valid_frame[:-1]))) == 0
    assert parser.dropped_frames == 0


def test_parser_recovers_after_each_format_or_crc_corruption() -> None:
    for offset, value in ((2, 2), (3, 2), (4, 95)):
        corrupted = bytearray(make_frame(sequence=20))
        corrupted[offset] = value
        parser = FrameParser()
        assert parser.feed(bytes(corrupted) + make_frame(sequence=21)) == [
            decode_frame(make_frame(sequence=21))
        ]
        assert parser.format_errors == 1
        assert parser.crc_errors == 0

    corrupted = bytearray(make_frame(sequence=30))
    corrupted[30] ^= 0x01
    parser = FrameParser()
    assert parser.feed(bytes(corrupted) + make_frame(sequence=31)) == [
        decode_frame(make_frame(sequence=31))
    ]
    assert parser.crc_errors == 1
    assert parser.format_errors == 0


def test_sequence_gap_counts_missing_frames_not_crc_failures() -> None:
    parser = FrameParser()

    assert len(parser.feed(make_frame(sequence=10))) == 1
    assert len(parser.feed(make_frame(sequence=12))) == 1
    assert parser.sequence_gaps == 1
    assert parser.dropped_frames == 1

    corrupted = bytearray(make_frame(sequence=13))
    corrupted[40] ^= 0x80
    assert len(parser.feed(bytes(corrupted))) == 0
    assert parser.sequence_gaps == 1
    assert parser.dropped_frames == 1
    assert parser.crc_errors == 1


def test_nonfinite_float_values_decode_as_none() -> None:
    frame = bytearray(make_frame())
    struct.pack_into("<f", frame, 14, math.nan)
    struct.pack_into("<f", frame, 30, math.inf)
    struct.pack_into("<f", frame, 42, -math.inf)
    struct.pack_into("<H", frame, 102, crc16_ccitt_false(bytes(frame[2:102])))

    snapshot = decode_frame(bytes(frame))

    assert snapshot is not None
    assert snapshot["euler_deg"][0] is None
    assert snapshot["gyro_dps"][1] is None
    assert snapshot["accel_g"][1] is None


def test_unit_conversion_and_timestamp_alignment() -> None:
    snapshot = decode_frame(make_frame())
    assert snapshot is not None
    message = make_imu_message(snapshot, 4_000_000_000)

    assert message["timestamp_us"] == 1_000_000
    assert math.isclose(message["gyro_rad_s"][0], -0.5 * math.pi / 180.0)
    assert math.isclose(message["accel_m_s2"][0], 0.25 * 9.80665)
    assert timestamp_ns_for(0x00000010, 0xFFFFFFF0, 10_000) == 42_000
    assert timestamp_ns_for(25, 20, 10_000) == 15_000


def test_published_messages_have_explicit_json_keys() -> None:
    snapshot = decode_frame(make_frame())
    assert snapshot is not None
    imu = make_imu_message(snapshot, 2_000)
    diagnostics = make_diagnostics_message(snapshot, sequence_gaps=1, crc_errors=2,
                                            format_errors=3, dropped_frames=4)

    assert set(json.loads(json.dumps(imu))) == {
        "timestamp_us", "status", "euler_deg", "quaternion", "gyro_dps",
        "accel_g", "gyro_rad_s", "accel_m_s2",
    }
    assert set(json.loads(json.dumps(diagnostics))) == {
        "timestamp_us",
        "accel_samples", "gyro_samples", "accel_overruns", "gyro_overruns",
        "spi_errors", "rejected_dt", "long_gaps", "sensor_reinitializations",
        "telemetry_drops", "sequence_gaps", "crc_errors", "format_errors",
        "dropped_frames",
    }


def test_publisher_stops_server_when_channel_creation_fails() -> None:
    class Server:
        stopped = False

        def stop(self) -> None:
            self.stopped = True

    server = Server()

    def start_server(*, host: str, port: int) -> Server:
        assert host == "127.0.0.1"
        assert port == 8765
        return server

    def channel(*args: object, **kwargs: object) -> object:
        raise RuntimeError("channel failure")

    fake_foxglove = types.SimpleNamespace(
        start_server=start_server,
        Channel=channel,
    )
    previous = sys.modules.get("foxglove")
    sys.modules["foxglove"] = fake_foxglove
    try:
        try:
            FoxglovePublisher("127.0.0.1", 8765)
        except RuntimeError as error:
            assert str(error) == "channel failure"
        else:
            raise AssertionError("channel creation unexpectedly succeeded")
    finally:
        if previous is None:
            del sys.modules["foxglove"]
        else:
            sys.modules["foxglove"] = previous
    assert server.stopped


def test_publisher_closes_created_channels_when_channel_creation_is_partial() -> None:
    class Server:
        stopped = False

        def stop(self) -> None:
            self.stopped = True

    class Channel:
        def __init__(self) -> None:
            self.close_calls = 0

        def close(self) -> None:
            self.close_calls += 1

    server = Server()
    first_channel = Channel()
    channel_calls = 0

    def start_server(*, host: str, port: int) -> Server:
        return server

    def channel(*args: object, **kwargs: object) -> Channel:
        nonlocal channel_calls
        channel_calls += 1
        if channel_calls == 1:
            return first_channel
        raise RuntimeError("second channel failure")

    fake_foxglove = types.SimpleNamespace(start_server=start_server, Channel=channel)
    previous = sys.modules.get("foxglove")
    sys.modules["foxglove"] = fake_foxglove
    try:
        try:
            FoxglovePublisher("127.0.0.1", 8765)
        except RuntimeError as error:
            assert str(error) == "second channel failure"
        else:
            raise AssertionError("partial channel creation unexpectedly succeeded")
    finally:
        if previous is None:
            del sys.modules["foxglove"]
        else:
            sys.modules["foxglove"] = previous
    assert first_channel.close_calls == 1
    assert server.stopped


def test_publisher_stops_server_when_channel_close_raises() -> None:
    class Server:
        stopped = False

        def stop(self) -> None:
            self.stopped = True

    class Channel:
        def __init__(self, should_raise: bool) -> None:
            self.should_raise = should_raise
            self.close_calls = 0

        def close(self) -> None:
            self.close_calls += 1
            if self.should_raise:
                raise RuntimeError("channel close failure")

    server = Server()
    channels = [Channel(True), Channel(False)]
    channel_index = 0

    def start_server(*, host: str, port: int) -> Server:
        return server

    def channel(*args: object, **kwargs: object) -> Channel:
        nonlocal channel_index
        result = channels[channel_index]
        channel_index += 1
        return result

    fake_foxglove = types.SimpleNamespace(start_server=start_server, Channel=channel)
    previous = sys.modules.get("foxglove")
    sys.modules["foxglove"] = fake_foxglove
    try:
        publisher = FoxglovePublisher("127.0.0.1", 8765)
        try:
            publisher.close()
        except RuntimeError as error:
            assert str(error) == "channel close failure"
        else:
            raise AssertionError("channel close unexpectedly succeeded")
    finally:
        if previous is None:
            del sys.modules["foxglove"]
        else:
            sys.modules["foxglove"] = previous
    assert server.stopped
    assert [channel.close_calls for channel in channels] == [1, 1]


def test_main_closes_publisher_when_serial_close_raises() -> None:
    import tools.foxglove_debug_bridge as bridge

    class SerialDevice:
        def read(self, size: int) -> bytes:
            raise KeyboardInterrupt

        def close(self) -> None:
            raise RuntimeError("serial close failure")

    class Publisher:
        def __init__(self, host: str, port: int) -> None:
            self.closed = False

        def close(self) -> None:
            self.closed = True

    serial_device = SerialDevice()
    publisher_instances: list[Publisher] = []

    class SerialModule:
        def Serial(self, *args: object, **kwargs: object) -> SerialDevice:
            return serial_device

    def make_publisher(host: str, port: int) -> Publisher:
        publisher = Publisher(host, port)
        publisher_instances.append(publisher)
        return publisher

    previous_serial = sys.modules.get("serial")
    previous_publisher = bridge.FoxglovePublisher
    sys.modules["serial"] = SerialModule()
    bridge.FoxglovePublisher = make_publisher  # type: ignore[assignment]
    try:
        try:
            bridge.main(["--device", "/dev/fake"])
        except RuntimeError as error:
            assert str(error) == "serial close failure"
        else:
            raise AssertionError("serial close unexpectedly succeeded")
    finally:
        bridge.FoxglovePublisher = previous_publisher
        if previous_serial is None:
            del sys.modules["serial"]
        else:
            sys.modules["serial"] = previous_serial
    assert len(publisher_instances) == 1
    assert publisher_instances[0].closed


def test_main_reports_missing_foxglove_sdk() -> None:
    import tools.foxglove_debug_bridge as bridge

    class SerialDevice:
        def close(self) -> None:
            pass

    class SerialModule:
        def Serial(self, *args: object, **kwargs: object) -> SerialDevice:
            return SerialDevice()

    def missing_publisher(host: str, port: int) -> Any:
        raise ModuleNotFoundError("No module named 'foxglove'")

    previous_serial = sys.modules.get("serial")
    previous_publisher = bridge.FoxglovePublisher
    sys.modules["serial"] = SerialModule()
    bridge.FoxglovePublisher = missing_publisher  # type: ignore[assignment]
    try:
        assert bridge.main(["--device", "/dev/fake"]) == 2
    finally:
        bridge.FoxglovePublisher = previous_publisher
        if previous_serial is None:
            del sys.modules["serial"]
        else:
            sys.modules["serial"] = previous_serial


def test_auto_device_selection_uses_the_only_serial_device() -> None:
    class SerialDevice:
        def __init__(self, device: str, **kwargs: object) -> None:
            self.device = device
            self.kwargs = kwargs

    class SerialModule:
        def Serial(self, device: str, **kwargs: object) -> SerialDevice:
            return SerialDevice(device, **kwargs)

    serial_module = SerialModule()
    device = open_serial_device(
        serial_module, "auto", 115200, ["/dev/serial/by-id/usb-debug"]
    )

    assert device.device == "/dev/serial/by-id/usb-debug"
    assert device.kwargs == {"baudrate": 115200, "timeout": 1}


def test_auto_device_selection_probes_multiple_devices_for_valid_frame() -> None:
    valid_frame = make_frame()

    class SerialDevice:
        def __init__(self, device: str, **kwargs: object) -> None:
            self.device = device
            self.kwargs = kwargs
            self.closed = False
            self.read_once = False

        def read(self, size: int) -> bytes:
            assert size == FRAME_SIZE * 4
            if self.device.endswith("ttyUSB1") and not self.read_once:
                self.read_once = True
                return valid_frame
            return b"not a Foxglove frame"

        def close(self) -> None:
            self.closed = True

    class SerialModule:
        def __init__(self) -> None:
            self.instances: list[SerialDevice] = []

        def Serial(self, device: str, **kwargs: object) -> SerialDevice:
            result = SerialDevice(device, **kwargs)
            self.instances.append(result)
            return result

    serial_module = SerialModule()
    device = open_serial_device(
        serial_module, "auto", 115200, ["/dev/ttyUSB0", "/dev/ttyUSB1"]
    )

    assert device.device == "/dev/ttyUSB1"
    assert device.kwargs == {"baudrate": 115200, "timeout": 1}
    assert serial_module.instances[0].closed
    assert serial_module.instances[1].closed


def test_auto_device_selection_rejects_ambiguous_serial_devices() -> None:
    class SerialDevice:
        def __init__(self, device: str, **kwargs: object) -> None:
            self.device = device

        def read(self, size: int) -> bytes:
            return b""

        def close(self) -> None:
            pass

    class SerialModule:
        def Serial(self, device: str, **kwargs: object) -> SerialDevice:
            return SerialDevice(device, **kwargs)

    try:
        select_serial_device(
            SerialModule(), 115200, ["/dev/ttyUSB0", "/dev/ttyUSB1"]
        )
    except RuntimeError as error:
        assert "could not identify exactly one Foxglove device" in str(error)
    else:
        raise AssertionError("ambiguous serial devices were accepted")


def test_auto_device_selection_rejects_two_valid_devices() -> None:
    class SerialDevice:
        def __init__(self, device: str, **kwargs: object) -> None:
            self.device = device

        def read(self, size: int) -> bytes:
            return make_frame()

        def close(self) -> None:
            pass

    class SerialModule:
        def Serial(self, device: str, **kwargs: object) -> SerialDevice:
            return SerialDevice(device, **kwargs)

    try:
        select_serial_device(
            SerialModule(), 115200, ["/dev/ttyUSB0", "/dev/ttyUSB1"]
        )
    except RuntimeError as error:
        assert "could not identify exactly one Foxglove device" in str(error)
    else:
        raise AssertionError("multiple valid serial devices were accepted")


def test_auto_device_selection_ignores_probe_close_failure() -> None:
    class SerialDevice:
        def __init__(self, device: str, **kwargs: object) -> None:
            self.device = device

        def read(self, size: int) -> bytes:
            return make_frame() if self.device.endswith("ttyUSB0") else b""

        def close(self) -> None:
            raise RuntimeError("probe close failure")

    class SerialModule:
        def Serial(self, device: str, **kwargs: object) -> SerialDevice:
            return SerialDevice(device, **kwargs)

    device = open_serial_device(
        SerialModule(), "auto", 115200, ["/dev/ttyUSB0", "/dev/ttyUSB1"]
    )
    assert device.device == "/dev/ttyUSB0"


def test_discovery_falls_back_after_stale_stable_link() -> None:
    root = Path(tempfile.mkdtemp(prefix="foxglove-serial-"))
    by_id = root / "by-id"
    by_id.mkdir()
    os.symlink("/dev/does-not-exist", by_id / "stale")
    try:
        assert discover_serial_devices(
            by_id, ["/dev/ttyUSB1"]
        ) == ["/dev/ttyUSB1"]
    finally:
        shutil.rmtree(root)


def run_tests() -> None:
    tests = [value for name, value in globals().items()
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"{len(tests)} tests passed")


if __name__ == "__main__":
    run_tests()
