# STM32F103C8 BMI088 遥测固件

本项目是基于 RT-Thread 的 STM32F103C8 固件：采集 BMI088 数据，使用 Mahony 滤波器估算相对姿态，并通过 USART2 发送固定 40 字节、200 Hz 的 v2 二进制数据流。目标芯片为 64 KB Flash、20 KB SRAM。

## 操作流程

请在开发容器内从仓库根目录运行公开入口：

```sh
tools/test.sh       # 运行主机端 C 测试 fixture，构建固件并执行仓库门禁
tools/build.sh      # 构建固件并执行 Flash/SRAM 容量门禁
tools/flash.sh      # 构建一次，通过 ST-LINK/SWD 烧录并校验
tools/flash-stlink.sh    # 显式通过 ST-LINK/SWD 烧录
tools/flash-cp2102.sh    # 显式通过 CP2102/USART1 Bootloader 烧录
tools/debug.sh      # 启动 OpenOCD，并连接 arm-none-eabi-gdb 或 gdb-multiarch
tools/console.sh    # 以 115200 8-N-1 打开 USART1 控制台
tools/bridge.sh     # 自动发现 USART1 并启动 Foxglove bridge
```

默认烧录入口 `tools/flash.sh` 使用 ST-LINK/SWD。CP2102 串口烧录使用独立入口 `tools/flash-cp2102.sh`，默认设备为 `/dev/ttyUSB0`，也可以显式指定稳定设备名：

```sh
tools/flash-cp2102.sh /dev/serial/by-id/usb-Silicon_Labs_CP210x_...
# 或
HUOKONG_SERIAL=/dev/serial/by-id/usb-Silicon_Labs_CP210x_... tools/flash-cp2102.sh
```

CP2102 烧录完成后 `stm32flash` 退出并释放 USB 串口，随后可使用同一设备打开控制台。`tools/debug.sh` 仍自动识别 CMSIS-DAP/DAPLink/FireDAP、ST-Link 和 J-Link；未识别或同时连接多个探针时，可使用 `HUOKONG_PROBE=cmsis-dap|stlink|jlink` 明确选择。

本板默认使用 `RTS` 控制 `BOOT0`、`DTR` 控制复位；若 CP2102 转接板的极性不同，可用 `HUOKONG_STM32FLASH_GPIO` 覆盖 `stm32flash -i` 控制序列。

Linux/SCons 的固件、主机测试和编译数据库产物位于 `build/scons/`，编译数据库固定为 `build/scons/compile_commands.json`。Keil/MDK5 工作流打开 `project/keil/huokong.uvprojx` 并构建 Debug target，产物位于 `build/keil/stm32f103c8/Debug/`。Keil 原生构建需要 Windows/MDK5，Linux 环境只执行 manifest、工程 XML 和 scatter file 静态检查；`.uvoptx/.uvguix` 为本地忽略的 IDE 元数据。

从 clean checkout 开始可以直接运行 `tools/test.sh`；它先构建并运行主机端 C 测试，再构建固件，随后执行尺寸、ELF/MAP 链接所有权、布局及相关静态门禁。单独运行 `tools/build.sh` 也会执行尺寸和链接所有权门禁。

## BMI088 技术验证

### 硬件连接与信号职责

BMI088 的加速度计和陀螺仪是两个独立的内部子器件，共用 SPI1 的时钟和数据线，分别使用独立的片选。当前 STM32F103C8 引脚分配如下：

| 信号 | STM32 引脚 | 固件职责 |
| --- | --- | --- |
| SPI1 SCK | PA5 | SPI 时钟 |
| SPI1 MISO/SDO | PA6 | 主机输入 |
| SPI1 MOSI/SDI | PA7 | 主机输出 |
| 陀螺仪 CS | PA4 | 软件片选，低有效 |
| 加速度计 CS | PB13 | 软件片选，低有效 |
| 加速度计 DRDY/INT1 | PB12 | EXTI15_10，下降沿 |
| 陀螺仪 DRDY/INT3 | PB14 | EXTI15_10，下降沿 |

传感器的 `VDD`、`VDDIO` 应按照模块原理图接到合法电压，当前目标板预期为 3.3 V；`PS` 必须明确配置为 SPI 模式，不能悬空。STM32、BMI088、USB-UART 和逻辑分析仪必须共地，不能把 5 V 直接接到 BMI088 或 STM32 GPIO。

### SPI 事务与 CHIP ID

