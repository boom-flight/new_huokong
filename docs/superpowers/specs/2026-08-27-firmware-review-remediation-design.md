# Firmware Review Remediation Design

## Goal

修复 `new_huokong` 审查发现的运行时生命周期、采样一致性、构建复现和测试门禁问题，同时删除没有实际边界收益的重复抽象，不改变 USART2 v1 协议格式、传感器配置和既有状态语义。

## Scope

- IMU 读取必须检测 DRDY 事件在 SPI 读取期间发生的变化；无法证明读取数据与锁存事件对应时，丢弃该次样本并保留 overrun 诊断。
- 故障重试必须清理 RT-Thread 事件残留，不能把重初始化前的事件当成新样本。
- 启动失败必须释放已经初始化的服务和平台资源；日志服务失败必须保持可重试且回滚已创建对象。
- 遥测 DMA 状态机只保留必要的状态转移，发送结果必须一次性表达 busy、同步启动失败和异步错误。
- SCons、Keil 和 clangd 使用一致且固定的工程事实；默认测试必须执行链接所有权和产物布局门禁。
- 保留现有模块边界；仅合并没有独立生产消费者或造成重复状态判断的抽象，不进行与本目标无关的目录重写。

## Architecture

运行时修复集中在 IMU 服务和平台 BMI088 适配器，平台适配器提供中断安全的 DRDY latch 读取，IMU 服务在 SPI 读取前后比较序号。服务生命周期由显式 deinit/rollback 路径负责，静态 RT-Thread 对象在停止前先阻止线程继续运行。

遥测发送由一个明确的尝试结果驱动，DMA 模块只维护 busy、failure_pending 和对应的 reserve/release/error 转移。构建门禁继续以显式 manifest 为源文件边界，同时保证 SCons 使用的编译配置与 Keil 生成配置不再静默分叉。

## Non-goals

- 不新增 USART2 RX、运行时配置、Flash 校准持久化、PA8 控制、CAN 或其他未实现功能。
- 不改变 32 字节 IMU telemetry v1 帧、字段缩放、CRC 或序号规则。
- 不声称软件可以证明 BMI088 物理数据寄存器的绝对样本身份；软件只拒绝检测到并发更新的无法确认样本。
- 不把 vendor 快照格式化、重命名或重构。

## Acceptance Criteria

- 主机测试覆盖：DRDY 读取期间序号变化、故障重试事件清理、日志初始化失败后的再次初始化、DMA 各类发送结果和新门禁脚本。
- `./tools/test.sh` 通过，并明确执行 `test_link_owners.py`。
- `./tools/build.sh` 通过，Flash 和静态 SRAM 仍低于现有门限。
- `python3 tests/scripts/test_link_owners.py build/scons/firmware/huokong.elf build/scons/firmware/huokong.map` 通过。
- `build/scons/compile_commands.json` 与 `.clangd` 配置一致，外部 `RTT_ROOT` 不会替换固定 vendor RT-Thread。
- 架构文档、构建文档和主 README 不再互相重复或描述不存在的门禁。
