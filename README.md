# STM32F103C8 BMI088 遥测固件

本项目是基于 RT-Thread 的 STM32F103C8 固件：采集 BMI088 数据，使用 Mahony 滤波器估算相对姿态，并通过 USART2 发送固定 40 字节、200 Hz 的 v2 二进制数据流。目标芯片为 64 KB Flash、20 KB SRAM。

## 操作流程

请在开发容器内从仓库根目录运行公开入口：

```sh
tools/test.sh       # 运行主机端 C 测试 fixture，构建固件并执行仓库门禁
tools/build.sh      # 构建固件并执行 Flash/SRAM 容量门禁
tools/flash.sh      # 构建一次，自动识别 SWD 探针后烧录并校验
tools/debug.sh      # 启动 OpenOCD，并连接 arm-none-eabi-gdb 或 gdb-multiarch
tools/console.sh    # 以 115200 8-N-1 打开 USART1 控制台
tools/bridge.sh     # 自动发现 USART1 并启动 Foxglove bridge
```

烧录和调试入口会自动识别 CMSIS-DAP/DAPLink/FireDAP、ST-Link 和 J-Link。未识别或同时连接多个探针时，可使用 `HUOKONG_PROBE=cmsis-dap|stlink|jlink` 明确选择，例如 `HUOKONG_PROBE=cmsis-dap tools/flash.sh`。

Linux/SCons 的固件、主机测试和编译数据库产物位于 `build/scons/`，编译数据库固定为 `build/scons/compile_commands.json`。Keil/MDK5 工作流打开 `project/keil/huokong.uvprojx` 并构建 Debug target，产物位于 `build/keil/stm32f103c8/Debug/`。Keil 原生构建需要 Windows/MDK5，Linux 环境只执行 manifest、工程 XML 和 scatter file 静态检查；`.uvoptx/.uvguix` 为本地忽略的 IDE 元数据。

从 clean checkout 开始可以直接运行 `tools/test.sh`；它先构建并运行主机端 C 测试，再构建固件，随后执行尺寸、ELF/MAP 链接所有权、布局及相关静态门禁。单独运行 `tools/build.sh` 也会执行尺寸和链接所有权门禁。

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
