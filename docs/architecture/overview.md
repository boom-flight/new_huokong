# 固件架构概览

## 当前范围

本仓库当前只生成一个面向 `STM32F103C8` 的固件镜像。固件采集 `BMI088`，计算姿态，并通过串口发送 IMU 遥测。当前没有实现云台、电机、点火、CAN 或完整火控功能，也不维护多板卡、多 MCU 或多镜像框架。

## 目录模型

```text
src/
├── app/
├── kernel/
│   ├── imu/
│   └── telemetry/
├── modules/
│   ├── attitude/
│   ├── devices/bmi088/
│   ├── protocols/imu_telemetry/
│   ├── timing/
│   └── transport/
└── platform/
    ├── board/stm32f103c8/
    ├── devices/
    ├── time/
    └── transport/

vendor/
├── rt-thread/
├── cmsis-core/
├── stm32f1-cmsis/
├── stm32f1-hal/
├── rt-thread-stm32-drivers/
├── patches/
└── manifest.md
```

- `app` 是唯一装配入口，只决定初始化顺序、连接依赖并启动服务，不保存传感器策略或协议编码逻辑。
- `kernel` 拥有长期运行流程。当前 `imu` 内核管理采集、校准、姿态状态和恢复，`telemetry` 内核管理 200 Hz 调度、序号、丢帧状态和发送。
- `modules` 保存输入输出明确、可在主机独立测试的实现，包括姿态数学、BMI088 寄存器访问、32 字节协议编码、计时扩展和 DMA 发送状态机。
- `platform` 保存 STM32F103C8 板级实现、具体外设、IRQ、HAL 回调和时间/传输适配器。
- `vendor` 保存固定版本的 RT-Thread、CMSIS、STM32 HAL 和通用 STM32 驱动快照；来源、版本和本地补丁记录在 `vendor/manifest.md`。

只有出现至少两个生产消费者且能力不含业务语义时才创建 `src/utility/`。没有实现的能力不预建空目录；未来若实现云台或火控，应在需求和边界明确后新增对应内核，而不是把占位代码写入当前固件。

## 运行关系

启动入口先初始化 IMU 服务，再启动遥测服务。IMU 服务通过平台适配器接收 BMI088 数据就绪事件，发布不可变快照；遥测服务读取快照，调用纯协议编码器，再通过 USART2 DMA 适配器发送。平台适配器拥有全部硬件句柄、中断和 HAL 回调，内核不直接拥有寄存器级外设状态。

编译产物集中在 `build/`。固件、主机测试、编译数据库和 SCons 状态分别写入固定子目录，不在源码目录或仓库根目录生成对象和镜像。

## 延伸文档

- [依赖规则](dependency-rules.md)
- [构建、测试与调试](../development/build-test-debug.md)
- [固件行为需求](../requirements/firmware-behavior.md)
- [IMU 遥测协议 v1](../protocols/imu-telemetry-v1.md)
- [硬件验收记录](../hardware/acceptance.md)
