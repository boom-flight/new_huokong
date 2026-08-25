# STM32F103C8T6 BMI088 Telemetry Firmware Design

## Status

Approved in chat on 2026-08-25. This document defines the first implementation scope for replacing the repository's STM32F427 firmware with a custom STM32F103C8T6 firmware.

## Goals

- Replace the root firmware with an STM32F103C8T6 project based on the RT-Thread v5.2.2 `stm32f103-blue-pill` BSP.
- Continue using STM32 HAL and RT-Thread.
- Read the onboard BMI088 over SPI1 using data-ready interrupts.
- Run a float Mahony attitude estimator at the gyroscope sample rate.
- Publish relative Roll, Pitch, Yaw, angular velocity, and linear acceleration over USART2.
- Send a compact, versioned, CRC-protected binary frame at 200 Hz and 115200 baud.
- Preserve and adapt the Ubuntu container, GCC, SCons, OpenOCD, and VS Code workflow.
- Fit within the official STM32F103C8T6 limits of 64 KB Flash and 20 KB SRAM.

## Non-Goals

- Heater control on PA8.
- Four-channel ignition control.
- Absolute yaw estimation.
- Persisting calibration values in internal Flash.
- Runtime telemetry configuration over USART2.
- USB, CAN, filesystem, networking, FinSH, or unrelated packages.
- Compatibility with the old STM32F427 build.
- Porting the RMCS Eigen EKF or the librmcs USB protocol.
- Sending angular acceleration. BMI088 does not measure angular acceleration directly.

## Baseline And Migration

The implementation will replace the root project with the RT-Thread v5.2.2 `bsp/stm32/stm32f103-blue-pill` GCC BSP. The BSP is a starting point, not the target board definition. Its clock, pins, linker limits, enabled components, and board documentation will be changed for the custom PCB. The pinned RT-Thread kernel, STM32F1 CMSIS device files, HAL, and common STM32 drivers will be checked into this repository so normal builds do not download dependencies.

The following repository content remains:

- `demand/`, including the schematic, pin table, and `demand_analysis.md`.
- `.devcontainer/`, adapted to the new firmware where needed.
- `.script/`, adapted to STM32F1 build, flash, debug, console, and test commands.
- `.vscode/`, adapted to STM32F103 and `target/stm32f1x.cfg`.
- Git history, which remains the source for the removed STM32F427 implementation.

The old F427 board support, HAL F4 library, startup files, linker scripts, and applications are removed from the active build. No dual-target compatibility layer will be introduced.

RT-Thread configuration will enable only the kernel facilities needed by two static application threads, event notification, pin support, and the USART1 console. Runtime application objects use static allocation, and application code performs no dynamic allocation after initialization.

## Hardware Definition

The design uses the following schematic and pin table:

- `demand/平衡炮口转接板V2.0.pdf`
- `demand/平衡炮炮口转接板引脚定义表.xlsx`

| Function | Pin | Configuration |
| --- | --- | --- |
| USART2_TX | PA2 | 115200, 8-N-1, DMA1 Channel 7 |
| USART2_RX | PA3 | Not used in the first version |
| BMI088 gyro CS | PA4 | GPIO push-pull, idle high |
| SPI1_SCK | PA5 | SPI mode required by BMI088, approximately 9 MHz |
| SPI1_MISO | PA6 | SPI1 input |
| SPI1_MOSI | PA7 | SPI1 output |
| BMI088 accel DRDY/INT1 | PB12 | Falling-edge EXTI |
| BMI088 accel CS | PB13 | GPIO push-pull, idle high |
| BMI088 gyro DRDY/INT3 | PB14 | Falling-edge EXTI |
| Heater PWM | PA8 | Reserved, not configured by this version |
| Debug USART1_TX/RX | PA9/PA10 | RT-Thread console, 115200, 8-N-1 |
| LED | PB6 | Firmware state indication |
| SWDIO/SWCLK | PA13/PA14 | ST-Link debug and flash |

PB12 and PB14 share `EXTI15_10_IRQn`; the ISR must inspect the pending lines and dispatch each source independently. The BMI088 `PS` hardware selection must be in SPI mode before hardware acceptance testing.

USART1 is connected to the onboard CP2102 and remains the debug console. USART2 is routed to connector U15 and is the telemetry link to the balance-gun controller.

## Source Layout And Ownership

```text
applications/
  main.c
  imu_service.c
  imu_service.h
  telemetry_service.c
  telemetry_service.h

drivers/
  bmi088.c
  bmi088.h
  bmi088_port.c
  bmi088_port.h

algorithm/
  mahony.c
  mahony.h
  imu_calibration.c
  imu_calibration.h

protocol/
  imu_telemetry.c
  imu_telemetry.h

board/
  board.c
  board.h
  CubeMX_Config/
  linker_scripts/

tests/
  host-side tests for algorithm, calibration, protocol, and CRC
```