BMI088 的 CHIP ID 寄存器地址都是 `0x00`，期望值不同：

| 子器件 | CHIP ID 期望值 | 三轴数据起始寄存器 | 读取规则 |
| --- | --- | --- | --- |
| 加速度计 | `0x1E` | `0x12` | 命令后增加 1 个 dummy byte |
| 陀螺仪 | `0x0F` | `0x02` | 命令后不增加 dummy byte |

每个读取事务都在一次完整的 `HAL_SPI_TransmitReceive()` 中完成。读取时序为：目标 CS 拉低，发送读命令（寄存器地址按位或 `0x80`），发送所需 dummy 字节并接收数据，事务结束后目标 CS 拉高。两个 CS 不得同时拉低。

加速度计上电或软复位后，第一次 CHIP ID 访问还需要先执行一个独立的单字节读取，以把加速度计从 I2C 状态切换到 SPI 状态，然后再执行实际的 CHIP ID 读取。因此逻辑分析仪上第一次加速度计 ID 检查会看到两个 CS 事务；后续读取只遵循表中的单次事务规则。

在正常返回路径中，即使 HAL SPI 操作失败，代码也会在事务结束处释放 CS。因此初始化失败或单次读取失败后，PA4/PB13 最终都应回到高电平；逻辑分析仪上只会看到失败事务产生的短暂片选脉冲。

### 单传感器临时探针

根配置中的 `HUOKONG_BMI088_PROBE_MODE` 是互斥三选一：

| 配置项 | 行为 |
| --- | --- |
| `Normal dual-sensor firmware` | 启动 telemetry 和 IMU 服务，初始化并使用两个子器件 |
| `Gyroscope only` | 只访问 PA4/陀螺仪，PB13 保持高电平 |
| `Accelerometer only` | 只访问 PB13/加速度计，PA4 保持高电平 |

选择方式：

```sh
tools/menuconfig.sh
```

选择模式后，配置会生成到 `rtconfig.h`。也可以检查实际编译配置：

```sh
rg 'HUOKONG_BMI088_PROBE_(GYRO|ACCEL)' rtconfig.h
```

探针模式不会启动 telemetry、IMU 线程、DRDY 采样调度或 Mahony 校准。平台初始化仍会配置 PB12/PB14 的 DRDY GPIO 和 EXTI，但探针使用空的 DRDY 回调，因此不会产生采样调度；随后只通过 SPI 直接周期性读取目标子器件。当前探针每 500 ms 重复读取一次 CHIP ID 和六字节原始数据，因此串口应持续出现以下类型的日志：

```text
BMI088 probe: gyro-only, the other CS must stay high
BMI088 probe: CHIP ID=0x0F (PASS)
BMI088 probe: gyro-only raw=(..., ..., ...)
```

或：

```text
BMI088 probe: accel-only, the other CS must stay high
BMI088 probe: CHIP ID=0x1E (PASS)
BMI088 probe: accel-only raw=(..., ..., ...)
```

`CHIP ID read failed` 表示一次独立 ID 事务失败；`CHIP ID=0xFF (FAIL)` 通常表示 MISO 没有被传感器驱动。原始值 `(-1, -1, -1)` 等价于六个数据字节全部为 `0xFF`，不能视为有效的静止加速度或角速度。

### 逻辑分析仪优先于万用表

SPI 信号是 MHz 级数字波形，万用表读数只能作为粗略平均值，不能用来判断逻辑高低或 CHIP ID。应在 MCU 引脚侧使用示波器或逻辑分析仪观察：

陀螺仪探针：

- PA4 在事务期间拉低并产生片选脉冲。
- PB13 全程保持高电平。
- PA5 出现 0 V 到 3.3 V 的 SPI 时钟，Mode 3 空闲高电平。
- PA7 发送读命令，PA6 在对应返回阶段产生数据。

加速度计探针：

- PB13 在事务期间拉低并产生片选脉冲。
- PA4 全程保持高电平。
- CHIP ID 读取包含命令、dummy 和返回字节；六字节数据读取包含命令、dummy 和六字节返回数据。

建议按下面顺序缩小问题范围：

