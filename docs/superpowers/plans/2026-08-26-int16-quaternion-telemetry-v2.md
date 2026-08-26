# INT16 Quaternion Telemetry v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the USART2 IMU telemetry stream from the 32-byte v1 frame to a 40-byte v2 frame that publishes the Mahony quaternion as four int16 fixed-point components while preserving the existing Euler, gyro, and acceleration fields.

**Architecture:** Keep the existing IMU snapshot and Mahony data flow unchanged. Extend the platform-independent telemetry sample and encoder with `w/x/y/z` quaternion fields, then update the telemetry service and UART transport to use the 40-byte frame. Treat the protocol version change as explicit and update the protocol documentation, host tests, and the missing firmware logging build integration needed for a successful link.

**Tech Stack:** C11, STM32F103C8, RT-Thread, STM32 HAL, SCons, host-side `assert` tests.

**Spec:** `docs/superpowers/specs/2026-08-26-int16-quaternion-telemetry-v2-design.md`

## Global Constraints

- Frame size is fixed at 40 bytes.
- Protocol version is `2`; `payload_length` is `34` (`0x22`).
- Quaternion order is `w, x, y, z`.
- Quaternion encoding is `round(component * 32767)`, saturated to `INT16_MIN..INT16_MAX`; non-finite values encode as zero.
- CRC-16/CCITT-FALSE covers `frame[2..37]` and is stored at `frame[38..39]` little-endian.
- Existing Euler fields retain `roll, pitch, yaw` semantics at offsets `12..17`.
- Existing 200 Hz schedule, 115200 8N1 UART configuration, DMA double buffering, sequence behavior, status bits, and drop handling remain unchanged.
- Quaternion data is usable only when `IMU_STATUS_VALID` is set; the receiver re-normalizes decoded quaternions.
- Do not add USART2 RX, v1/v2 runtime negotiation, mixed v1/v2 output, absolute yaw, or unrelated refactoring.

---

### Task 1: Lock the v2 Encoder Contract With Host Tests

**Files:**
- Modify: `tests/modules/protocols/imu_telemetry/test_imu_telemetry.c`
- Reference: `src/modules/protocols/imu_telemetry/imu_telemetry.h`
- Reference: `src/modules/protocols/imu_telemetry/imu_telemetry.c`

**Interfaces:**
- Consumes the existing `imu_telemetry_encode(uint16_t sequence, const imu_telemetry_sample_t *sample, uint8_t frame[IMU_TELEMETRY_FRAME_SIZE])` interface.
- Requires `imu_telemetry_sample_t` to expose `imu_quatf_t quaternion` before the implementation test can compile.
- Produces exact v2 expectations for later encoder implementation: 40-byte frame, version `2`, payload length `34`, quaternion offsets `30..37`, and CRC at `38..39`.

- [ ] **Step 1: Update test constants and sample data to describe v2.**

Change the test's frame-size assertions from 32 to 40 and add a quaternion to the golden sample:

```c
.quaternion = {0.5f, -0.5f, 0.70710678f, 0.0f},
```

The expected 40-byte golden frame for the existing sample values is:

```c
static const uint8_t expected[IMU_TELEMETRY_FRAME_SIZE] = {
    0xA5u, 0x5Au, 0x02u, 0x22u, 0xCDu, 0xABu, 0x12u, 0x34u,
    0x56u, 0x78u, 0xFFu, 0x01u, 0x2Du, 0xBAu, 0xD3u, 0x45u,
    0xD3u, 0x04u, 0xFFu, 0x7Fu, 0x00u, 0x80u, 0x0Du, 0x00u,
    0xFFu, 0x7Fu, 0x00u, 0x80u, 0xE9u, 0x03u, 0x00u, 0x40u,
    0x00u, 0xC0u, 0x82u, 0x5Au, 0x00u, 0x00u, 0x3Fu, 0x89u,
};
```

- [ ] **Step 2: Add explicit quaternion field assertions.**

After the existing scalar assertions, assert the signed little-endian values:

```c
assert((int16_t)test_le16(&frame[30]) == 16384);
assert((int16_t)test_le16(&frame[32]) == -16384);
assert((int16_t)test_le16(&frame[34]) == 23170);
assert((int16_t)test_le16(&frame[36]) == 0);
    assert(test_le16(&frame[38]) == 0x893Fu);
```

