# IMU 遥测协议 v2

## 传输约定

固件通过 USART2 TX 以 115200 baud、8 data bits、no parity、1 stop bit 发送固定 40 字节二进制帧，调度频率为 200 Hz。多字节整数（包括有符号 `int16_t`）全部采用 little-endian。协议版本固定为 `2`，payload length 固定为 `34`。

## 字节布局

| 偏移 | 长度 | 字段 | 编码 |
| --- | ---: | --- | --- |
| `0..1` | 2 | `sync` | 固定 `0xA5 0x5A`，不纳入 CRC。 |
| `2` | 1 | `version` | 固定 `2`。 |
| `3` | 1 | `payload_length` | 固定 `34`（`0x22`）。 |
| `4..5` | 2 | `sequence` | `uint16_t`，每次发送尝试递增并自然回绕。 |
| `6..9` | 4 | `timestamp_us` | `uint32_t`，IMU 快照的微秒时间戳。 |
| `10..11` | 2 | `status` | `uint16_t`，编码前与 `0x01FF` 按位与。 |
| `12..13` | 2 | `euler_x` | `int16_t`，角度先归一化到 `[-180, 180)`，再按 `degree * 100` 编码。 |
| `14..15` | 2 | `euler_y` | 与 `euler_x` 相同。 |
| `16..17` | 2 | `euler_z` | 与 `euler_x` 相同。 |
| `18..19` | 2 | `gyro_x` | `int16_t`，按 `degree_per_second * 10` 编码。 |
| `20..21` | 2 | `gyro_y` | 与 `gyro_x` 相同。 |
| `22..23` | 2 | `gyro_z` | 与 `gyro_x` 相同。 |
| `24..25` | 2 | `accel_x` | `int16_t`，按 `g * 1000` 编码。 |
| `26..27` | 2 | `accel_y` | 与 `accel_x` 相同。 |
| `28..29` | 2 | `accel_z` | 与 `accel_x` 相同。 |
| `30..31` | 2 | `quaternion_w` | `q_w * 32767`，四舍五入并饱和。 |
| `32..33` | 2 | `quaternion_x` | `q_x * 32767`，四舍五入并饱和。 |
| `34..35` | 2 | `quaternion_y` | `q_y * 32767`，四舍五入并饱和。 |
| `36..37` | 2 | `quaternion_z` | `q_z * 32767`，四舍五入并饱和。 |
| `38..39` | 2 | `crc16` | CRC 结果，little-endian，覆盖 `frame[2..37]`。 |

浮点值缩放后按最接近整数舍入，并饱和到 `INT16_MIN..INT16_MAX`。非有限值编码为 `0`。四元数分量按 `q * 32767` 编码，接收端解码后应重新归一化。角度 `180.0` 归一化为 `-180.0`。

## 状态位

| 位 | 掩码 | 含义 |
| ---: | --- | --- |
| 0 | `0x0001` | `IMU_STATUS_VALID`：运行中且姿态估计有效。 |
| 1 | `0x0002` | `IMU_STATUS_CALIBRATING`：正在静止校准。 |
| 2 | `0x0004` | `IMU_STATUS_BMI_INIT_FAILED`：BMI088 初始化失败。 |
| 3 | `0x0008` | `IMU_STATUS_GYRO_SATURATED`：陀螺仪原始值达到饱和判据。 |
| 4 | `0x0010` | `IMU_STATUS_ACCEL_CORRECTION_INVALID`：加速度修正样本无效或过期。 |
| 5 | `0x0020` | `IMU_STATUS_TIMESTAMP_INVALID`：陀螺仪时间基线或间隔无效。 |
| 6 | `0x0040` | `IMU_STATUS_SPI_ERROR`：最近处理路径发生 SPI 读取错误。 |
| 7 | `0x0080` | `IMU_STATUS_EVENT_OVERRUN`：数据就绪序号出现跳跃。 |
| 8 | `0x0100` | `IMU_STATUS_TELEMETRY_DROPPED`：此前发送尝试发生丢帧。 |

编码器只保留上述九位，因此状态掩码必须是 `0x01FF`。

## CRC-16/CCITT-FALSE

- polynomial：`0x1021`
- init：`0xFFFF`
- refin：`false`
- refout：`false`
- xorout：`0x0000`
- 覆盖范围：`frame[2..37]`，共 36 字节
- 标准校验向量：ASCII `123456789` 得到 `0x29B1`

同步字节 `frame[0..1]` 和 CRC 字段 `frame[38..39]` 不参与计算。黄金帧测试中的 CRC 字节为 `0x3F 0x89`，即数值 `0x893F`。

## 调度与丢帧

遥测线程每 5 个 RT-Thread tick 唤醒一次；当前 tick 频率为 1000 Hz，因此目标频率为 200 Hz。线程使用两个 40 字节发送缓冲区，只有 DMA 成功排队后才切换缓冲区。

`sequence` 在每次发送尝试开始时消耗，包括 UART 忙或排队失败的尝试。发送失败会累计丢帧数并设置粘滞状态；下一次编码帧携带 `IMU_STATUS_TELEMETRY_DROPPED`，成功排队后清除粘滞状态。接收端可同时使用序号间隙和状态位诊断丢帧。

v1 接收端不能解析 v2；固件不会在同一 USART2 数据流中混发 v1 和 v2。

固件状态和恢复要求见[固件行为需求](../requirements/firmware-behavior.md)。
