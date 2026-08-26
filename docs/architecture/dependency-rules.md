# 依赖规则

## 允许的方向

```text
app ---------> kernel ---------> modules ---------> utility
 |                |
 |                +-----------> platform 抽象接口
 +----------------------------> platform 具体实现

platform 具体实现 -----------> modules 中的端口契约（需要时）
platform 具体实现 -----------> utility
platform 具体实现 -----------> vendor
```

运行时回调可以从 IRQ 或 DMA 完成路径通知上层，但反向控制流不能形成反向源码依赖。

## 强制边界

- `modules` 和 `utility` 不得包含或调用 RT-Thread、STM32 HAL、CMSIS、`platform` 或 `vendor` 的头文件、类型、宏和函数。
- `platform` 不得依赖 `kernel`，不得包含内核头文件。平台通过回调、状态、事件或函数表向上提供能力。
- `kernel` 不得直接包含 HAL 或 CMSIS。内核私有运行时 `.c` 可以使用 RT-Thread；内核公共头文件不得暴露任何 RT-Thread 类型、宏或句柄。
- `kernel`、`modules` 和 `utility` 的公共接口不得暴露 HAL 或 CMSIS 类型。模块公共头文件只使用 C 标准类型和所属模块定义的类型。
- `vendor` 不得依赖任何自研代码，也不得为了适配自研层而直接修改未记录的第三方源码。
- 可变外设句柄属于 `platform` 私有状态。每个 IRQ 和 HAL 回调在最终链接中必须只有一个强定义和一个明确所有者。
- 内核之间只交换不可变快照或窄命令，不读取另一个内核的内部状态。
- IMU kernel 通过 `imu_snapshot_t` 向 telemetry kernel 提供快照；telemetry 只能通过 `imu_service_record_telemetry_drop()` 反馈丢帧诊断，不能访问 IMU 内部状态。
- IMU 样本读取必须比较 SPI 读取前后的 DRDY sequence。sequence 变化时必须拒绝样本并保留 overrun 状态和计数；不得用该次读取结果更新有效快照。
- 遥测传输必须区分 DMA busy、同步启动失败和异步错误。三类结果都进入丢帧策略；发送尝试序号即使发送未排队也必须消耗。
- 服务启动失败按子资源先于父对象的顺序回滚。停止时等待线程退出失败必须返回失败，不得伪装成成功；当前部分底层清理返回值尚未完整纳入状态传播，因此不承诺所有清理失败都保留可重试状态。
- 名称或接口带有 IMU、遥测、云台、机器人或火控语义的代码不得进入 `utility`。代码只有被至少两个非测试、非平台生产模块使用时，才具备进入 `utility` 的必要条件。

## 头文件所有权

include 路径必须表达所有权，例如 `attitude/mahony.h`、`bmi088/bmi088.h` 和 `imu/imu_snapshot.h`。消费者只获得模块明确导出的公共 include 目录；私有头文件目录不加入全局搜索路径。公共头文件应能在其声明的直接依赖下独立编译，不能依赖其他头文件偶然先被包含。

## 构建声明

根 `SConscript` 只进入固定列出的层和模块。每个自研 `SConscript` 必须显式列出每个生产源文件、自己的 include 路径和直接依赖；禁止使用 `Glob`、递归扫描或目录遍历发现生产源码。新增、移动或删除 `.c` 文件时，必须在同一变更中更新对应显式清单。所有 SCons 入口固定解析仓库内的 `vendor/rt-thread/`，不接受外部 `RTT_ROOT` 替换第三方构建源。

对象目标也按所有权显式落入 `build/scons/firmware/<layer>/<module>/`。主机测试统一位于 `build/scons/host-tests/`，编译数据库统一写入 `build/scons/compile_commands.json`。主机测试在没有 RT-Thread、HAL、CMSIS 和板级 include 路径的环境中编译 `modules`，以验证边界不是只靠约定维持。

架构总体说明见[固件架构概览](overview.md)。