Module responsibilities:

- `bmi088.c` owns register definitions, reset and identity checks, sensor configuration, raw burst reads, and conversion using the configured ranges.
- `bmi088_port.c` owns HAL SPI1 access, both chip-select GPIOs, both data-ready interrupt inputs, TIM2 timestamps, and the USART2 TX DMA primitive used by telemetry.
- `imu_calibration.c` owns stationary detection and boot-time gyroscope bias estimation.
- `mahony.c` owns quaternion state, accelerometer feedback, gyroscope integration, quaternion normalization, and Euler conversion.
- `imu_service.c` owns event handling, sample sequencing, calibration state, data validation, algorithm updates, diagnostics, and snapshot publication.
- `imu_telemetry.c` is a platform-independent encoder for the fixed binary protocol.
- `telemetry_service.c` owns the 200 Hz schedule, snapshot consumption, double TX buffers, and DMA completion state.

HAL directly owns SPI1, TIM2, BMI088 GPIO/EXTI, and USART2 DMA. The RT-Thread device framework owns USART1 console output. A peripheral must not be initialized or serviced by both layers.

## Data Types And Coordinate Convention

Raw samples are represented as three signed 16-bit values plus a 32-bit microsecond timestamp and a monotonic 32-bit sample sequence. Converted internal values use `float`:

- Acceleration: g.
- Angular velocity: deg/s at the service boundary, converted to rad/s inside Mahony.
- Quaternion: scalar-first `(w, x, y, z)`.
- Euler output: degrees, normalized to `[-180, 180)`.

A compile-time signed permutation maps sensor axes to body axes. The first implementation uses the identity mapping for bench bring-up because the schematic does not define the physical body orientation. Hardware acceptance must verify the mounting orientation and change this one mapping definition if necessary; no algorithm or protocol code changes are allowed for axis remapping.

Yaw is relative to the orientation at estimator initialization. A six-axis BMI088 cannot provide absolute yaw and yaw drift is expected.

## Sensor Configuration

The first version configures:

- Accelerometer range: +/-6 g.
- Accelerometer output data rate: 800 Hz.
- Accelerometer data-ready output: INT1, push-pull, active low.
- Gyroscope range: +/-2000 deg/s.
- Gyroscope output data rate: 1000 Hz with the corresponding BMI088 bandwidth setting.
- Gyroscope data-ready output: INT3, push-pull, active low.

Initialization performs the required accelerometer dummy read to select SPI mode, soft resets both sensor halves, checks accelerometer `CHIP_ID=0x1E` and gyroscope `CHIP_ID=0x0F`, writes configuration registers, and reads each critical register back. Each critical operation is attempted at most three times per initialization cycle.

SPI1 uses blocking HAL transfers from `imu_service`, with the bus serialized by that single thread. ISR code never accesses SPI. The approximately 9 MHz bus clock leaves margin below the STM32F103 SPI1 limit while keeping six-byte burst reads short.

## Timing And Concurrency

TIM2 is a 16-bit timer on STM32F103C8T6. It runs at 1 MHz and raises an update interrupt every 65.536 ms. The update ISR increments a 16-bit software high word, producing a 32-bit microsecond clock that wraps after approximately 71.6 minutes. The timestamp read helper atomically combines the high word and TIM2 counter and accounts for a pending update flag, so a read concurrent with overflow cannot move backward. Unsigned subtraction handles the final 32-bit wrap naturally.

The interrupt path is:

1. Identify whether PB12, PB14, or both asserted.
2. Read the extended TIM2 microsecond clock once for each asserted source.
3. Store its latest timestamp and increment its sample sequence.
4. Set the corresponding RT-Thread event bit.
5. Return without SPI, floating-point work, logging, or frame encoding.

The IMU thread uses a 768-byte static stack and is the highest-priority application thread. It drains gyroscope work before accelerometer work when both are pending. A changed sequence with more than one unconsumed sample records an event overrun.

Every valid gyroscope sample updates Mahony using the most recent valid accelerometer sample. `dt` comes from consecutive gyroscope timestamps. Duplicate timestamps, implausible deltas outside 0.5 to 2.0 ms, or a gap greater than 20 ms do not enter attitude integration. The timebase is restarted from the next valid sample.

The initial Mahony gains are `Kp=0.2` and `Ki=0.0`, matching the conservative RMCS Mahony configuration. They are compile-time constants for the first version. The estimator starts from the gravity direction after calibration, with yaw set to zero.

