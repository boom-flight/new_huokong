# STM32F103C8 BMI088 遥测固件

本项目是基于 RT-Thread 的 STM32F103C8 固件：采集 BMI088 数据，使用 Mahony 滤波器估算相对姿态，并通过 USART2 发送固定 200 Hz 二进制数据流。内存布局严格对应 STM32F103C8：64 KB Flash、20 KB SRAM。

## 操作流程

请在开发容器内从仓库根目录运行所有命令。

```sh
tools/test.sh       # 清理、构建并运行全部主机端 C 测试
tools/build.sh      # 构建固件并执行 Flash/SRAM 容量门禁
tools/flash.sh      # 构建一次，再通过 ST-Link/OpenOCD 烧录并校验
tools/debug.sh      # 启动 OpenOCD，并连接 arm-none-eabi-gdb 或 gdb-multiarch
tools/console.sh    # 以 115200 8-N-1 打开 USART1 控制台
```

`tools/test.sh test_imu_service_logic` 可运行一个指定的 C 测试程序。无参数形式会发现并运行 `build/host-tests/test_*` 中的全部可执行文件，不需要目标硬件。固件构建在 Flash 用量（`text + data`）超过 53,248 字节或静态 SRAM 用量（`data + bss`）超过 16,384 字节时失败；在物理容量内分别保留 12 KB Flash 和 4 KB SRAM 设计余量。

VS Code 提供 `host tests`、`firmware build + size`、`flash STM32F103`、`debug STM32F103` 和 `USART1 console` 任务。烧录任务只通过 `tools/flash.sh` 构建一次，不设置重复的构建依赖。Cortex-Debug 在启动前使用带容量门禁的固件构建任务。

## 硬件连接

| 功能 | 引脚 | 配置 |
| --- | --- | --- |
| USART2 TX | PA2 | 遥测，115200 8-N-1，DMA1 Channel 7 |
| USART2 RX | PA3 | 未配置 |
| BMI088 陀螺仪 CS | PA4 | GPIO 输出，空闲高电平 |
| SPI1 SCK | PA5 | 约 9 MHz |
| SPI1 MISO | PA6 | SPI1 输入 |
| SPI1 MOSI | PA7 | SPI1 输出 |
| BMI088 加速度计 DRDY/INT1 | PB12 | 下降沿 EXTI |
| BMI088 加速度计 CS | PB13 | GPIO 输出，空闲高电平 |
| BMI088 陀螺仪 DRDY/INT3 | PB14 | 下降沿 EXTI |
| 调试 USART1 TX/RX | PA9/PA10 | 控制台，115200 8-N-1 |
| 状态 LED | PB6 | 高电平点亮 |
| SWDIO/SWCLK | PA13/PA14 | ST-Link 烧录和调试 |

USART1 是通过板载 CP2102 使用的文本诊断控制台。USART2 是连接器 U15 上的二进制遥测输出，其帧不会出现在 USART1 USB 控制台中。硬件验收前必须确认 BMI088 的 `PS` 选择已在电气上配置为 SPI 模式。

## 启动与恢复

校准期间应保持开发板静止。固件接收 2,000 个静止陀螺仪样本，依据重力初始化横滚角和俯仰角，并计算运行时陀螺仪偏置。检测到运动会重置校准累计。每次启动和传感器完整重初始化后都会重新校准，校准数据不会持久化到 Flash。

连续三次 SPI 读取失败会使输出失效并进入 `FAULT_RETRY`。服务继续输出诊断信息和无效状态遥测，在双脉冲 LED 模式下等待一秒，然后重新初始化 BMI088 的两部分并重新校准。USART2 DMA 冲突只丢弃当前帧；状态位 8 会保持置位，直到后续帧成功进入发送队列。

USART1 只在启动和读取 ID 时记录一次日志、校准完成时记录一次、每次状态切换时记录一次，并且聚合错误计数的输出频率不超过每秒一次。ISR 和逐样本路径不输出文本。

| 状态 | PB6 指示 | 含义 |
| --- | --- | --- |
| `INITIALIZING` | 每 1,000 ms 点亮 100 ms | BMI088 配置与身份检查 |
| `CALIBRATING` | 每 100 ms 翻转 | 正在收集静止校准样本 |
| `RUNNING` | 每 2,000 ms 点亮 100 ms | 姿态估算和遥测已运行 |
| `FAULT_RETRY` | 亮 100 ms、灭 100 ms、亮 100 ms，然后保持熄灭至 1,000 ms | 传感器故障，等待完整重试 |

## 遥测协议

USART2 每 5 ms 发送一个严格为 32 字节的小端序帧。

| 偏移 | 字段 | 类型 | 编码 |
| ---: | --- | --- | --- |
| 0 | 同步字 | `uint8[2]` | `0xA5, 0x5A` |
| 2 | 版本 | `uint8` | `1` |
| 3 | 载荷长度 | `uint8` | `26` |
| 4 | 帧序号 | `uint16` | 每次尝试发送时递增，自然回绕 |
| 6 | 时间戳 | `uint32` | 微秒，自然回绕 |
| 10 | 状态 | `uint16` | 各位含义见下表 |
| 12 | 横滚角、俯仰角、偏航角 | `int16[3]` | 0.01 deg/LSB |
| 18 | 陀螺仪 X、Y、Z | `int16[3]` | 0.1 deg/s/LSB |
| 24 | 加速度 X、Y、Z | `int16[3]` | 0.001 g/LSB |
| 30 | CRC16 | `uint16` | CRC-16/CCITT-FALSE |

CRC 覆盖第 2 至 29 字节，多项式为 `0x1021`，初始值为 `0xFFFF`，不反射且不执行最终异或。检查向量 `123456789` 的结果为 `0x29B1`。

| 状态位 | 含义 |
| ---: | --- |
| 0 | 姿态和六轴输出有效 |
| 1 | 正在校准 |
| 2 | BMI088 初始化失败 |
| 3 | 陀螺仪达到或超过配置量程的 95% |
| 4 | 加速度数据不能用于 Mahony 修正 |
| 5 | 时间戳无效或过期 |
| 6 | SPI 读取错误 |
| 7 | IMU 数据就绪事件溢出 |
| 8 | 自上次成功入队后发生过 USART2 丢帧 |
| 9-15 | 保留，发送值为零 |

机体加速度字段表示机体系比力并包含重力，不是去除重力后的线加速度。横滚角和俯仰角以重力为参考；由于没有磁力计或外部航向参考，偏航角只相对于启动时刻。相对偏航角可能漂移，不能表示绝对航向。

## 范围限制

当前版本有意不包含 USART2 RX、PA8 加热控制、点火控制、绝对偏航、Flash 校准持久化、运行时配置和角加速度。任何新增字段或行为只要改变二进制帧，就必须发布新的协议版本。

## 第三方来源

仓库中的第三方源码是不可变的固定快照，不得格式化或规范化。

| 组件 | 固定提交 |
| --- | --- |
| RT-Thread 和 STM32 通用驱动 | `ddf52e2cdd977f14fc04035c88672ac204aec713` |
| CMSIS-Core | `39d8e01f0be84b83a8f11d33756e82ce1ef07a84` |
| STM32F1 CMSIS 设备包 | `4d57f5017d2937f10d07331e90828d3a81f980b8` |
| STM32F1 HAL | `0b18f3336e7ef67e51080e72ae6805dba6cc7bb8` |

源码 URL 和 SVD 来源见 `packages/provenance.md`。上板结果应记录在 `docs/hardware-acceptance.md`；主机测试不能把硬件项目从“待上板验证”改为“通过”。
