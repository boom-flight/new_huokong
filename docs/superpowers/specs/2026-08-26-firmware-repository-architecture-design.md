# 固件仓库架构设计

## 状态

本设计于 2026-08-26 在对话中获准。它对 `new_huokong` 仓库进行整体治理，并在不改变现有 STM32F103 BMI088 遥测固件外部行为的前提下重新划分模块边界。

## 背景

当前仓库根目录同时放置自研源码、板级支持、第三方依赖、测试、文档、开发工具、缓存、IDE 文件和构建产物。自研源码主要按 `algorithm`、`applications`、`drivers`、`protocol` 等技术类别划分，部分文件还混合了不相关职责：

- `bmi088_port.c` 同时管理 BMI088 总线、GPIO/EXTI、TIM2、USART2、DMA、中断和 HAL 回调。
- `imu_service_logic.c` 同时包含 IMU 策略、LED 策略、周期维护和遥测发送状态。
- `drivers/` 同时包含硬件相关实现和纯状态机。
- 历史 F427 文档、当前 F103 文档和新旧生成物容易被误认为同一套有效源码。

本设计借鉴 `rmcs_auto_aim_v2` 对运行时内核、可复用模块和通用工具的区分，同时补充 MCU/RTOS 固件必须具备的平台层和第三方依赖边界。不会照搬 ROS 2 Component、C++ PImpl、Concept、插件、共享库或递归源码扫描。

## 目标

- 让仓库根目录按产物类别组织，而不是平铺固件子系统。
- 按运行时职责和复用边界组织生产代码。
- 阻止 HAL、CMSIS 和 RT-Thread 依赖扩散到纯逻辑代码。
- 让每个外设、中断和 HAL 回调只有一个明确所有者。
- 为未来云台、电机、点火、CAN 和完整火控能力提供清晰扩展位置。
- 当前仍保持单 STM32F103C8 板卡和单固件镜像，不提前构建多产品框架。
- 让可在主机测试的代码一目了然，并让测试目录镜像生产模块。
- 使用固定版本的本地第三方快照，保证正常构建不依赖网络。
- 将全部生成物集中到唯一且被忽略的 `build/` 目录。
- 通过 SCons 的显式源文件和 include 路径约束依赖，而不只依靠命名约定。

## 非目标

- 不改变当前 32 字节遥测帧、引脚、采样率、线程优先级、校准、状态机和错误恢复行为。
- 本次迁移不增加云台、电机、点火、CAN 或新火控功能。
- 不增加多板卡、多 MCU 或多固件镜像支持。
- 不替换 SCons、Kconfig、RT-Thread、STM32 HAL 或 C11。
- 不改用 Git submodule，也不在正常构建时联网下载依赖。
- 不保留旧 `.script/` 命令兼容层，新命令统一放在 `tools/`。
- 不格式化或整理第三方源码。

## 架构模型

生产代码分为五类：

1. `app` 是装配入口，只负责初始化顺序、依赖装配和服务启动，不承载业务策略。
2. `kernel` 实现 IMU 采集、遥测调度、云台控制和火控等长期运行流程，拥有线程、状态机、数据发布和跨模块编排。
3. `modules` 放置输入输出明确的可复用实现，不依赖 RT-Thread、HAL、CMSIS、中断或具体外设。
4. `platform` 实现内核所需的板级、外设、中断和时间接口，是自研代码中唯一允许包含 HAL、CMSIS 和具体外设头文件的层。`kernel` 的私有运行时 `.c` 可以直接使用 RT-Thread，但不得把 RT-Thread 类型暴露到公共接口。
5. `utility` 只允许放置与业务无关的基础能力。名称或接口中出现 IMU、遥测、云台、机器人或火控语义的代码不得进入该目录。

编译期依赖方向如下：

```text
app ---------> kernel ---------> modules ---------> utility
 |                |
 |                +-----------> platform 抽象接口
 +----------------------------> platform 具体实现

platform 具体实现 -----------> modules 定义的端口契约（需要时）
platform 具体实现 -----------> utility
platform 具体实现 -----------> vendor

vendor 不得依赖自研代码
modules 和 utility 不得依赖 platform 或 vendor
platform 不得依赖 kernel
```