1. 没有 PA5 时钟：检查 SPI1 初始化、PA5 引脚和目标板供电。
2. 有时钟但目标 CS 没拉低：检查 PA4/PB13 的实际走线和模块 CS 引脚。
3. CS 和 MOSI 正常、MISO 始终为高：检查 PA6/MISO、PS、VDDIO、GND 和电平转换器。
4. MISO 有跳变但 ID 不正确：检查 SPI Mode 3、MSB first、命令字节和 dummy 时序。
5. ID 正确但数据全 `0xFF`：继续检查数据事务的寄存器地址、dummy byte 和传感器工作状态。

### 初始化失败与复位现象

默认双传感器固件的 `bmi088_init()` 会同时检查两个 CHIP ID。任一子器件失败都会进入 `IMU_FAULT_RETRY`，等待约 1 秒后重新初始化。因此以下日志表示初始化没有通过，而不是正常采样：

```text
IMU state: initializing -> fault-retry
IMU state: fault-retry -> initializing
IMU errors: spi=0 accel_samples=0 gyro_samples=0 ...
```

初始化阶段失败当前不会增加 `spi_errors`，所以 `spi=0` 与初始化失败可以同时出现；`accel_samples=0` 和 `gyro_samples=0` 表示尚未进入正常 DRDY 采样阶段。

按下 RESET 时 UART 输出中断是正常的。保持控制台打开并松开复位键后，探针模式应重新输出探针启动行和 CHIP ID；默认双传感器模式应重新输出普通启动、状态和 `BMI088 IDs:` 日志。如果松开后没有恢复，检查 `BOOT0` 是否为低电平、`NRST` 是否回到约 3.3 V，以及 ST-Link 是否持续拉低复位线。若 `BOOT0` 为高电平，芯片会进入系统 Bootloader，不会运行应用日志。

### 编译、烧录与观察日志

编译和默认烧录入口：

```sh
tools/flash-stlink.sh       # 自动构建，需要 ST-Link/SWD
tools/console.sh /dev/ttyUSB0
```

`tools/flash-stlink.sh` 和 `tools/flash-cp2102.sh` 都会先调用 `tools/build.sh`，再执行烧录，因此不需要在前面单独运行一次构建。只需要构建而不烧录时使用 `tools/build.sh`。`tools/flash.sh` 当前只是 `flash-stlink.sh` 的默认别名。只有 CP2102 时不能使用 ST-Link/SWD 脚本，应使用 STM32 USART1 系统 Bootloader：

```sh
tools/flash-cp2102.sh /dev/ttyUSB0
tools/console.sh /dev/ttyUSB0
```

ST-Link 需要连接 SWDIO、SWCLK、NRST、GND 和 VTref；CP2102 烧录需要让 STM32 进入系统 Bootloader，默认由 RTS 控制 `BOOT0`、DTR 控制复位。烧录器输出 `init mode failed`、`Failed to init device after retry` 或 `OpenOCD init failed` 都表示尚未完成目标连接，不能认为固件已经写入。成功烧录应看到编程、校验和复位完成信息，随后控制台才会显示与新模式对应的日志。

## 启动与恢复

当前 `src/app/main.c` 的实际调用顺序是先初始化 telemetry，再初始化 IMU；这与“先 IMU 后 telemetry”的计划描述不一致。本次只整理文档，未修改生产源码，因此这里按当前源码和测试记录实际行为。IMU 初始化失败时会停止并释放已成功启动的 telemetry，再报告失败；启动失败路径按子资源先于父对象的顺序回滚。停止时等待线程退出失败会返回失败，不伪装成成功；但部分底层清理返回值尚未纳入状态传播，因此不能据此保证所有清理失败都保留可重试状态。

## 文档

- [架构总览](docs/architecture/overview.md)
- [依赖边界](docs/architecture/dependency-rules.md)
- [构建、测试和调试](docs/development/build-test-debug.md)
- [IMU 遥测协议 v2](docs/protocols/imu-telemetry-v2.md)
- [固件行为与恢复](docs/requirements/firmware-behavior.md)
- [硬件验收](docs/hardware/acceptance.md)

硬件验收目前仍为“待上板验证”。主机测试不能将此状态改为通过。

## 公开限制

当前版本有意不包含 USART2 RX、PA8 加热控制、点火控制、绝对偏航、Flash 校准持久化、运行时配置和角加速度。任何改变固定 40 字节 USART2 v2 帧的数据或行为都必须发布新的协议版本。

## 第三方来源

第三方源码是不可变固定快照，不得格式化或规范化。版本、源码 URL、SVD 来源和 EXTI ownership patch 见 [`vendor/manifest.md`](vendor/manifest.md)。
