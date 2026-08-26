# Linux 与 Keil 双构建隔离设计

## 目标

让 `new_huokong` 在保留当前 Linux/SCons + GNU Arm 工具链开发方式的同时，增加可由 Keil MDK5 默认 ARMCLANG 原生编译、下载和调试的 uVision 工程。

两个后端必须：

- 共享 `src/` 自研源码和 `vendor/` 固定第三方快照，不复制一份 Keil 专用源码；
- 使用各自的编译器、链接描述和调试配置；
- 让对象、依赖数据库、map、可执行文件和烧录文件完全隔离；
- 保持 STM32F103C8、64 KiB Flash、20 KiB SRAM、现有引脚、RT-Thread 配置、200 Hz 遥测协议和运行行为不变。

本阶段只支持当前固定的 `STM32F103xB / Cortex-M3` 目标，并为 Keil 提供 `Debug` 构建目标。后续增加芯片或构建 profile 时，必须新增对应 profile 的清单和输出目录，不能复用本目标的生成物目录。

## 方案概览

采用共享固件清单作为两个构建后端的源码边界。清单描述后端无关的信息：源码分组、源码路径、头文件搜索路径、公共宏、目标芯片和内存限制。SCons 和 Keil 工程生成器都读取这份清单。

工具链相关内容不放入共享清单：

- GCC 的编译参数、SCons 依赖数据库和 GNU linker script 继续由 SCons 后端维护；
- ARMCLANG 的编译选项、uVision target 设置、scatter file 和 Keil 调试选项由 Keil 后端维护；
- `tests/` 只用于主机测试，不进入任一固件源码清单。

Keil 工程文件提交为可直接打开的 `project/keil/huokong.uvprojx`。生成器用于在源码清单变化后重新生成它，而不是作为 Keil 用户打开工程前的必需步骤。用户在 Keil 中产生的 `.uvoptx`、`.uvguix` 和调试器配置不提交。

## 目录与职责

```text
project/
  firmware-manifest.json       # 两个固件后端共用的源码边界
  keil/
    huokong.uvprojx             # 可直接打开的 MDK5 工程
    stm32f103c8.sct             # ARMCLANG scatter file

src/                            # 自研生产代码，两个后端共用
vendor/                         # 固定第三方快照，两个后端共用
tests/                          # 主机测试，不进入 Keil 固件工程

build/
  scons/
    firmware/
      objects/
      huokong.elf
      huokong.bin
      huokong.map
    host-tests/
    compile_commands.json
    scons/
  keil/
    stm32f103c8/
      Debug/
        Objects/
        Listings/
        huokong.axf
        huokong.hex
        huokong.bin
        huokong.map
```

仓库根目录、`src/`、`vendor/` 和 `project/` 下不得写入固件对象或链接产物。`build/` 是唯一允许的固件构建输出根目录；两个后端只能写入自己的子目录。Keil 必需的用户级 IDE 元数据（`.uvoptx`、`.uvguix` 和调试器配置）是唯一例外：它们被忽略、不得提交，也不属于可交付固件产物。

## 共享清单

`project/firmware-manifest.json` 按以下逻辑分组维护当前完整固件：`board`、`kernel`、`modules`、`platform`、`rt-thread`、`cmsis`、`hal` 和 `drivers`。每个分组列出相对仓库根目录的源码路径，并声明该组使用的 include 路径和宏定义。

清单规则如下：

- 只允许 `src/` 和 `vendor/` 下的生产 C、头文件和启动汇编进入固件；
- 路径必须存在，不能使用递归目录扫描、通配符或隐式发现；
- 目标固定声明为 `STM32F103xB`、`Cortex-M3`、Flash 起始地址 `0x08000000`、长度 `64 KiB`、SRAM 起始地址 `0x20000000`、长度 `20 KiB`；
- 当前 `.config` 对应的 RT-Thread、HAL 和驱动源文件显式记录在清单中；
- 同一文件不能重复出现在多个分组；
- 清单顺序稳定，生成的 `.uvprojx` 必须是确定性的；
- 未来 Kconfig 产生不同固件组合时，应使用独立 profile 清单，而不是让 Keil 工程通过目录扫描自行改变源码集合。

SCons 根入口消费清单建立固件对象，同时保留 SCons 的依赖追踪和增量构建能力。Keil 生成器消费相同清单建立 uVision 的源码 group、include path 和公共 define。这样源码集合只有一个事实来源，后端差异不会污染模块代码。

## SCons 后端

现有 SCons 后端调整为使用 `build/scons/` 作为输出根：

- firmware target 为 `build/scons/firmware/huokong.elf`；
- BIN 和 MAP 位于同一 firmware 目录；
- 对象统一位于 `build/scons/firmware/objects/`；
- SCons 数据库位于 `build/scons/scons/firmware.dblite`；
- 主机测试位于 `build/scons/host-tests/`；
- 编译数据库位于 `build/scons/compile_commands.json`；
- GNU linker script 仍为 `src/platform/board/stm32f103c8/linker_scripts/link.lds`。

`tools/build.sh`、`tools/clean.sh`、`tools/flash.sh`、`tools/debug.sh`、尺寸检查、文档和测试中的旧 `build/firmware`、`build/host-tests` 引用必须同步更新。Linux 烧录和调试入口只接受 SCons ELF，不读取 Keil AXF。

## Keil 后端

`project/keil/huokong.uvprojx` 使用 ARMCLANG 原生构建，源码路径相对于工程文件保存为 Windows 可用的相对路径。工程包含清单中的全部生产源码，但不包含主机测试、SCons 文件或 GCC linker script。

Keil 配置至少包括：

