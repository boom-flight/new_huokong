# 固件行为需求

## 范围与验证状态

本需求描述当前单一 `STM32F103C8`、单一 BMI088 IMU 遥测固件的已实现行为，不包含云台、电机、点火、CAN 或火控功能。主机测试和交叉构建只能证明软件门禁通过，不能证明物理板卡、传感器安装、信号完整性或长时间通信已通过；物理结果以[硬件验收记录](../hardware/acceptance.md)为准。

## 时钟与总线

- 外部 8 MHz HSE 晶振直接进入 PLL，PLL 倍频为 9，`SYSCLK` 为 72 MHz；HSI 保持开启。
- AHB 不分频，为 72 MHz；APB1 二分频，为 36 MHz；APB2 不分频，为 72 MHz。Flash latency 配置为 2 wait states。
- TIM2 位于 APB1。定时器预分频值为 71、周期为 65535，以 1 MHz 计数；16 位溢出高字扩展形成 32 位微秒时间戳。
- SPI1 为主机、8 bit、MSB first、双线，CPOL high、CPHA second edge（SPI mode 3），APB2 时钟八分频，软件片选，单次 HAL 传输超时 10 ms。
- USART2 遥测为 TX only、115200 baud、8N1、无流控、16 倍过采样，使用 DMA1 Channel 7 normal mode。
- USART1 是 RT-Thread 控制台，使用 PA9 TX、PA10 RX，控制台速率为 115200 baud。

## 引脚

| 功能 | MCU 引脚 | 行为 |
| --- | --- | --- |
| 状态 LED | PB6 | 推挽输出，高电平点亮。 |
| BMI088 gyro CS | PA4 | 推挽输出，低有效。 |
| SPI1 SCK | PA5 | 复用推挽。 |
| SPI1 MISO | PA6 | 浮空输入。 |
| SPI1 MOSI | PA7 | 复用推挽。 |
| BMI088 accel CS | PB13 | 推挽输出，低有效。 |
| BMI088 accel DRDY | PB12 | 上拉、下降沿 EXTI。 |
| BMI088 gyro DRDY | PB14 | 上拉、下降沿 EXTI。 |
| USART2 telemetry TX | PA2 | 复用推挽；固件不启用遥测 RX。 |
| USART1 console TX/RX | PA9 / PA10 | TX 复用推挽，RX 上拉输入。 |
| SWDIO / SWCLK | PA13 / PA14 | 保留 SWD，启动时关闭 JTAG 复用。 |
| HSE OSC_IN / OSC_OUT | PD0 / PD1 | 外接 8 MHz 晶振。 |

PB12 与 PB14 共用 `EXTI15_10_IRQn`。中断在下降沿锁存微秒时间戳和递增序号，再通知 IMU 线程；不得由另一个驱动重复定义该共享向量。

## BMI088 初始化与采样

- 轴映射当前为 `x<-x`、`y<-y`、`z<-z`，三个符号均为正，并在软件中验证为右手系。该映射仍需通过实物安装测量确认。
- 初始化先执行加速度计预读，再依次软复位加速度计和陀螺仪，等待 50 ms 与 30 ms。
- 期望 chip ID 为 accel `0x1E`、gyro `0x0F`。总线读写和寄存器写回校验最多尝试 3 次。
- 加速度计配置为 ±6 g、800 Hz data ready；陀螺仪配置为 ±2000 degree/s、1000 Hz data ready。实际速率属于待上板测量项。
- 加速度样本按 `6 / 32768` 转换为 g，陀螺仪样本按 `2000 / 32768` 转换为 degree/s。
- 任一轴原始陀螺仪绝对值达到 31130 时设置饱和状态。

## 校准

进入 `IMU_CALIBRATING` 后必须连续接受 2000 组有效校准样本。有效样本同时满足：

- 已建立陀螺仪时间基线，间隔在 500..2000 us 内，且没有数据就绪 overrun。
- 加速度样本已存在、已被消费且相对陀螺仪时间戳不超过 5000 us。
- 加速度模长在 0.9..1.1 g 内，陀螺仪模长小于 3.0 degree/s。

若存在尚未消费的新加速度样本，本次陀螺仪样本跳过但不重置累计；其他不满足条件的样本重置校准。校准期间发生 SPI 读取失败或采样 overrun 也会重置累计。

完成后使用均值作为 `gyro_bias_dps` 和 `gravity_g`，以平均重力初始化 Mahony，参数为 `kp=0.2`、`ki=0.0`，然后进入 `IMU_RUNNING`。运行中先扣除陀螺仪 bias 再积分。

## 状态、故障与恢复

状态机固定为：

1. `IMU_INITIALIZING`：启动或重试时初始化 BMI088。
2. `IMU_CALIBRATING`：传感器初始化成功后执行静止校准。
3. `IMU_RUNNING`：校准完成后更新姿态和发布有效快照。
4. `IMU_FAULT_RETRY`：初始化失败或连续 3 次样本读取失败后进入故障等待。

`IMU_FAULT_RETRY` 保持 1 秒后增加 `sensor_reinitializations`，返回 `IMU_INITIALIZING` 并重新执行传感器初始化。任一成功读取把连续失败计数清零。初始化、线程创建或平台适配器启动失败时，已初始化资源按逆序释放，应用入口报告失败，不把部分初始化状态视为可运行。

陀螺仪时间间隔在 500..2000 us 时允许姿态积分；小于 500 us 或在 2001..20000 us 时跳过本次积分并记录 rejected dt，但保持已有估计；大于 20000 us 时同时记录 long gap 并使估计失效。运行中若超过 20000 us 且没有待处理陀螺仪事件，在 20001 us 到期点设置 timestamp invalid 并使估计失效。后续新样本重新建立时间基线。

用于姿态修正的加速度样本必须与已消费序号一致、相对陀螺仪不超过 5000 us，且模长在 0.7..1.3 g 内；否则只禁用该次加速度修正，不阻止合规陀螺仪积分。

状态 LED 模式为：初始化 100 ms 亮/900 ms 灭；校准 100 ms 亮/100 ms 灭；运行 100 ms 亮/1900 ms 灭；故障为 100 ms 亮、100 ms 灭、100 ms 亮、700 ms 灭的双闪周期。诊断计数每秒输出到控制台。

## 线程与中断优先级

| 执行单元 | 优先级 | 栈 | time slice |
| --- | ---: | ---: | ---: |
| IMU 线程 | 5 | 768 bytes | 10 ticks |
| RT-Thread main 线程 | 10 | 512 bytes | 由 RT-Thread 配置管理 |
| telemetry 线程 | 15 | 512 bytes | 10 ticks |

RT-Thread 当前使用 32 级优先级和 1000 Hz tick。`EXTI15_10_IRQn` 与 `TIM2_IRQn` 的 NVIC preemption priority 为 5、subpriority 为 0；`DMA1_Channel7_IRQn` 与 `USART2_IRQn` 为 6、0。

## 遥测行为

遥测线程每 5 ticks 调度一次，即 200 Hz。它读取双缓冲 IMU 快照，编码固定 32 字节 v1 帧，并通过 USART2 DMA 发送；发送缓冲区只在排队成功后切换。UART 忙、同步 DMA 启动失败或异步 UART 错误都会累计丢帧并设置粘滞状态，后续帧通过状态位和序号间隙报告。

帧的每个偏移、缩放、状态位和 CRC 见[IMU 遥测协议 v1](../protocols/imu-telemetry-v1.md)。
