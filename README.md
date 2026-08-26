# STM32F103C8 BMI088 遥测固件

本项目是基于 RT-Thread 的 STM32F103C8 固件：采集 BMI088 数据，使用 Mahony 滤波器估算相对姿态，并通过 USART2 发送固定 200 Hz 二进制数据流。目标芯片为 64 KB Flash、20 KB SRAM。

## 操作流程

请在开发容器内从仓库根目录运行公开入口：

```sh
tools/test.sh
tools/build.sh
tools/flash.sh
tools/debug.sh
tools/console.sh
```

`tools/test.sh` 运行主机端 C 测试以及仓库门禁；`tools/build.sh` 构建固件并执行 Flash/SRAM 容量门禁。烧录和调试入口支持 CMSIS-DAP/DAPLink/FireDAP、ST-Link 和 J-Link；未识别或同时连接多个探针时，可使用 `HUOKONG_PROBE=cmsis-dap|stlink|jlink` 明确选择。

## 文档

- [架构总览](docs/architecture/overview.md)
- [依赖边界](docs/architecture/dependency-rules.md)
- [构建、测试和调试](docs/development/build-test-debug.md)
- [IMU 遥测协议 v1](docs/protocols/imu-telemetry-v1.md)
- [固件行为与恢复](docs/requirements/firmware-behavior.md)
- [硬件验收](docs/hardware/acceptance.md)

硬件验收目前仍为“待上板验证”。主机测试不能将此状态改为通过。

## 公开限制

当前版本有意不包含 USART2 RX、PA8 加热控制、点火控制、绝对偏航、Flash 校准持久化、运行时配置和角加速度。任何改变 32 字节 USART2 帧的数据或行为都必须发布新的协议版本。

## 第三方来源

第三方源码是不可变固定快照，不得格式化或规范化。版本、源码 URL、SVD 来源和 EXTI ownership patch 见 [`vendor/manifest.md`](vendor/manifest.md)。