- device 为 STM32F103C8/STM32F103xB 对应的 Cortex-M3 器件；
- ARM Compiler 6/ARMCLANG；
- C11 语言模式、与现有 GCC 构建等价的优化和调试信息；
- `STM32F103xB`、`USE_HAL_DRIVER`、`__RTTHREAD__` 及 `.config` 生成配置宏；
- `src/`、`vendor/rt-thread/include`、CMSIS、STM32F1 CMSIS、HAL 和驱动所需 include 路径；
- CMSIS ARMCLANG 启动文件 `vendor/stm32f1-cmsis/Source/Templates/arm/startup_stm32f103xb.s`；
- 由 `project/keil/stm32f103c8.sct` 完成链接；
- 输出目录、listing 目录、map 文件和 hex/bin 生成路径均位于 `build/keil/stm32f103c8/Debug/`。

Keil scatter file 必须表达与 GNU linker script 相同的内存边界：Flash 为 `0x08000000` 到 `0x0800FFFF`，SRAM 为 `0x20000000` 到 `0x20004FFF`，主栈预算为 `0x400`。启动符号、RT-Thread 初始化段、`.data` 加载地址、`.bss` 和中断向量表必须与现有 ARM 启动实现匹配。

新增 `tools/generate-keil-project.py` 读取共享清单并生成确定性的 `.uvprojx`。生成器遇到缺失路径、重复文件、非法清单路径、重复对象名或不支持的文件类型时立即失败，不产生部分工程文件。工程生成后可用 `--check` 模式只验证当前 `.uvprojx` 是否与清单一致。

自研代码出现 ARMCLANG 兼容问题时，优先使用 CMSIS 已有的 `cmsis_armclang.h` 适配层；必要的自研兼容定义集中放在 `project/keil/compiler_compat.h`，禁止在业务模块中散落 `#ifdef KEIL`。第三方快照不格式化、不升级、不直接修改。

## 产物隔离

Linux 和 Keil 不共享任何中间或最终产物：

| 产物 | SCons | Keil |
| --- | --- | --- |
| 对象 | `build/scons/firmware/objects/` | `build/keil/stm32f103c8/Debug/Objects/` |
| 依赖/数据库 | `build/scons/scons/` | `build/keil/stm32f103c8/Debug/Database/`，不提交 |
| 可执行文件 | `huokong.elf` | `huokong.axf` |
| 烧录文件 | `huokong.bin` | `huokong.hex`、`huokong.bin` |
| map | `build/scons/firmware/huokong.map` | `build/keil/stm32f103c8/Debug/huokong.map` |

`.gitignore` 必须覆盖两套目录以及 Keil 的 `RTE/`、`DebugConfig/`、`.uvoptx`、`.uvguix` 和日志缓存，但必须保留 `huokong.uvprojx`、scatter file、生成器和共享清单。Keil 工程配置应把对象、listing 和数据库路径指向 `build/keil/`；若 MDK 版本仍在工程目录生成用户 IDE 元数据，只能依靠忽略规则隔离，不能把这些文件当作构建输入。

一个后端的 clean 命令不得删除另一个后端目录。Keil Rebuild 由 uVision 管理其 `build/keil/` 子目录，Linux `tools/clean.sh` 只清理 `build/scons/`。

## 验证策略

### 静态一致性

新增布局测试验证：

- 清单中所有路径存在且位于允许的生产目录；
- `.uvprojx` 的源码集合与清单完全相同；
- Keil 工程不含 `tests/`、SCons 构建脚本或 GCC linker script；
- scatter file 的内存边界和目标芯片正确；
- 生成物目录只允许 `build/scons/` 或 `build/keil/`；
- 仓库中不存在根目录或源码目录下的 `.o`、`.obj`、`.elf`、`.axf`、`.bin`、`.hex`、`.map`；
- 两个后端没有引用对方的产物路径。

### Linux 验证

运行现有主机测试、SCons 固件构建、Flash/SRAM 尺寸检查、链接内存检查和 IRQ owner 检查。构建后确认 ELF、BIN、MAP 和 compile commands 全部位于 `build/scons/`。

### Keil 验证

在安装 MDK5/ARMCLANG 的 Windows 环境中使用以下等价命令构建：

```text
UV4.exe -b project/keil/huokong.uvprojx -j0
```

验证 AXF、HEX、BIN、MAP 均生成于 Keil 输出目录，并从 AXF/map 检查 Flash、SRAM、向量表和关键 IRQ/HAL callback 没有重复强定义。Linux 没有 MDK 时，Keil 实际编译标记为环境限制，不阻塞 Linux 测试；工程解析、清单一致性和目录门禁仍必须执行。

### 硬件验证

分别使用 SCons 和 Keil 产物在同一 STM32F103C8 上验证启动、BMI088 采样、USART2 200 Hz 遥测帧、协议 CRC 和调试连接。没有实际硬件或探针时，验收状态保持“待上板验证”，不能由主机或 Keil 工程解析测试替代。

## 实施边界

本设计不包含以下内容：

- 不引入 CMake 或用 Keil 调用 SCons；
- 不复制自研源码或第三方源码；
- 不把主机测试编译进固件；
- 不修改 RT-Thread、CMSIS、HAL 和 STM32 驱动快照；
- 不改变协议、线程优先级、引脚、中断所有权或运行时行为；
- 不在本阶段增加 Release、其他芯片或其他 IDE 工程。

完成标准是：共享清单、可直接打开的 ARMCLANG `.uvprojx`、独立 scatter file 和 SCons 路径迁移完成；Linux 测试与 SCons 构建通过；Keil 工程静态一致性通过；在可用 Windows/MDK 环境时 Keil 原生构建和硬件验证通过或明确记录为未执行。