- [ ] **Step 3: Add fixed-point boundary and non-finite coverage.**

Extend the boundary test with `{1.0f, -1.0f, 0.0f, 0.70710678f}` and verify `32767`, `-32767`, `0`, and `23170`. Extend the non-finite test with all four quaternion components set to `NAN` or infinities and verify offsets `30`, `32`, `34`, and `36` are zero.

- [ ] **Step 4: Change CRC coverage assertions to v2.**

Use the zero sample to assert the v2 CRC, mutate `frame[37]` and verify the CRC changes, then mutate `frame[1]` and verify the CRC calculation over `frame[2..37]` is unchanged. Also encode a second sample whose only changed encoded quaternion byte is `frame[37]` to prove the encoder includes that final payload byte:

```c
assert(test_le16(&frame[38]) == 0x3AF1u);
frame[37] ^= 0x01u;
assert(imu_telemetry_crc16_ccitt_false(&frame[2], 36u) !=
       test_le16(&frame[38]));

const imu_telemetry_sample_t changed = {
    .quaternion = {0.0f, 0.0f, 0.0f, 256.0f / 32767.0f},
};
uint8_t changed_frame[IMU_TELEMETRY_FRAME_SIZE] = {0};
imu_telemetry_encode(0u, &changed, changed_frame);
assert(changed_frame[36] == 0u && changed_frame[37] == 1u);
assert(test_le16(&changed_frame[38]) != test_le16(&frame[38]));
```

The zero-sample v2 CRC is `0x3AF1`; store it little-endian as `0xF1 0x3A`. Do not retain the v1 `0x4FB6` expectation.

- [ ] **Step 5: Run the focused test before implementation.**

Run: `scons -f tests/SConstruct -Q build/host-tests/test_imu_telemetry`

Expected: FAIL because the v2 constants and quaternion member are not implemented yet. This confirms the test exercises the requested change rather than only the old contract.

### Task 2: Implement the Platform-Independent v2 Encoder

**Files:**
- Modify: `src/modules/protocols/imu_telemetry/imu_telemetry.h:9-23`
- Modify: `src/modules/protocols/imu_telemetry/imu_telemetry.c:32-84`
- Test: `tests/modules/protocols/imu_telemetry/test_imu_telemetry.c`

**Interfaces:**
- Produces `IMU_TELEMETRY_FRAME_SIZE == 40u` and `IMU_TELEMETRY_VERSION == 2u`.
- Extends `imu_telemetry_sample_t` with `imu_quatf_t quaternion`.
- Keeps `imu_telemetry_encode()` and `imu_telemetry_crc16_ccitt_false()` signatures unchanged.

- [ ] **Step 1: Change protocol constants and sample structure.**

Update the header as follows:

```c
#define IMU_TELEMETRY_FRAME_SIZE 40u
#define IMU_TELEMETRY_VERSION 2u

typedef struct {
    uint32_t timestamp_us;
    uint16_t status;
    imu_vec3f_t euler_deg;
    imu_vec3f_t gyro_dps;
    imu_vec3f_t accel_g;
    imu_quatf_t quaternion;
} imu_telemetry_sample_t;
```

- [ ] **Step 2: Reuse the existing signed saturating encoder for quaternion components.**

Use `encode_i16(sample->quaternion.w, 32767.0f)` and the same helper for `x`, `y`, and `z`. This preserves finite-value handling, nearest-integer rounding, and saturation behavior without adding a second conversion path.

- [ ] **Step 3: Encode the v2 header and quaternion fields.**

Keep fields through `frame[29]` unchanged, except for the version and payload length bytes. Add:

```c
put_u16(&frame[30], (uint16_t)encode_i16(sample->quaternion.w, 32767.0f));
put_u16(&frame[32], (uint16_t)encode_i16(sample->quaternion.x, 32767.0f));
put_u16(&frame[34], (uint16_t)encode_i16(sample->quaternion.y, 32767.0f));
put_u16(&frame[36], (uint16_t)encode_i16(sample->quaternion.z, 32767.0f));
put_u16(&frame[38], imu_telemetry_crc16_ccitt_false(&frame[2], 36u));
```

Remove the old CRC write at offsets `30..31`.

- [ ] **Step 4: Run the focused encoder test.**