中断或 DMA 完成回调可以在运行时通知已注册回调或 RTOS 原语，但这种反向控制流不得形成反向源码依赖。

## 目标目录

```text
new_huokong/
├── src/
│   ├── app/
│   │   ├── main.c
│   │   └── SConscript
│   ├── kernel/
│   │   ├── imu/
│   │   ├── telemetry/
│   │   ├── gimbal/              # 实现功能时再创建
│   │   └── fire_control/        # 实现功能时再创建
│   ├── modules/
│   │   ├── attitude/
│   │   ├── devices/
│   │   │   └── bmi088/
│   │   ├── protocols/
│   │   │   └── imu_telemetry/
│   │   ├── timing/
│   │   └── transport/
│   ├── platform/
│   │   ├── board/
│   │   │   └── stm32f103c8/
│   │   ├── devices/
│   │   ├── time/
│   │   ├── transport/
│   │   └── rtos/                 # 出现可复用 RTOS 适配需求时再创建
│   └── utility/                 # 出现真正通用能力时再创建
├── tests/
│   ├── kernel/
│   │   ├── imu/
│   │   └── telemetry/
│   ├── modules/
│   │   ├── attitude/
│   │   ├── devices/bmi088/
│   │   ├── protocols/imu_telemetry/
│   │   ├── timing/
│   │   └── transport/
│   ├── fakes/
│   └── scripts/
├── vendor/
│   ├── rt-thread/
│   ├── cmsis-core/
│   ├── stm32f1-cmsis/
│   ├── stm32f1-hal/
│   ├── rt-thread-stm32-drivers/
│   ├── patches/
│   └── manifest.md
├── tools/
│   ├── build.sh
│   ├── test.sh
│   ├── flash.sh
│   ├── debug.sh
│   ├── console.sh
│   └── check-size.sh
├── docs/
│   ├── architecture/
│   ├── requirements/
│   ├── hardware/
│   ├── protocols/
│   ├── development/
│   ├── archive/stm32f427/
│   └── superpowers/
├── build/                       # 被忽略，唯一生成物目录
├── SConstruct
├── SConscript
├── Kconfig
├── rtconfig.h
├── rtconfig.py
├── .config
└── README.md
```

`.devcontainer/`、`.github/` 和 `.vscode/` 等约定目录仍保留在根目录，因为相关工具只会在这里自动发现它们。RT-Thread 或构建工具要求位于根目录的配置文件也暂时保留，除非工具能够原生支持移动且无需复制同步。不得为了展示未来结构创建空目录。

## 模块职责

### 应用入口

`src/app/main.c` 是唯一装配入口，启动平台初始化、IMU 内核和遥测内核。它不得包含传感器寄存器、协议编码、调度策略或错误恢复逻辑。

### IMU 内核

`src/kernel/imu/` 负责：

- IMU 服务线程和 RTOS 事件流程。
- 初始化、校准、运行和故障重试状态转换。
- 样本新鲜度、故障策略、快照发布和诊断。
- IMU 状态对应的 LED 策略。

姿态数学和 BMI088 寄存器访问放在可复用模块中。现有 `imu_service_logic` 按职责拆分，不能整文件机械搬迁。

### 遥测内核

`src/kernel/telemetry/` 负责：

- 200 Hz 遥测线程和调度。
- IMU 快照消费和双缓冲选择。
- 帧序号、丢帧粘滞状态和发送策略。
- 调用抽象遥测传输接口。

遥测内核依赖 IMU 快照契约、遥测编码器、可移植发送状态机和平台传输接口，不得包含 IMU 硬件端口头文件。

### 可复用模块

- `modules/attitude/`：姿态值类型、Mahony 和静态 IMU 校准。
- `modules/devices/bmi088/`：寄存器、复位、身份校验、配置、重试、突发读取、单位转换和抽象总线契约。
- `modules/protocols/imu_telemetry/`：固定 32 字节编码和 CRC。
- `modules/timing/`：16 位计时器扩展为 32 位时间戳的纯计算。
- `modules/transport/`：DMA 发送纯状态机。

