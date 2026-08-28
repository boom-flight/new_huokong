# 构建、测试与调试

## 环境

Linux/SCons 构建需要 GCC 主机编译器、SCons、`arm-none-eabi-gcc` 工具链、Newlib、GNU Arm binutils、GDB、OpenOCD 和 picocom。Keil/MDK5 使用 ARMCLANG 和 uVision，当前 Linux 验证环境不提供 MDK。项目脚本统一从仓库根目录解析环境，目标架构为 Arm Cortex-M3，生产代码按 GNU C11 编译。

正常构建离线使用仓库内的固定依赖：

- `vendor/rt-thread/`
- `vendor/cmsis-core/`
- `vendor/stm32f1-cmsis/`
- `vendor/stm32f1-hal/`
- `vendor/rt-thread-stm32-drivers/`
- `vendor/patches/` 和 `vendor/manifest.md`

不得把系统中另一份 RT-Thread、HAL 或 CMSIS 隐式加入 include 路径。
所有公开脚本和 SCons 入口都固定使用仓库内的 `vendor/rt-thread/`；外部
`RTT_ROOT` 环境变量不会改变构建源。

## 支持的命令

所有公开操作都从仓库根目录执行，支持的入口只有以下五个脚本：

| 操作 | 命令 | 说明 |
| --- | --- | --- |
| 测试 | `tools/test.sh` | 先构建并运行主机测试 fixture，再构建固件，随后执行尺寸、链接所有权、布局和其他仓库静态门禁。可传一个测试可执行文件名进行聚焦运行。 |
| 构建 | `tools/build.sh` | 交叉构建固件并执行尺寸门禁；传入 `--cdb` 时同时生成编译数据库。 |
| 烧录 | `tools/flash.sh` | 先构建，再通过 `tools/openocd.sh` 自动识别 CMSIS-DAP/DAPLink/FireDAP、ST-Link 或 J-Link，使用 OpenOCD 校验、烧录并复位目标板；可用 `HUOKONG_PROBE=cmsis-dap|stlink|jlink` 手动选择。 |
| 调试 | `tools/debug.sh` | 对已有 ELF 启动 OpenOCD，并用 `arm-none-eabi-gdb` 或 `gdb-multiarch` 连接 `localhost:3333`。 |
| 控制台 | `tools/console.sh` | 以 115200 baud 打开 USART1 控制台；可传串口设备，默认 `/dev/ttyUSB0`。 |

Keil 工作流：在 Windows 的 Keil MDK5 中打开 `project/keil/huokong.uvprojx`，构建 `Debug` target。提交的 `.uvprojx`、manifest 和 scatter file 可直接使用；`.uvoptx/.uvguix` 是本地忽略的 IDE 元数据。

Foxglove debug 工作流需要安装 PC bridge 依赖：

```bash
python3 -m pip install pyserial foxglove-sdk
python3 tools/foxglove_debug_bridge.py --device /dev/ttyUSB0
```

然后在 Foxglove 中使用 Foxglove WebSocket connection type 连接
`ws://127.0.0.1:8765`。debug 构建不提供 USART1 控制台；正常构建保留
USART1 控制台和 USART2 遥测。

从 clean checkout 开始可直接执行 `tools/test.sh`。它的顺序是主机测试、固件构建、ELF/MAP 链接所有权检查、布局及其他静态门禁；主机测试失败时不会进入固件构建。单独使用 `tools/build.sh` 也会执行尺寸和链接所有权门禁。烧录和调试使用 STM32F1 OpenOCD 目标配置；烧录地址为 `0x08000000`。这些软件门禁不代表传感器、引脚、电气信号或通信时序已经完成硬件验证。

## 集中输出

全部生成物位于被忽略的 `build/`：

```text
build/scons/firmware/huokong.elf
build/scons/firmware/huokong.bin
build/scons/firmware/huokong.map
build/scons/host-tests/<测试名>
build/scons/compile_commands.json
build/keil/stm32f103c8/Debug/huokong.axf
build/keil/stm32f103c8/Debug/huokong.hex
build/keil/stm32f103c8/Debug/huokong.bin
build/keil/stm32f103c8/Debug/huokong.map
```

SCons 固件对象和数据库只写入 `build/scons/`，其中编译数据库固定为 `build/scons/compile_commands.json`；Keil 对象、listing、数据库和固件只写入 `build/keil/stm32f103c8/Debug/`。两个后端不共享中间产物，Linux 脚本只接受 SCons ELF/BIN。

## Kconfig 工作流

根 `Kconfig` 选择当前唯一的 `SOC_STM32F103C8`，并从最终的 `vendor/rt-thread/` 和 `src/platform/board/stm32f103c8/` 读取内核、SoC 与板级选项。`.config` 是选项结果，`rtconfig.h` 是供 C 构建使用的生成配置；两者必须与 Kconfig 选择保持同步并一同审查。

配置维护通过仓库已有的 Kconfig 界面完成，不把底层 SCons 或 Kconfig 调用作为新的用户入口。修改选项后应检查 `.config` 和 `rtconfig.h` 的差异，再使用上述测试、构建命令验证。第三方路径变化必须同步更新根 Kconfig、SCons 路径和 `vendor/manifest.md`，不能只改一处。

## 链接所有权与尺寸

`tools/build.sh` 对 `build/scons/firmware/huokong.elf` 自动执行尺寸和内存布局门禁：Flash 使用量为 `text + data`，不得超过 53248 字节；静态 SRAM 为 `data + bss`，不得超过 16384 字节。SCons 使用 GNU linker script，Keil 使用 `project/keil/stm32f103c8.sct`；两者都保持 64 KiB Flash（`0x08000000` 起）与 20 KiB SRAM（`0x20000000` 起）的精确范围。

最终验收还会从 ELF 强符号和 Map 对象记录交叉检查七个所有者：`EXTI15_10_IRQHandler` 属于 BMI088 平台适配器；`TIM2_IRQHandler` 与 `HAL_TIM_PeriodElapsedCallback` 属于单调时钟适配器；`DMA1_Channel7_IRQHandler`、`USART2_IRQHandler`、`HAL_UART_TxCpltCallback` 和 `HAL_UART_ErrorCallback` 属于遥测 UART 适配器。相关检查由统一测试和验收流程调用，不增加第六个公开命令。