Run: `scons -f tests/SConstruct -Q build/host-tests/test_imu_telemetry && build/host-tests/test_imu_telemetry`

Expected: PASS, including the v2 golden frame, fixed-point boundaries, non-finite handling, and CRC coverage.

### Task 3: Thread Quaternion Data Through Telemetry and DMA

**Files:**
- Modify: `src/kernel/telemetry/telemetry_service.c:16,57-71`
- Modify: `src/platform/transport/telemetry_uart_stm32.c:8-10,115-128`
- Test: `tests/modules/protocols/imu_telemetry/test_imu_telemetry.c`

**Interfaces:**
- Consumes `imu_snapshot_t.quaternion` without changing `imu_snapshot_t` or Mahony APIs.
- Produces a 40-byte `imu_telemetry_encode()` input and a 40-byte `telemetry_uart_stm32_try_start()` request.
- Preserves the existing two-buffer ownership rule: switch `next_buffer` only after DMA accepts the frame.

- [ ] **Step 1: Copy the snapshot quaternion into the telemetry sample.**

In the sample initializer in `telemetry_thread_entry()`, add:

```c
.quaternion = snapshot.quaternion,
```

Do not recompute or normalize the quaternion in the telemetry thread; the snapshot is the single published value and the receiver performs final normalization.

- [ ] **Step 2: Change the transport frame-size guard.**

Change the local `TELEMETRY_FRAME_SIZE` in `telemetry_uart_stm32.c` from `32u` to `40u`. Keep the `length != TELEMETRY_FRAME_SIZE` guard and the existing `uint16_t` HAL length cast.

- [ ] **Step 3: Audit all frame-size uses.**

Confirm that the telemetry buffers already use `IMU_TELEMETRY_FRAME_SIZE` and that the only platform-local constant now agrees with it. Search for both `32u` and `IMU_TELEMETRY_FRAME_SIZE` under `src/` and remove only telemetry-frame assumptions; do not change unrelated 32-byte constants.

- [ ] **Step 4: Run host tests and inspect the resulting frame length.**

Run: `./tools/test.sh`

Expected: exit 0, all existing host tests pass, and the telemetry encoder test reports no frame-size or CRC assertion failures.

### Task 4: Update Protocol and Behavior Documentation

**Files:**
- Modify: `docs/protocols/imu-telemetry-v1.md` -> rename to `docs/protocols/imu-telemetry-v2.md`
- Modify: `README.md:3,24,32`
- Modify: `docs/requirements/firmware-behavior.md:83-87`
- Reference: `docs/superpowers/specs/2026-08-26-int16-quaternion-telemetry-v2-design.md`

**Interfaces:**
- Documents the external wire contract implemented by Tasks 2 and 3.
- Produces no new runtime API; the receiver-facing protocol document becomes the source of truth for v2.

- [ ] **Step 1: Rename the protocol document and update its title.**

Rename `docs/protocols/imu-telemetry-v1.md` to `docs/protocols/imu-telemetry-v2.md`, change the title to `IMU 遥测协议 v2`, and update the transport statement to fixed 40 bytes, version `2`, payload length `34`, and 200 Hz.

- [ ] **Step 2: Replace the byte-layout table.**

Use the exact offsets from the design spec: quaternion `w/x/y/z` at `30..37`, CRC at `38..39`, and CRC coverage `frame[2..37]` for 36 bytes. Document `q * 32767`, little-endian signed integers, non-finite zero encoding, and receiver re-normalization.

- [ ] **Step 3: Preserve and update scheduling/error semantics.**

Retain the existing sequence, drop-sticky, DMA double-buffer, status-bit, and 200 Hz rules. Change only frame size, version, payload length, and CRC coverage. Explicitly state that v1 receivers cannot parse the v2 stream.

- [ ] **Step 4: Update repository references.**

Change README links and wording from the fixed 32-byte v1 frame to the fixed 40-byte v2 frame. Update the firmware behavior section to state that telemetry encodes and sends the quaternion in addition to Euler, gyro, and acceleration data. Keep the `roll, pitch, yaw` field semantics and relative-yaw limitation explicit.

- [ ] **Step 5: Check for stale v1 references.**

Run: `rg -n "imu-telemetry-v1|32 字节|VERSION 1|payload length.*26|frame\[2\.\.29\]" README.md docs src tests`