模块按能力命名，不再使用 `algorithm`、`common` 等无法表达所有权的泛化名称。

### 平台层

`src/platform/board/stm32f103c8/` 管理时钟、引脚定义、HAL 配置、启动集成、链接脚本和板级 Kconfig。

现有 `bmi088_port` 拆成三个适配器：

- `platform/devices/bmi088_stm32.c`：SPI1、片选、BMI088 数据就绪 GPIO/EXTI 和 BMI088 总线契约实现。
- `platform/time/monotonic_clock_stm32.c`：TIM2 和微秒时钟硬件部分。
- `platform/transport/telemetry_uart_stm32.c`：USART2、DMA1 Channel 7、发送及 UART/DMA HAL 回调。

当前两个服务线程可以在 `kernel` 私有实现中直接使用 RT-Thread。只有出现多个内核共同需要、且必须通过 fake 测试的 RTOS 能力时，才创建 `platform/rtos/` 抽象，避免为静态线程和事件对象增加无必要的通用包装。

每个 IRQ 和 HAL 回调只能有一个所有者。`EXTI15_10_IRQn` 等共享向量由拥有该向量全部启用线路的适配器统一分发。平台适配器通过状态、事件、回调或函数表向上提供能力，不得包含内核头文件。

## 现有文件迁移映射

| 当前路径 | 目标路径或处理方式 |
| --- | --- |
| `applications/main.c` | `src/app/main.c` |
| `applications/imu_service.*` | `src/kernel/imu/imu_service.*` |
| `imu_service_logic.*` 中 IMU 策略 | `src/kernel/imu/imu_policy.*` |
| `imu_service_logic.*` 中遥测策略 | `src/kernel/telemetry/telemetry_policy.*` |
| `applications/telemetry_service.*` | `src/kernel/telemetry/telemetry_service.*` |
| `algorithm/mahony.*` | `src/modules/attitude/mahony.*` |
| `algorithm/imu_calibration.*` | `src/modules/attitude/imu_calibration.*` |
| `include/imu_types.h` | 拆入姿态类型和 IMU 快照契约的所有者目录 |
| `drivers/bmi088.*` | `src/modules/devices/bmi088/bmi088.*` |
| `drivers/timestamp_extender.*` | `src/modules/timing/timestamp_extender.*` |
| `drivers/telemetry_dma_state.*` | `src/modules/transport/dma_tx_state.*` |
| `bmi088_port.*` 中 BMI088 部分 | `src/platform/devices/bmi088_stm32.*` |
| `bmi088_port.*` 中 TIM2 部分 | `src/platform/time/monotonic_clock_stm32.*` |
| `bmi088_port.*` 中 USART2/DMA 部分 | `src/platform/transport/telemetry_uart_stm32.*` |
| `board/` | `src/platform/board/stm32f103c8/` |
| `rt-thread/` | `vendor/rt-thread/` |
| `packages/CMSIS-Core-latest/` | `vendor/cmsis-core/` |
| `packages/stm32f1_cmsis_driver-latest/` | `vendor/stm32f1-cmsis/` |
| `packages/stm32f1_hal_driver-latest/` | `vendor/stm32f1-hal/` |
| `libraries/HAL_Drivers/` | `vendor/rt-thread-stm32-drivers/` 并记录本地补丁 |
| `.script/` | 改为 `tools/` 下的新命令 |
| `demand/` | 按用途进入 `docs/requirements/` 和 `docs/hardware/` |
| 已删除的 `develop_ws/` | 不恢复；在 `docs/archive/stm32f427/README.md` 说明历史资料可从 Git 记录查阅 |
| 遥测协议文档 | `docs/protocols/imu-telemetry-v2.md` |
| `docs/hardware-acceptance.md` | `docs/hardware/acceptance.md` |
| `.o`、ELF、BIN、MAP、缓存和测试产物 | 仅在 `build/` 中重新生成 |

