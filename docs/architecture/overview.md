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
├── debug/
│   └── Foxglove snapshot service
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
- `modules` 保存输入输出明确、可在主机独立测试的实现，包括姿态数学、BMI088 寄存器访问、40 字节协议编码、计时扩展和 DMA 发送状态机。
- `debug` 保存可选的 Foxglove 快照帧编码和低优先级服务；它依赖 IMU 快照和
  USART1 的平台传输契约，不反向成为 platform 的依赖。
- `platform` 保存 STM32F103C8 板级实现、具体外设、IRQ、HAL 回调和时间/传输适配器。
  `STM32 Foxglove UART` 复用 RT-Thread 的 `uart1` 设备；debug 模式下 USART1
  只输出 104 字节 Foxglove 帧，正常模式保留 USART1 控制台和 USART2 遥测。
- `vendor` 保存固定版本的 RT-Thread、CMSIS、STM32 HAL 和通用 STM32 驱动快照；来源、版本和本地补丁记录在 `vendor/manifest.md`。

只有出现至少两个生产消费者且能力不含业务语义时才创建 `src/utility/`。没有实现的能力不预建空目录；未来若实现云台或火控，应在需求和边界明确后新增对应内核，而不是把占位代码写入当前固件。

## 运行关系

按当前 `src/app/main.c`，启动入口实际先初始化 telemetry 服务，再初始化 IMU 服务；IMU 初始化失败时回滚已经启动的 telemetry。该顺序与计划中的“先 IMU 后 telemetry”描述不一致，本仓库当前源码和测试是事实来源，本次文档整理不改变它。启动失败路径按子资源先于父对象的顺序释放已创建资源；停止时等待线程退出失败会返回失败，不把停止报告为成功。当前 `telemetry_service.c` 忽略 `rt_thread_detach()` 返回值，`imu_service.c` 也未把 event/deinit 清理返回值完整纳入结果，因此不能声称所有底层清理失败都保留可重试状态。

IMU 服务通过 BMI088 平台适配器接收 DRDY 事件并发布不可变快照；遥测服务读取快照，调用纯协议编码器，再通过 USART2 DMA 适配器发送。SPI 读取在传输前后各取得一次 DRDY latch；若 sequence 在读取期间变化，则拒绝本次样本，并保留 `IMU_STATUS_EVENT_OVERRUN` 和相应 overrun 诊断计数，而不发布过期或不一致的数据。平台适配器拥有全部硬件句柄、中断和 HAL 回调，内核不直接拥有寄存器级外设状态。

遥测每 5 个 RT-Thread tick 尝试一次发送。UART 适配器分别报告 DMA busy、同步 DMA 启动失败和异步 UART 错误；三者都会被遥测策略作为丢帧处理，序号按每次尝试消耗，粘滞丢帧状态由后续成功排队清除。

IMU 服务成功初始化后，debug 服务（若由 Kconfig 启用）以低优先级线程每 20
个 tick 读取一次不可变快照，并通过 USART1 平台契约发送 Foxglove v1 帧。
该适配器不拥有 USART1 IRQ 或 HAL 回调，不依赖 kernel 或 debug 源码。

编译产物集中在 `build/`。固件、主机测试、编译数据库和 SCons 状态分别写入固定子目录；SCons 固定使用仓库内 `vendor/rt-thread/`，编译数据库固定为 `build/scons/compile_commands.json`，不在源码目录或仓库根目录生成对象和镜像。

## 延伸文档

- [依赖规则](dependency-rules.md)
- [构建、测试与调试](../development/build-test-debug.md)
- [固件行为需求](../requirements/firmware-behavior.md)
- [IMU 遥测协议 v2](../protocols/imu-telemetry-v2.md)
- [Foxglove Debug 协议 v1](../protocols/foxglove-debug-v1.md)
- [硬件验收记录](../hardware/acceptance.md)
