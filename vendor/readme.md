# `vendor` 第三方依赖说明

`vendor/` 保存本固件直接使用的第三方源码固定快照。这里的代码不是本项目自研模块，不应进行格式化、无记录的功能修改或版本漂移。来源、完整 commit、许可证、迁移前路径和补丁状态以 [`manifest.md`](manifest.md) 为准。

## 依赖总览

| 目录或文件 | 第三方来源 | 固定版本 | 许可证 | 在本项目中的作用 |
| --- | --- | --- | --- | --- |
| `vendor/rt-thread/` | [RT-Thread/rt-thread](https://github.com/RT-Thread/rt-thread) | `ddf52e2cdd977f14fc04035c88672ac204aec713` | Apache License 2.0 | 提供 RT-Thread 内核、线程、事件、时钟、IPC、内存管理和组件构建入口。`src/kernel` 的长期运行服务基于它运行。 |
| `vendor/rt-thread-stm32-drivers/` | [RT-Thread/rt-thread](https://github.com/RT-Thread/rt-thread)，原始子路径 `bsp/stm32/libraries/HAL_Drivers/` | `ddf52e2cdd977f14fc04035c88672ac204aec713` | Apache License 2.0 | 提供 STM32 通用 GPIO、串口、SPI、DMA、定时器等 RT-Thread 设备驱动，使 `src/platform` 能使用 RT-Thread 设备抽象。 |
| `vendor/cmsis-core/` | [RT-Thread-packages/CMSIS-Core](https://github.com/RT-Thread-packages/CMSIS-Core) | `39d8e01f0be84b83a8f11d33756e82ce1ef07a84` | Apache License 2.0 | 提供 ARM CMSIS 核心头文件、编译器适配和 Cortex-M 内核定义。 |
| `vendor/stm32f1-cmsis/` | [RT-Thread-packages/cmsis-device-f1](https://github.com/RT-Thread-packages/cmsis-device-f1) | `4d57f5017d2937f10d07331e90828d3a81f980b8` | Apache License 2.0 | 提供 STM32F1 系列设备头文件、寄存器定义、启动文件和系统级设备描述，支撑 STM32F103C8 编译和启动。 |
| `vendor/stm32f1-hal/` | [RT-Thread-packages/stm32f1xx-hal-driver](https://github.com/RT-Thread-packages/stm32f1xx-hal-driver) | `0b18f3336e7ef67e51080e72ae6805dba6cc7bb8` | BSD 3-Clause | 提供 STM32F1 HAL API 和外设实现。当前构建显式使用 SPI、TIM、TIM_EX 等 HAL 源文件。 |
| `.vscode/STM32F103xx.svd` | [cmsis-svd/cmsis-svd-data](https://github.com/cmsis-svd/cmsis-svd-data) | `c65f8551e57c770344d229dcaa0bf838fa29aff4` | STMicroelectronics EULA 1.0 | 为 VS Code/Cortex-Debug 提供 STM32F103xx 寄存器和外设调试描述；它位于 `.vscode/`，不在 `vendor/` 目录内，但同样纳入来源追踪。 |

## 各依赖的边界

### RT-Thread

RT-Thread 是系统运行时基础设施，不承载本项目的 BMI088 算法或遥测协议。它负责线程调度、同步对象、时间服务、设备框架以及组件的构建集成。`src/app` 和 `src/kernel` 可以通过 RT-Thread API 运行服务，但 `src/modules` 不得依赖 RT-Thread。

### STM32 通用驱动

`rt-thread-stm32-drivers` 是 RT-Thread 针对 STM32 的设备驱动集合，位于 RT-Thread 与本项目平台适配器之间。它提供通用设备能力，例如 GPIO、UART、SPI 和定时器；具体的 BMI088 SPI 配置、DRDY 中断所有权和遥测 DMA 业务行为仍由 `src/platform` 管理。

该快照应用了一个本地补丁：

```text
vendor/patches/rt-thread-stm32-drivers-exti15-10-owner.patch
```

补丁由 `BSP_GPIO_EXTI15_10_EXTERNAL=y` 控制，避免通用 GPIO 驱动重复定义 `EXTI15_10_IRQHandler`。这样通用驱动仍保留 `HAL_GPIO_EXTI_Callback`，而 BMI088 平台适配器拥有 EXTI15_10 中断入口。重放命令和完整上下文见 [`manifest.md`](manifest.md)。

### CMSIS-Core 与 STM32F1 CMSIS

两者共同提供 ARM Cortex-M3 和 STM32F1 的底层编译契约：前者描述通用 ARM 内核，后者描述 STM32F103xx 芯片寄存器、启动代码和设备宏。它们不包含本项目的板级时钟策略；板级时钟和链接布局由 `src/platform/board/stm32f103c8` 负责。

### STM32F1 HAL

STM32F1 HAL 将 STM32 外设寄存器操作封装为 HAL API。`src/platform/devices`、`src/platform/time` 和 `src/platform/transport` 使用它访问 SPI、GPIO、TIM、USART 和 DMA。HAL 不知道 IMU 快照、Mahony 姿态或 40 字节 v2 遥测协议，因此这些业务职责不会进入该快照。

### STM32F103xx SVD

SVD 只服务于调试器和 IDE 的寄存器可视化，不参与固件链接。文件的 SHA-256 和来源 commit 已记录在 `manifest.md`，用于防止调试描述文件与芯片型号不匹配。

## 维护规则

- 固定版本必须使用完整 commit，不使用浮动分支或 tag 代替。
- 不修改第三方源码；确有必要时，将差异写成 `vendor/patches/` 下的可重放补丁。
- 更新依赖前同步修改 `manifest.md`，记录上游地址、commit、许可证、原始路径、目标路径和补丁状态。
- 不让 vendor 代码反向依赖 `src/`。自研适配逻辑放在 `src/platform`，自研业务逻辑放在 `src/kernel` 或 `src/modules`。
- 不把 vendor 目录中的 README、测试样例或上游工具误认为本固件生产模块；真正参与固件构建的入口由根 `SConstruct` 和根 `SConscript` 显式声明。

## 相关文档

- [自研源码架构](../docs/architecture/overview.md)
- [依赖边界](../docs/architecture/dependency-rules.md)
- [固定来源清单](manifest.md)