An accelerometer sample always remains available for telemetry. It participates in Mahony correction only when its norm is within 0.7 to 1.3 g.

The IMU snapshot contains:

- Microsecond timestamp.
- Sensor and estimator status bits.
- Quaternion.
- Roll, Pitch, and relative Yaw.
- Body-frame angular velocity.
- Body-frame acceleration.
- Diagnostic counters.

Two statically allocated snapshots and an active index provide publication. The producer fills the inactive snapshot and switches the active index in a short interrupt-disabled critical section. The telemetry consumer copies the active snapshot under the same short protection, so it cannot observe a partial update.

The telemetry thread uses a 512-byte static stack and wakes every five RT-Thread ticks at a 1000 Hz system tick. It encodes the newest snapshot into one of two static 32-byte buffers. If USART2 DMA is idle, transmission starts. If DMA is still active, the frame is dropped and the drop counter is incremented; telemetry never blocks IMU processing.

## Boot Calibration

After BMI088 initialization, both service threads run and telemetry reports the calibration state. Calibration collects 2000 accepted gyroscope samples, approximately two seconds at 1 kHz.

A sample is accepted only when:

- Acceleration norm is between 0.9 and 1.1 g.
- Gyroscope norm is below 3 deg/s.
- The sample timestamps are valid.

Detected movement resets the accepted-sample count and running sums. The estimator and telemetry data-valid bit remain disabled until calibration completes. The average gyroscope vector becomes the runtime bias and is subtracted from subsequent samples. Calibration values are not written to Flash and are recomputed at every boot or sensor reinitialization.

## USART2 Protocol

All multi-byte values are little-endian. The frame is exactly 32 bytes.

| Offset | Field | Type | Encoding |
| ---: | --- | --- | --- |
| 0 | Sync | `uint8[2]` | `0xA5, 0x5A` |
| 2 | Version | `uint8` | `1` |
| 3 | Payload length | `uint8` | `26` |
| 4 | Frame sequence | `uint16` | Increment per attempted frame, natural wrap |
| 6 | Timestamp | `uint32` | Microseconds, natural wrap |
| 10 | Status | `uint16` | Bit field below |
| 12 | Roll, Pitch, Yaw | `int16[3]` | 0.01 deg/LSB |
| 18 | Gyro X, Y, Z | `int16[3]` | 0.1 deg/s/LSB |
| 24 | Accel X, Y, Z | `int16[3]` | 0.001 g/LSB |
| 30 | CRC16 | `uint16` | CRC-16/CCITT-FALSE |

CRC covers bytes 2 through 29. Its parameters are polynomial `0x1021`, initial value `0xFFFF`, no input or output reflection, and no final XOR. The standard test string `123456789` produces `0x29B1`.

Status bits:

| Bit | Meaning |
| ---: | --- |
| 0 | Attitude and six-axis output valid |
| 1 | Boot calibration in progress |
| 2 | BMI088 initialization failed |
| 3 | Gyroscope at or above 95 percent of configured range |
| 4 | Accelerometer invalid for Mahony correction |
| 5 | Timestamp invalid or stale |
| 6 | SPI read error |
| 7 | IMU data-ready event overrun |
| 8 | At least one USART2 frame was dropped since the previous successful frame |
| 9-15 | Reserved and transmitted as zero |

Encoding uses explicit byte writes and saturation helpers rather than a packed C struct, so layout does not depend on ABI alignment or host endianness. At 32 bytes and 200 Hz, the link carries 6400 bytes per second, approximately 56 percent of the useful 115200 8-N-1 capacity.

Angular acceleration is not included. Adding it requires a new protocol version and a specified derivative filter.

## State Machine And Error Recovery

The runtime states are `INITIALIZING`, `CALIBRATING`, `RUNNING`, and `FAULT_RETRY`.

- `INITIALIZING`: configure and verify both BMI088 halves. Success enters `CALIBRATING`; failure enters `FAULT_RETRY`.
- `CALIBRATING`: collect stationary samples and publish status frames with valid bit clear. Completion initializes Mahony and enters `RUNNING`.
- `RUNNING`: acquire, validate, estimate, and publish telemetry.
- `FAULT_RETRY`: retain USART1 diagnostics and LED indication, publish invalid fault frames when USART2 is available, wait one second, and retry complete sensor initialization.

Three consecutive SPI read failures clear output validity and cause complete sensor reinitialization followed by a new calibration. A gyroscope gap greater than 20 ms clears estimator validity and restarts the timestamp baseline; repeated read failures control whether the full sensor reset is required.