Expected: no stale active-protocol references remain outside historical design/archive material; update any active reference found before proceeding.

### Task 5: Restore the Firmware Logging Build Integration

**Files:**
- Modify: `SConscript:9-24`
- Modify: `src/kernel/logging/SConscript:4-13`

**Interfaces:**
- Consumes the existing `IMU Logging` group that defines `imu_log_submit()` and `imu_log_service_init()`.
- Produces linkable firmware objects without changing logging behavior.

- [ ] **Step 1: Add the logging SConscript to the root script list.**

Insert the existing script in the kernel portion of the list, for example immediately after `src/kernel/imu/SConscript`:

```python
'src/kernel/imu/SConscript',
'src/kernel/logging/SConscript',
'src/app/SConscript',
```

Do not duplicate the script or modify the logging source files.

- [ ] **Step 2: Add the transitive BMI088 include path required by the logging event header.**

In `src/kernel/logging/SConscript`, define the device module root alongside the existing kernel and module roots, then include it in `CPPPATH`:

```python
device_root = Dir('#src/modules/devices').abspath

group = DefineGroup(
    'IMU Logging',
    ['imu_log_event.c', 'imu_log_service.c'],
    depend=[''],
    CPPPATH=[kernel_root, module_root, device_root],
    LOCAL_CCFLAGS=' -std=c11 -Wall -Wextra -Werror',
)
```

This is required because `imu_log_event.h` includes `imu_policy.h`, which includes `bmi088/bmi088.h` from `src/modules/devices/bmi088`.

- [ ] **Step 3: Build the firmware.**

Run: `./tools/build.sh`

Expected: link succeeds, `build/scons/firmware/huokong.elf` and `.bin` are produced, and Flash/SRAM size checks pass. The prior undefined references to `imu_log_submit` and `imu_log_service_init`, and the missing `bmi088/bmi088.h` error, must be absent.

### Task 6: Complete End-to-End Verification and Hardware Acceptance

**Files:**
- Test: `tests/modules/protocols/imu_telemetry/test_imu_telemetry.c`
- Verify: `build/firmware/huokong.elf`
- Verify: `docs/hardware/acceptance.md`

**Interfaces:**
- Consumes the v2 frame contract and built firmware from Tasks 1-5.
- Produces test evidence for host behavior, firmware linking, UART framing, and on-board quaternion validity.

- [ ] **Step 1: Run the complete host test suite.**

Run: `./tools/test.sh`

Expected: exit 0, no assertion failures, no compiler warnings promoted to errors, and the size/ownership checks included by the script pass.

- [ ] **Step 2: Run the complete firmware build.**

Run: `./tools/build.sh`

Expected: exit 0 with successful link and Flash/SRAM gates.

- [ ] **Step 3: Verify the wire stream off-board.**

Connect a 3.3 V USB-UART receiver to the USART2 TX pin on the U15 connector and capture at 115200 8N1. For every candidate frame, require:

```text
frame[0..1] == A5 5A
frame[2] == 2
frame[3] == 34
frame length == 40
CRC(frame[2..37]) == little_endian(frame[38..39])
```

Decode signed quaternion fields at offsets `30,32,34,36`, divide by `32767.0`, and verify that valid frames have norm close to `1.0` after normalization.

- [ ] **Step 4: Verify 200 Hz and no sustained frame loss.**

Capture at least 10 minutes and check sequence continuity, CRC errors, frame rate near 200 Hz, and `IMU_STATUS_TELEMETRY_DROPPED`. Record results in `docs/hardware/acceptance.md`; do not mark unrelated sensor/axis/PS checks as passed without their own measurements.

- [ ] **Step 5: Verify invalid-state behavior.**

During startup/calibration or induced sensor failure, confirm the status field does not contain `IMU_STATUS_VALID`; the receiver must ignore the quaternion even if the payload contains the last finite estimate.

## Review Checkpoints

- After Task 2: the pure encoder contract and all fixed-point rules are host-tested.
- After Task 3: the runtime data path carries the snapshot quaternion and submits exactly 40 bytes to DMA.
- After Task 5: the firmware links successfully, removing the pre-existing logging integration blocker.
- After Task 6: host, build, wire-format, timing, CRC, and hardware evidence are all available before claiming completion.