只有当新测试和构建声明准备完成后才进行重命名和拆分。该固件没有外部源码消费者或稳定 SDK，因此不增加旧路径转发头文件。

## 头文件和接口规则

- include 路径体现所有权，例如 `attitude/mahony.h`、`bmi088/bmi088.h` 和 `imu/imu_snapshot.h`。
- 模块只导出 `SConscript` 明确列出的公共头文件，私有头文件目录不加入消费者 include 路径。
- 公共模块头文件只能使用 C 标准类型和所属模块类型。
- HAL、CMSIS 和 RT-Thread 类型不得出现在 `kernel`、`modules` 或 `utility` 公共接口中；RT-Thread 仅允许出现在 `kernel` 私有运行时实现和 `platform` 实现中。
- 可变全局硬件句柄必须保持为平台适配器私有状态。
- 内核之间通过不可变快照或窄命令通信，不得访问另一个内核的内部状态。
- 新代码只有同时被至少两个非测试、非平台模块使用且不含业务词汇时，才允许进入 `utility`。

## 构建和产物

根 `SConstruct` 配置工具链和输出目录，然后进入固定列表中的自研和第三方 `SConscript`。根 `SConscript` 不再动态枚举目录。

每个自研 `SConscript` 必须：

- 显式列出生产源文件。
- 定义自身 include 路径。
- 只声明直接依赖。
- 将对象文件写入 `build/firmware/<layer>/<module>/`。
- 禁止使用 `Glob` 或递归扫描发现生产源码。

第三方源码仍由配置和显式清单选择。移动路径时必须同时更新 `RTT_ROOT`、Kconfig 路径、依赖检查、链接脚本、尺寸检查、OpenOCD、VS Code 和编译数据库生成逻辑。

标准输出位置为：

```text
build/firmware/huokong.elf
build/firmware/huokong.bin
build/firmware/huokong.map
build/host-tests/<测试名>
build/compile_commands.json
build/scons/
```

根 `.gitignore` 统一忽略 `build/` 和工具缓存。现有源码目录中的 `.o` 不参与迁移，只能在对应源码已安全进入新构建后作为被忽略的生成物清理。

## 第三方依赖管理

`vendor/manifest.md` 记录每项依赖的上游 URL、固定 commit、许可证、目标目录和补丁状态。第三方源码禁止格式化。

当前 RT-Thread STM32 通用驱动包含 EXTI 所有权本地修改。该差异必须记录在 `vendor/patches/` 和 manifest 中。为保证离线构建，仓库内快照可以包含已应用补丁，但必须能够由固定上游 commit 和记录的补丁集重现。

依赖升级必须与目录迁移分开提交，并单独执行固件构建、主机测试、尺寸比较、许可证和 manifest 检查。

## 文档和工具

- `README.md` 是用户入口，只描述当前 F103 固件。
- `docs/architecture/` 说明当前架构和依赖规则。
- `docs/requirements/` 保存有效需求和原始需求资料。
- `docs/hardware/` 保存板级资料和硬件验收记录。
- `docs/protocols/` 保存带版本的线协议。
- `docs/development/` 保存构建、测试、烧录、调试和依赖升级指南。
- `docs/archive/stm32f427/` 必须明确标记为历史资料。

支持的用户命令改为 `tools/test.sh`、`tools/build.sh`、`tools/flash.sh`、`tools/debug.sh` 和 `tools/console.sh`。VS Code 和容器任务只能调用这些命令，不能复制其内部逻辑。

面向用户和维护者的项目文档统一使用中文；代码标识符、协议字段、命令、路径和第三方原文保持其必要的英文形式。

## 测试策略

测试目录镜像生产代码所有权：

- Mahony 和校准测试进入 `tests/modules/attitude/`。
- BMI088 测试和 fake bus 进入 `tests/modules/devices/bmi088/` 与 `tests/fakes/`。
- 遥测编码测试进入 `tests/modules/protocols/imu_telemetry/`。
- 时间戳扩展测试进入 `tests/modules/timing/`。
- DMA 状态测试进入 `tests/modules/transport/`。
- IMU 策略测试进入 `tests/kernel/imu/`。
- 遥测策略测试进入 `tests/kernel/telemetry/`。
- 构建、尺寸和测试入口测试进入 `tests/scripts/`。