Gyroscope saturation sets its status flag and publishes the clipped sensor reading. The estimator result remains available but is marked unreliable through the status field. Acceleration outside the correction gate is published but excluded from gravity feedback.

USART2 DMA contention drops only the current frame. Bit 8 remains set until a later frame is successfully queued, ensuring the receiver observes the loss indication.

USART1 logging is limited to startup information, identity values, calibration completion, state transitions, and rate-limited error summaries. No high-frequency path prints text.

PB6 LED indications are:

- `INITIALIZING`: short pulse every second.
- `CALIBRATING`: 5 Hz toggle.
- `RUNNING`: short pulse every two seconds.
- `FAULT_RETRY`: two short pulses every second.

## Container And Tooling

The existing Ubuntu container remains the build environment on an Arch Linux host. It continues to use `arm-none-eabi-gcc`, SCons, OpenOCD, GDB, and Cortex-Debug with `/dev` passed into the privileged container.

Required changes include:

- GCC target flags `-mcpu=cortex-m3 -mthumb`, with all FPU and hard-float flags removed.
- STM32F1 CMSIS device, HAL, and common STM32 driver sources checked into the repository from the pinned RT-Thread BSP dependencies.
- A linker script with exactly 64 KB ROM at `0x08000000` and 20 KB RAM at `0x20000000`.
- OpenOCD `target/stm32f1x.cfg`.
- An STM32F103 SVD in the Cortex-Debug configuration.
- USART1 console defaults in `.script/console.sh` and VS Code tasks.
- A host-test script and a post-build size check.

The build must not require Keil, STM32CubeIDE, or host-distribution-specific tools outside the container.

## Testing

Platform-independent algorithm, calibration, protocol, and CRC code is compiled natively for host tests. `tests/SConstruct` builds small C test executables with the container's host compiler and standard-library assertions; no third-party test framework is added. `.script/test.sh` performs a clean host-test build and runs every executable.

Required automated tests:

- CRC standard vector returns `0x29B1`.
- Protocol frame is exactly 32 bytes with the specified offsets, little-endian values, scales, saturation, status bits, angle wrapping, and CRC range.
- Mahony remains finite for zero acceleration, invalid acceleration, stationary input, constant angular velocity, and rejected `dt` values.
- Static gravity vectors converge to the corresponding Roll and Pitch while relative Yaw starts at zero.
- Calibration completes from stationary samples, resets on motion, and computes the expected bias.
- The TIM2 high-word extension handles ordinary overflow, a pending overflow during capture, and final 32-bit wrap without backward timestamps.
- Value encoding saturates instead of overflowing.

Firmware verification commands must include a clean SCons build and an ELF size check. Linker limits enforce the hard maximums of 64 KB Flash and 20 KB RAM. Design targets are at most 52 KB Flash and 16 KB statically occupied RAM, retaining operating margin for stacks and future diagnostics.

Because target hardware is not currently connected, first-version completion requires:

- All host tests passing.
- A clean GCC/SCons firmware build.
- The ELF linked against the exact 64 KB/20 KB memory map.
- Flash, debug, console, and test scripts updated for STM32F103.
- Hardware-only checks documented and explicitly reported as pending.

Hardware acceptance after the board becomes available requires:

- Accelerometer identity `0x1E` and gyroscope identity `0x0F`.
- Measured data-ready rates within 5 percent of 800 Hz and 1000 Hz.
- Ten minutes with no IMU event overrun under normal operation.
- USART2 output at 200 Hz for ten minutes with no receiver CRC errors.
- Stationary level Roll and Pitch within approximately 2 degrees after calibration.
- Relative Yaw without discontinuities; drift is measured and recorded, not treated as absolute-heading accuracy.
- Recovery after sensor reset, temporary receiver disconnection, and board power cycling.

## Implementation Sequence

1. Replace the root with the pinned RT-Thread F103 BSP and restore the retained documentation and container workflow.
2. Establish the 64 KB/20 KB GCC build, USART1 console, LED, OpenOCD, and VS Code debug configuration.
3. Add host-test infrastructure and protocol/CRC tests.
4. Add BMI088 register driver tests, port interface, SPI1, chip selects, TIM2, and EXTI handling.
5. Implement identity checks, configuration, blocking burst reads, and error recovery.
6. Implement calibration and Mahony using test-driven host tests.
7. Implement snapshot publication and the IMU service thread.
8. Implement the fixed telemetry encoder, USART2 DMA double buffering, and the telemetry thread.
9. Add state reporting, rate-limited diagnostics, LED patterns, and size enforcement.
10. Run all available automated verification and document pending hardware acceptance results.
