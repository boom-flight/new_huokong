# STM32F103C8 BMI088 Telemetry Firmware

This RT-Thread firmware samples a BMI088, estimates relative attitude with a Mahony filter, and transmits a fixed 200 Hz binary stream over USART2. It targets the STM32F103C8 memory map exactly: 64 KB Flash and 20 KB SRAM.

## Operator Workflow

Run all commands from the repository root in the development container.

```sh
.script/test.sh       # clean-build and run every host C test plus the size-gate test
.script/build.sh      # build firmware and enforce Flash/SRAM design limits
.script/flash.sh      # build once, then flash and verify through ST-Link/OpenOCD
.script/gdb.sh        # start OpenOCD and attach arm-none-eabi-gdb or gdb-multiarch
.script/console.sh    # open the USART1 console at 115200 8-N-1
```

`.script/test.sh test_imu_service_logic` runs one named C test executable. The no-argument form discovers every executable matching `tests/build/test_*`; no target hardware is needed. The firmware build fails above 53,248 bytes of Flash (`text + data`) or 16,384 bytes of static SRAM (`data + bss`), leaving 12 KB of Flash and 4 KB of SRAM as design reserve inside the physical limits.

VS Code provides `host tests`, `firmware build + size`, `flash STM32F103`, `debug STM32F103`, and `USART1 console` tasks. The flash task delegates its only build to `.script/flash.sh`; it has no duplicate build dependency. Cortex-Debug uses the size-gated firmware build as its prelaunch task.

## Connections

| Function | Pin | Configuration |
| --- | --- | --- |
| USART2 TX | PA2 | Telemetry, 115200 8-N-1, DMA1 Channel 7 |
| USART2 RX | PA3 | Not configured |
| BMI088 gyro CS | PA4 | GPIO output, idle high |
| SPI1 SCK | PA5 | Approximately 9 MHz |
| SPI1 MISO | PA6 | SPI1 input |
| SPI1 MOSI | PA7 | SPI1 output |
| BMI088 accel DRDY/INT1 | PB12 | Falling-edge EXTI |
| BMI088 accel CS | PB13 | GPIO output, idle high |
| BMI088 gyro DRDY/INT3 | PB14 | Falling-edge EXTI |
| Debug USART1 TX/RX | PA9/PA10 | Console, 115200 8-N-1 |
| State LED | PB6 | Active high |
| SWDIO/SWCLK | PA13/PA14 | ST-Link flash and debug |

USART1 is the text diagnostic console through the onboard CP2102. USART2 is the binary telemetry output on connector U15; its frames do not appear on the USART1 USB console. Verify the BMI088 `PS` selection electrically in SPI mode before hardware acceptance.

## Startup And Recovery

Keep the board stationary during calibration. Firmware accepts 2,000 stationary gyroscope samples, initializes roll and pitch from gravity, and derives a runtime gyro bias. Motion resets calibration accumulation. Calibration is repeated after boot and every full sensor reinitialization; it is not persisted to Flash.

Three consecutive SPI read failures invalidate output and enter `FAULT_RETRY`. The service continues diagnostics and invalid-status telemetry, waits one second while driving the two-pulse LED pattern, then reinitializes both BMI088 halves and recalibrates. USART2 DMA contention drops only the current frame; status bit 8 remains set until a later frame is queued.

USART1 logs startup and IDs once, calibration completion once, each state transition once, and aggregate error counters no more than once per second. No ISR or per-sample path prints text.

| State | PB6 indication | Meaning |
| --- | --- | --- |
| `INITIALIZING` | 100 ms on every 1,000 ms | BMI088 setup and identity checks |
| `CALIBRATING` | Toggle every 100 ms | Stationary calibration is collecting samples |
| `RUNNING` | 100 ms on every 2,000 ms | Attitude and telemetry are active |
| `FAULT_RETRY` | 100 ms on, 100 ms off, 100 ms on, then off to 1,000 ms | Sensor fault and scheduled full retry |

## Telemetry Protocol

USART2 sends an exact 32-byte little-endian frame every 5 ms.

| Offset | Field | Type | Encoding |
| ---: | --- | --- | --- |
| 0 | Sync | `uint8[2]` | `0xA5, 0x5A` |
| 2 | Version | `uint8` | `1` |
| 3 | Payload length | `uint8` | `26` |
| 4 | Frame sequence | `uint16` | Increments per attempted frame, natural wrap |
| 6 | Timestamp | `uint32` | Microseconds, natural wrap |
| 10 | Status | `uint16` | Bits described below |
| 12 | Roll, Pitch, Yaw | `int16[3]` | 0.01 deg/LSB |
| 18 | Gyro X, Y, Z | `int16[3]` | 0.1 deg/s/LSB |
| 24 | Accel X, Y, Z | `int16[3]` | 0.001 g/LSB |
| 30 | CRC16 | `uint16` | CRC-16/CCITT-FALSE |

CRC covers bytes 2 through 29 with polynomial `0x1021`, initial value `0xFFFF`, no reflection, and no final XOR. The `123456789` check vector produces `0x29B1`.

| Status bit | Meaning |
| ---: | --- |
| 0 | Attitude and six-axis output valid |
| 1 | Calibration in progress |
| 2 | BMI088 initialization failed |
| 3 | Gyroscope at or above 95 percent of configured range |
| 4 | Accelerometer invalid for Mahony correction |
| 5 | Timestamp invalid or stale |
| 6 | SPI read error |
| 7 | IMU data-ready event overrun |
| 8 | USART2 frame dropped since the previous successful queue |
| 9-15 | Reserved, transmitted as zero |

The body acceleration fields are body-frame specific force and include gravity. They are not gravity-subtracted linear acceleration. Roll and pitch are gravity-referenced; yaw is only relative to startup because there is no magnetometer or external heading reference. Relative yaw can drift and does not provide absolute heading.

## Scope

This version intentionally excludes USART2 RX, PA8 heater control, ignition control, absolute yaw, Flash calibration persistence, runtime configuration, and angular acceleration. Adding fields or behavior that changes the binary frame requires a new protocol version.

## Vendor Provenance

Vendored sources are immutable snapshots. Do not reformat or normalize them.

| Component | Pinned commit |
| --- | --- |
| RT-Thread and STM32 common drivers | `ddf52e2cdd977f14fc04035c88672ac204aec713` |
| CMSIS-Core | `39d8e01f0be84b83a8f11d33756e82ce1ef07a84` |
| STM32F1 CMSIS device | `4d57f5017d2937f10d07331e90828d3a81f980b8` |
| STM32F1 HAL | `0b18f3336e7ef67e51080e72ae6805dba6cc7bb8` |

See `packages/provenance.md` for source URLs and SVD provenance. Record board results in `docs/hardware-acceptance.md`; host tests cannot convert hardware rows from Pending hardware to Pass.