主机测试在没有 STM32 和 RT-Thread include 路径的环境中编译 `modules`。内核测试使用平台接口 fake。平台集成通过干净交叉构建、链接/Map 检查和现有硬件验收流程验证。

只有迁移前全部测试、干净固件构建、精确内存映射和尺寸门禁在迁移后继续通过，才能判定行为未变。未实际上板重跑的硬件验收项继续保持 Pending。

## 迁移顺序

1. 使用现有 F103 提交作为安全基线，并记录主机测试、固件构建、ELF 尺寸和 Map 符号。
2. 建立显式 SCons 结构并先把所有输出重定向到 `build/`，此时不移动生产源码。
3. 将纯模块与其测试逐个迁移：timing、transport、protocol、attitude、BMI088。
4. 拆分并迁移 IMU 与遥测内核策略，始终保持测试通过。
5. 将 `bmi088_port` 拆为设备、时间和遥测平台适配器，并从链接 Map 核验 IRQ/HAL 回调所有权。
6. 原子移动 board，并一起更新 linker、Kconfig、尺寸、调试和烧录路径。
7. 将固定依赖移入 `vendor/`，增加 manifest 和补丁记录，同时更新 SCons 与 Kconfig。
8. 迁移测试、工具和有效文档，增加 F427 历史索引说明，更新 README、VS Code 和容器入口。
9. 重新生成全部忽略产物，执行完整验证，确认旧目录不再包含生产源码或构建产物。

每一步结束时仓库都必须可构建。临时新旧目录不得同时保留有效 `SConscript`，避免重复符号。

## 风险控制

- **未提交工作丢失：** 实施时只基于已确认的 F103 提交，不清理或还原用户的并行改动。
- **旧生成物被误认作源码：** 迁移清单只来自已审查源码和构建声明，不从 `.o`、Map、ELF 或旧 compile commands 推断。
- **隐式 include 依赖：** 逐模块移除全局 include 泄漏，每次移动后立即编译。
- **IRQ/HAL 回调冲突：** 拆分前指定所有者，拆分后检查最终链接符号。
- **SCons/Kconfig 路径分叉：** `RTT_ROOT`、Kconfig `RTT_DIR` 和依赖检查必须在同一步修改。
- **文件拆分引入行为变化：** 保留协议黄金测试、状态策略测试、尺寸检查和 Map 对比，本次不增加功能。
- **utility 变成杂物目录：** 评审时强制执行“至少两个生产消费者且无业务语义”的规则。
- **过度设计：** 不创建空的云台、火控、多板或多镜像基础设施。

## 验收标准

- 自研生产代码只存在于 `src/`，第三方生产代码只存在于 `vendor/`。
- `.o`、ELF、BIN、MAP、编译数据库、SCons 数据库和主机测试程序均不出现在 `build/` 或明确工具缓存之外。
- 根生产构建和自研模块发现不包含目录扫描或递归源码 Glob。
- `modules` 和 `utility` 主机测试无需 RT-Thread、HAL 或 CMSIS include 路径即可编译。
- `kernel` 不直接包含 HAL/CMSIS；其公共接口不暴露 RT-Thread 类型，私有运行时实现可以直接使用 RT-Thread。
- 每个启用的 IRQ 和 HAL 回调在最终固件中只有一个所有者。
- 第三方来源和本地补丁全部有记录。
- F427 历史文档明确归档，当前 README 只链接有效 F103 文档。
- `tools/test.sh` 运行全部现有主机测试并通过。
- 干净执行 `tools/build.sh` 后只在 `build/firmware/` 生成 ELF、BIN、MAP，并通过 Flash/SRAM 尺寸门禁。
- 固件链接范围仍严格为 64 KB Flash 和 20 KB SRAM。
- 遥测协议和当前所有可观察固件行为保持不变。
