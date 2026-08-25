# Hardware Acceptance

Board ID: Pending hardware

Test date: Pending hardware

Operator: Pending hardware

| Check | Requirement | Result |
| --- | --- | --- |
| BMI088 PS selection | PS verified in SPI mode electrically | Pending hardware |
| Body-axis mounting | Right-handed signed permutation measured; identity confirmed or map changed | Pending hardware |
| Identity | Accel 0x1E and gyro 0x0F | Pending hardware |
| Data-ready rates | Accel 800 Hz and gyro 1000 Hz, each within 5% | Pending hardware |
| Event endurance | 10 minutes, zero normal-operation IMU overruns | Pending hardware |
| Telemetry endurance | 200 Hz for 10 minutes, zero receiver CRC errors | Pending hardware |
| Stationary attitude | Level roll/pitch within approximately 2 degrees after calibration | Pending hardware |
| Relative yaw | No discontinuities; drift measured and recorded | Pending hardware |
| Recovery | Sensor reset, receiver disconnect, and power-cycle recovery observed | Pending hardware |

Host simulation and automated builds do not satisfy any row. Enter a dated hardware result only after performing that check on the target board.
