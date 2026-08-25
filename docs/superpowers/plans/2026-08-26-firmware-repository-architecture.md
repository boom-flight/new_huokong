# 固件仓库架构重构实施计划

> **供自动化执行者使用：** REQUIRED SUB-SKILL：必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans`，按任务逐项执行。所有步骤使用复选框跟踪。

**目标：** 将 STM32F103 固件仓库重构为 `app + kernel + modules + platform + vendor` 架构，并在不改变遥测协议和运行行为的前提下统一测试、工具、文档与构建产物管理。

**架构：** `app` 只负责装配，`kernel` 管理长期运行流程，`modules` 保持平台无关，`platform` 独占 HAL/RT-Thread/IRQ，`vendor` 保存固定第三方快照。迁移按可独立构建的提交推进，新旧路径不得同时保留有效构建入口。

**技术栈：** C11、RT-Thread 5.2.2、STM32 HAL、SCons、Kconfig、GNU Arm Embedded Toolchain、POSIX shell、主机 `cc`。

**设计文档：** `docs/superpowers/specs/2026-08-26-firmware-repository-architecture-design.md`

## 全局约束

- 实施基线必须包含 `843c3d8` 和 F103 收尾提交 `f2631b9`。
- 当前目标固定为 STM32F103C8、64 KB Flash、20 KB SRAM、单板单固件。
- 保持 USART2 32 字节遥测帧、200 Hz、CRC、引脚、线程优先级、状态机和恢复行为不变。
- 不增加云台、电机、点火、CAN 或新火控功能。
- 不升级 RT-Thread、CMSIS、HAL 或其他第三方依赖版本。
- 不格式化第三方源码。
- 所有自研生产源文件进入 `src/`，所有第三方源码进入 `vendor/`。
- 所有生成物只能进入根 `build/` 或明确忽略的工具缓存。
- 生产构建禁止使用目录扫描、`Glob` 或递归 Glob 发现自研源码。
- `modules` 主机测试不得获得 RT-Thread、HAL、CMSIS、board 或 platform include 路径。
- 用户和维护者文档统一使用中文；代码标识符、命令、路径和协议字段保留必要英文。
- 每个任务只暂存其“文件”段列出的路径；禁止使用 `git add -A`。
- 不运行 `git clean`、`git reset`、`git restore` 或 `git checkout --`。

---

### 任务 1：固定构建入口并集中生成物

**文件：**
- 新建：`tests/scripts/test_repository_layout.sh`
- 移动：`.script/*.sh` 到 `tools/*.sh`
- 修改：`SConstruct`
- 修改：`SConscript`
- 修改：`rtconfig.py`
- 修改：`tests/SConstruct`
- 修改：`tools/test.sh`
- 修改：`tools/build.sh`
- 修改：`tools/check-size.sh`
- 修改：`tools/flash.sh`
- 修改：`tools/debug.sh`（原 `.script/gdb.sh`）
- 修改：`tools/rebuild-index.sh`
- 修改：`.clangd`
- 修改：`.gitignore`
- 修改：`.vscode/tasks.json`
- 修改：`.vscode/launch.json`
- 修改：`.devcontainer/devcontainer.json`
- 修改：`README.md`

**接口：**
- 输入：当前 `algorithm/`、`applications/`、`board/`、`drivers/`、`packages/`、`protocol/` 源码布局。
- 输出：`tools/test.sh [测试名]`、`tools/build.sh [SCons 参数]`、`build/firmware/huokong.{elf,bin,map}`、`build/host-tests/test_*`、`build/compile_commands.json`。
- 本任务不移动生产源码，只固定当前构建发现顺序并改变工具与产物位置。

- [ ] **步骤 1：编写会失败的仓库布局测试**

`tests/scripts/test_repository_layout.sh` 的初始内容：

```sh
#!/usr/bin/env sh
set -eu

fail() {
    printf 'repository layout check failed: %s\n' "$1" >&2
    exit 1
}

grep -q 'os.listdir' SConscript && fail 'root SConscript scans directories'
grep -q "TARGET = os.path.join('build', 'firmware', 'huokong.'" SConstruct \
    || fail 'firmware target is outside build/firmware'
grep -q 'SConsignFile' SConstruct || fail 'firmware SCons database is not redirected'
grep -q 'build/firmware/huokong.map' rtconfig.py || fail 'map path is not centralized'

for command in build test flash debug console check-size; do
    test -x "tools/$command.sh" || fail "missing tools/$command.sh"
done

test ! -d .script || fail '.script compatibility directory still exists'
```

- [ ] **步骤 2：运行布局测试并确认红灯**

运行：`sh tests/scripts/test_repository_layout.sh`

预期：失败，至少报告根 `SConscript` 使用 `os.listdir`，或缺少 `tools/build.sh`。

- [ ] **步骤 3：将根 SConscript 改为当前路径的固定清单**

在尚未移动源码时使用明确列表，移除 `os.listdir()`：

```python
scripts = [
    'algorithm/SConscript',
    'applications/SConscript',
    'board/SConscript',
    'drivers/SConscript',
    'packages/SConscript',
    'protocol/SConscript',
]

env.Append(CPPDEFINES=['STM32F103xB'])
for script in scripts:
    objs.extend(SConscript(script))
```

不得在此步骤改变源文件顺序或宏。

- [ ] **步骤 4：把固件最终产物和 SCons 数据库重定向到 build**

`SConstruct` 使用：

```python
FIRMWARE_DIR = os.path.join('build', 'firmware')
TARGET = os.path.join(FIRMWARE_DIR, 'huokong.' + rtconfig.TARGET_EXT)
BIN_TARGET = os.path.join(FIRMWARE_DIR, 'huokong.bin')
MAP_TARGET = os.path.join(FIRMWARE_DIR, 'huokong.map')

SConsignFile('build/scons/firmware.dblite')
```

保留 `DoBuilding(TARGET, objs)`，随后从 `env['target']` 取得 Program 节点并显式生成 BIN：

```python
DoBuilding(TARGET, objs)
program = env['target']
binary = env.Command(BIN_TARGET, program,
                     '$OBJCOPY -O binary $SOURCE $TARGET')
env.SideEffect(MAP_TARGET, program)
env.Clean(program, [BIN_TARGET, MAP_TARGET])
Default(program, binary)
```

Environment 中增加 `OBJCOPY=rtconfig.OBJCPY`。`rtconfig.py` 删除从 `POST_ACTION` 生成根 `rtthread.bin` 的命令，Map 改为 `build/firmware/huokong.map`，链接脚本暂时仍使用 `board/linker_scripts/link.lds`。

- [ ] **步骤 5：把主机测试对象、程序和 SCons 数据库重定向到根 build**

`tests/SConstruct` 从仓库根通过 `scons -f tests/SConstruct` 调用。增加：

```python
SConsignFile('build/scons/host-tests.dblite')

base = Environment(
    CC='cc',
    CFLAGS=['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic'],
    LIBS=['m'],
)

def host_test(name, sources, include_paths):
    test_env = base.Clone()
    test_env.AppendUnique(CPPPATH=include_paths)
    objects = []
    for index, source in enumerate(sources):
        target = 'build/host-tests/obj/{}/{}'.format(name, index)
        objects.append(test_env.Object(target=target, source=source))
    return test_env.Program(
        target='build/host-tests/' + name,
        source=objects,
    )
```

本任务继续使用现有七个测试及旧源码路径，只改变输出位置。所有测试源列表保持显式。

- [ ] **步骤 6：移动工具脚本并原子更新调用方**

移动全部 `.script/*.sh` 到 `tools/`，其中 `gdb.sh` 改名 `debug.sh`。更新脚本内部互调、README、VS Code 和 devcontainer 引用。README 在本任务中同步改为中文，任务 11 再将详细内容拆到分类文档。

关键行为：

```sh
# tools/build.sh
source "$(dirname "$0")/env.sh"
scons -j"$(nproc)" "$@"
"$(dirname "$0")/check-size.sh" build/firmware/huokong.elf
```

```sh
# tools/test.sh 的构建和发现路径
scons -f tests/SConstruct -c
scons -f tests/SConstruct -j"$(nproc)"
discovery_dir=${TEST_DISCOVERY_DIR:-build/host-tests}
```

`tools/rebuild-index.sh` 保留现有 `--cdb` 意图，但删除把数据库复制到根目录的命令：

```sh
"$(dirname "$0")/build.sh" --cdb
```

`.clangd` 增加 `CompilationDatabase: build`。`tools/flash.sh` 使用 `build/firmware/huokong.bin`，`tools/debug.sh` 和 `.vscode/launch.json` 使用 `build/firmware/huokong.elf`。

- [ ] **步骤 7：运行任务级验证**

运行：

```sh
sh tests/scripts/test_repository_layout.sh
tools/test.sh
tools/build.sh --cdb
bash -n tools/*.sh
test -f build/firmware/huokong.elf
test -f build/firmware/huokong.bin
test -f build/firmware/huokong.map
test -f build/compile_commands.json
test ! -e rt-thread.elf
test ! -e rtthread.bin
test ! -e rt-thread.map
test ! -e compile_commands.json
```

预期：全部命令退出 0；Flash 不超过 53,248 字节，静态 SRAM 不超过 16,384 字节。

- [ ] **步骤 8：提交任务 1**

```sh
git add SConstruct SConscript rtconfig.py tests/SConstruct tests/scripts \
  .script tools .clangd .gitignore .vscode .devcontainer README.md
git commit -m "build: centralize firmware outputs and tools"
```

---

### 任务 2：迁移 timing 和 transport 纯模块

**文件：**
- 移动：`drivers/timestamp_extender.[ch]` 到 `src/modules/timing/`
- 移动并改名：`drivers/telemetry_dma_state.[ch]` 到 `src/modules/transport/dma_tx_state.[ch]`
- 新建：`src/modules/timing/SConscript`
- 新建：`src/modules/transport/SConscript`
- 移动：`tests/test_timestamp_extender.c` 到 `tests/modules/timing/`
- 移动并改名：`tests/test_telemetry_dma_state.c` 到 `tests/modules/transport/test_dma_tx_state.c`
- 修改：`drivers/SConscript`
- 修改：`SConscript`
- 修改：`tests/SConstruct`

**接口：**
- `uint32_t timestamp_extender_compose(uint16_t high_word, uint16_t counter, bool update_pending)` 保持不变。
- `telemetry_dma_state_t` 改名为 `dma_tx_state_t`。
- 全部 `telemetry_dma_state_*` 函数按相同后缀改为 `dma_tx_state_*`。

- [ ] **步骤 1：先修改两个测试的 include、符号和 SConstruct 源路径**

目标 include：

```c
#include "timing/timestamp_extender.h"
#include "transport/dma_tx_state.h"
```

- [ ] **步骤 2：运行两个测试并确认因目标源码不存在而失败**

运行：`scons -f tests/SConstruct test_timestamp_extender test_dma_tx_state`

预期：缺少 `src/modules/timing` 或 `src/modules/transport` 下的源文件/头文件。

- [ ] **步骤 3：移动实现并完成 DMA 状态机机械改名**

保持状态字段与状态转换不变，只执行以下一对一改名：

```text
telemetry_dma_state_t            -> dma_tx_state_t
telemetry_dma_state_reset        -> dma_tx_state_reset
telemetry_dma_state_reserve      -> dma_tx_state_reserve
telemetry_dma_state_cancel       -> dma_tx_state_cancel
telemetry_dma_state_complete     -> dma_tx_state_complete
telemetry_dma_state_async_error  -> dma_tx_state_async_error
telemetry_dma_state_busy         -> dma_tx_state_busy
telemetry_dma_state_take_failure -> dma_tx_state_take_failure
```

- [ ] **步骤 4：增加显式生产 SConscript 并移除旧驱动条目**

每个模块只显式列出一个 `.c`，目标根 `SConscript` 固定调用两个新脚本。旧 `drivers/SConscript` 删除这两个源文件但暂时保留 BMI088 和旧 port。

- [ ] **步骤 5：运行针对性和完整验证**

```sh
tools/test.sh test_timestamp_extender
tools/test.sh test_dma_tx_state
tools/test.sh
tools/build.sh
```

- [ ] **步骤 6：提交任务 2**

```sh
git add SConscript drivers/SConscript src/modules/timing src/modules/transport \
  tests/modules/timing tests/modules/transport tests/SConstruct \
  drivers/timestamp_extender.c drivers/timestamp_extender.h \
  drivers/telemetry_dma_state.c drivers/telemetry_dma_state.h \
  tests/test_timestamp_extender.c tests/test_telemetry_dma_state.c
git commit -m "refactor: isolate timing and transport modules"
```

---

### 任务 3：迁移姿态类型、Mahony、校准和遥测协议

**文件：**
- 移动：`include/imu_types.h` 到 `src/modules/attitude/imu_types.h`
- 移动：`algorithm/mahony.[ch]` 到 `src/modules/attitude/`
- 移动：`algorithm/imu_calibration.[ch]` 到 `src/modules/attitude/`
- 移动：`protocol/imu_telemetry.[ch]` 到 `src/modules/protocols/imu_telemetry/`
- 新建：两个目标模块的 `SConscript`
- 移动：对应三个测试及 `test_common.h` 到镜像目录
- 修改：所有 `#include "include/imu_types.h"` 使用者
- 修改：`SConscript`、`tests/SConstruct`、旧 `algorithm/SConscript`、旧 `protocol/SConscript`

**接口：**
- `imu_vec3f_t` 和 `imu_quatf_t` 的名称、字段、顺序和 `float` 类型保持不变。
- Mahony、校准、遥测编码公开函数签名保持不变。
- include 改为 `attitude/imu_types.h`、`attitude/mahony.h`、`attitude/imu_calibration.h`、`imu_telemetry/imu_telemetry.h`。

- [ ] **步骤 1：在测试中增加类型布局守卫并切换目标 include**

```c
_Static_assert(sizeof(imu_vec3f_t) == 3u * sizeof(float),
               "imu_vec3f_t layout changed");
_Static_assert(sizeof(imu_quatf_t) == 4u * sizeof(float),
               "imu_quatf_t layout changed");
```

- [ ] **步骤 2：修改 tests/SConstruct 指向目标路径并确认红灯**

运行：

```sh
scons -f tests/SConstruct test_mahony test_imu_calibration test_imu_telemetry
```

预期：因目标源文件尚未移动而失败。

- [ ] **步骤 3：移动姿态模块并更新全部值类型 include**

更新 BMI088、协议和暂未迁移的内核头文件，使其包含 `attitude/imu_types.h`。不得复制第二份类型定义。

- [ ] **步骤 4：移动协议模块并保留黄金帧行为**

不得改变以下常量：帧长 32、版本 1、payload 26、状态掩码 `0x01FF`、CRC-16/CCITT-FALSE。

- [ ] **步骤 5：运行针对性和完整验证**

```sh
tools/test.sh test_mahony
tools/test.sh test_imu_calibration
tools/test.sh test_imu_telemetry
tools/test.sh
tools/build.sh
```

- [ ] **步骤 6：提交任务 3**

```sh
git add SConscript algorithm protocol include src/modules/attitude \
  src/modules/protocols tests/modules/attitude \
  tests/modules/protocols tests/SConstruct applications drivers
git commit -m "refactor: organize attitude and telemetry protocol modules"
```

---

### 任务 4：迁移 BMI088 设备模块和 fake bus

**文件：**
- 移动：`drivers/bmi088.[ch]` 到 `src/modules/devices/bmi088/`
- 新建：`src/modules/devices/bmi088/SConscript`
- 移动：`tests/test_bmi088.c` 到 `tests/modules/devices/bmi088/`
- 移动：`tests/fake_bmi088_bus.[ch]` 到 `tests/fakes/`
- 修改：BMI088 使用者 include
- 修改：`drivers/SConscript`、`SConscript`、`tests/SConstruct`

**接口：**
- `bmi088_bus_t`、`bmi088_raw_sample_t`、错误枚举和全部公开函数签名保持不变。
- 新 include 路径为 `bmi088/bmi088.h`。
- 模块只能依赖 `attitude/imu_types.h` 和 C 标准库。

- [ ] **步骤 1：切换测试和 fake 的 include 与目标源路径**

- [ ] **步骤 2：运行 BMI088 测试并确认红灯**

运行：`scons -f tests/SConstruct test_bmi088`

预期：缺少 `src/modules/devices/bmi088/bmi088.c` 或新头文件。

- [ ] **步骤 3：移动 BMI088 实现及 fake，并建立显式 SConscript**

不得改动寄存器顺序、三次重试、ID 校验、配置 readback、little-endian 解码、比例和轴映射。

- [ ] **步骤 4：运行无平台 include 的主机编译和完整验证**

```sh
tools/test.sh test_bmi088
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -Isrc/modules -Isrc/modules/devices -Itests/fakes \
  tests/modules/devices/bmi088/test_bmi088.c \
  tests/fakes/fake_bmi088_bus.c \
  src/modules/devices/bmi088/bmi088.c -lm -o /tmp/test_bmi088
/tmp/test_bmi088
tools/test.sh
tools/build.sh
```

- [ ] **步骤 5：提交任务 4**

```sh
git add SConscript drivers src/modules/devices tests/modules/devices \
  tests/fakes tests/SConstruct applications
git commit -m "refactor: isolate BMI088 device module"
```

---

### 任务 5：拆分 IMU 和遥测内核策略

**文件：**
- 新建：`src/kernel/imu/imu_snapshot.h`
- 新建：`src/kernel/imu/imu_policy.[ch]`
- 新建：`src/kernel/telemetry/telemetry_policy.[ch]`
- 移动：`applications/imu_service.[ch]` 到 `src/kernel/imu/`
- 移动：`applications/telemetry_service.[ch]` 到 `src/kernel/telemetry/`
- 拆分：`tests/test_imu_service_logic.c` 到两个镜像测试
- 新建：两个 kernel `SConscript`
- 修改：`applications/SConscript`、`applications/main.c`、`SConscript`、`tests/SConstruct`
- 删除：迁移完成后的 `applications/imu_service_logic.[ch]`

**接口：**
- `imu_snapshot.h` 提供现有 `IMU_STATUS_*`、`imu_diagnostics_t`、`imu_snapshot_t`。
- `imu_service.h` 只导出：

```c
bool imu_service_init(void);
bool imu_snapshot_read(imu_snapshot_t *out);
void imu_service_record_telemetry_drop(void);
```

- `telemetry_policy.h` 提供：

```c
typedef struct {
    uint16_t next_sequence;
    uint32_t drops;
    bool drop_sticky;
} telemetry_attempt_state_t;

uint16_t telemetry_attempt_begin(telemetry_attempt_state_t *state);
void telemetry_attempt_dropped(telemetry_attempt_state_t *state);
void telemetry_attempt_queued(telemetry_attempt_state_t *state);
```

- 其余现有纯策略函数和签名移入 `imu_policy.[ch]`，本任务不重命名算法接口。

- [ ] **步骤 1：先拆分测试 main 和测试源路径**

`test_imu_policy.c` 保留原测试第 11-282、318-539 行对应测试；`test_telemetry_policy.c` 保留原第 284-316 行的 attempt/sequence 测试。两个测试分别拥有独立 `main()`。

- [ ] **步骤 2：运行两个新测试并确认红灯**

运行：`scons -f tests/SConstruct test_imu_policy test_telemetry_policy`

预期：缺少两个 policy 源文件。

- [ ] **步骤 3：抽取 snapshot 契约并拆分 policy**

`imu_policy.h` 不再包含 `imu_service.h`。`telemetry_policy.h` 只能包含 `<stdbool.h>` 和 `<stdint.h>`。

- [ ] **步骤 4：移动两个服务并更新 include**

本任务暂时继续调用旧 `bmi088_port`，以便在平台拆分前仍能独立构建。`telemetry_service.c` 删除 `imu_service_logic.h` 依赖并改用 `telemetry/telemetry_policy.h`。

- [ ] **步骤 5：运行针对性与完整验证**

```sh
tools/test.sh test_imu_policy
tools/test.sh test_telemetry_policy
tools/test.sh
tools/build.sh
```

- [ ] **步骤 6：提交任务 5**

```sh
git add SConscript applications src/kernel tests/kernel tests/SConstruct
git commit -m "refactor: separate IMU and telemetry kernels"
```

---

### 任务 6：拆分 STM32 平台适配器

**文件：**
- 新建：`src/platform/devices/bmi088_stm32.[ch]`
- 新建：`src/platform/time/monotonic_clock_stm32.[ch]`
- 新建：`src/platform/transport/telemetry_uart_stm32.[ch]`
- 新建：三个平台 `SConscript`
- 新建：`tests/scripts/test_link_owners.py`
- 修改：`src/kernel/imu/imu_service.c`
- 修改：`src/kernel/telemetry/telemetry_service.c`
- 修改：根 `SConscript`
- 修改：`tools/test.sh`
- 删除：`drivers/bmi088_port.[ch]` 和空的 `drivers/SConscript`

**接口：**

`bmi088_stm32.h`：

```c
#define BMI088_DRDY_EVENT_ACCEL (1u << 0)
#define BMI088_DRDY_EVENT_GYRO  (1u << 1)

typedef struct {
    uint32_t timestamp_us;
    uint32_t sequence;
} bmi088_drdy_latch_t;

typedef void (*bmi088_drdy_notify_fn)(void *context, uint32_t event_mask);

bool bmi088_stm32_init(bmi088_drdy_notify_fn notify, void *context);
void bmi088_stm32_deinit(void);
bmi088_bus_t bmi088_stm32_bus(void);
bmi088_drdy_latch_t bmi088_stm32_accel_latch(void);
bmi088_drdy_latch_t bmi088_stm32_gyro_latch(void);
```

`monotonic_clock_stm32.h`：

```c
bool monotonic_clock_stm32_init(void);
void monotonic_clock_stm32_deinit(void);
uint32_t monotonic_clock_stm32_now_us(void);
```

`telemetry_uart_stm32.h`：

```c
bool telemetry_uart_stm32_init(void);
void telemetry_uart_stm32_deinit(void);
bool telemetry_uart_stm32_try_start(const uint8_t *frame, size_t length);
bool telemetry_uart_stm32_busy(void);
bool telemetry_uart_stm32_take_failure(void);
```

- [ ] **步骤 1：先编写链接所有权检查并确认红灯**

`test_link_owners.py` 使用 `arm-none-eabi-nm -A --defined-only` 和 Map 文本，要求：

```text
EXTI15_10_IRQHandler           bmi088_stm32
TIM2_IRQHandler                monotonic_clock_stm32
HAL_TIM_PeriodElapsedCallback  monotonic_clock_stm32
DMA1_Channel7_IRQHandler       telemetry_uart_stm32
USART2_IRQHandler              telemetry_uart_stm32
HAL_UART_TxCpltCallback        telemetry_uart_stm32
HAL_UART_ErrorCallback         telemetry_uart_stm32
```

对当前构建运行：

```sh
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
```

预期：失败，因为当前所有符号仍来自 `bmi088_port`。

- [ ] **步骤 2：抽取 TIM2 单调时钟适配器**

移动 `htim2`、`timestamp_high_word`、TIM2 初始化/反初始化、时间读取、`TIM2_IRQHandler` 和 `HAL_TIM_PeriodElapsedCallback`。继续调用 `timestamp_extender_compose()`；未初始化时 `now_us()` 返回 0。

- [ ] **步骤 3：抽取 USART2/DMA 遥测适配器**

移动 `huart2`、`hdma_usart2_tx`、`dma_tx_state_t`、USART2/DMA 初始化/反初始化、发送函数、两个 IRQ 和两个 UART HAL 回调。保留长度必须等于 32 的检查和实例过滤。

- [ ] **步骤 4：抽取 BMI088 SPI/EXTI 适配器并建立通知桥**

移动 SPI1、片选、两个 DRDY latch 和 EXTI IRQ。回调调用顺序保持 accel 后 gyro。`imu_service.c` 提供一个私有 callback，把 event mask 转换为现有 RT-Thread event send；平台头文件不得暴露 `struct rt_event`。

- [ ] **步骤 5：更新服务初始化和反向回滚顺序**

初始化顺序：单调时钟、遥测 UART、IMU RTOS 对象、BMI088 EXTI，失败时反序释放。不得在 EXTI 启用前暴露无效 callback context。

- [ ] **步骤 6：运行完整测试、构建和链接所有权检查**

```sh
tools/test.sh
tools/build.sh
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
```

预期：七个关键符号各有一个强定义且对象所有者匹配；Flash/SRAM 门禁通过。

- [ ] **步骤 7：提交任务 6**

```sh
git add SConscript drivers src/platform src/kernel tests/scripts tools/test.sh
git commit -m "refactor: split STM32 platform adapters"
```

---

### 任务 7：迁移应用入口和板级支持

**文件：**
- 移动：`applications/main.c` 到 `src/app/main.c`
- 新建：`src/app/SConscript`
- 移动：`board/` 到 `src/platform/board/stm32f103c8/`
- 移动：`libraries/Kconfig` 到 `src/platform/board/stm32f103c8/soc/Kconfig`
- 修改：根 `SConscript`、`Kconfig`、`rtconfig.py`
- 修改：`tools/check-size.sh`
- 修改：`tests/scripts/test_check_size.sh`
- 修改：`.vscode` 中板级和链接路径

**接口：**
- 板时钟保持 HSE 8 MHz、PLL x9、APB1 36 MHz、APB2 72 MHz。
- 链接脚本保持 64 KB ROM 和 20 KB RAM。
- `STM32F103xB` 宏从根构建移入板级 `SConscript`。

- [ ] **步骤 1：先修改尺寸测试和布局测试期望新板路径**

期望链接脚本：`src/platform/board/stm32f103c8/linker_scripts/link.lds`。

- [ ] **步骤 2：运行尺寸和布局测试并确认红灯**

```sh
sh tests/scripts/test_check_size.sh
sh tests/scripts/test_repository_layout.sh
```

预期：旧 `board/` 路径不满足断言。

- [ ] **步骤 3：原子移动 app 和 board 并更新全部构建路径**

根 Kconfig 使用：

```kconfig
BSP_DIR := src/platform/board/stm32f103c8
RTT_DIR := rt-thread

source "$(RTT_DIR)/Kconfig"
rsource "$(BSP_DIR)/soc/Kconfig"
rsource "$(BSP_DIR)/Kconfig"
```

`rtconfig.py` 链接脚本改为目标路径。板级 Kconfig 暂时继续指向旧 `libraries/HAL_Drivers`，该路径在 vendor 任务中再原子修改。

- [ ] **步骤 4：运行 menuconfig 解析、测试和固件构建**

```sh
scons --menuconfig
tools/test.sh
tools/build.sh
tools/check-size.sh build/firmware/huokong.elf
```

若环境无法非交互退出 menuconfig，至少运行 Kconfig 解析命令并记录限制，不得修改 `.config` 的有效选项。

- [ ] **步骤 5：提交任务 7**

```sh
git add SConscript Kconfig rtconfig.py applications board libraries/Kconfig \
  src/app src/platform/board tools/check-size.sh tests/scripts .vscode
git commit -m "refactor: move application and board platform"
```

---

### 任务 8：将 RT-Thread 迁移到 vendor

**文件：**
- 移动：`rt-thread/` 到 `vendor/rt-thread/`
- 修改：`SConstruct`、`Kconfig`

**接口：**
- RT-Thread commit：`ddf52e2cdd977f14fc04035c88672ac204aec713`。
- `SConstruct` 的 `RTT_ROOT` 与 Kconfig 的 `RTT_DIR` 必须在同一提交切换。
- 不修改 RT-Thread 内部源码。

- [ ] **步骤 1：增加 RT-Thread vendor 路径静态断言并确认红灯**

扩展 `test_repository_layout.sh`：要求 `vendor/rt-thread/tools/building.py` 存在，并禁止根 `rt-thread/`。

运行：`sh tests/scripts/test_repository_layout.sh`

预期：失败，因为 RT-Thread 仍在旧目录。

- [ ] **步骤 2：移动 RT-Thread 并同步 SCons/Kconfig 根路径**

`SConstruct` 默认 `RTT_ROOT=vendor/rt-thread`；根 Kconfig `RTT_DIR := vendor/rt-thread`。移动后立即运行 `tools/test.sh && tools/build.sh`。

- [ ] **步骤 3：运行完整验证**

```sh
sh tests/scripts/test_repository_layout.sh
tools/test.sh
tools/build.sh
```

- [ ] **步骤 4：提交任务 8**

```sh
git add SConstruct Kconfig rt-thread vendor/rt-thread tests/scripts
git commit -m "refactor: move RT-Thread under vendor"
```

---

### 任务 9：迁移 CMSIS 和 STM32F1 HAL 固定快照

**文件：**
- 移动：`packages/CMSIS-Core-latest/` 到 `vendor/cmsis-core/`
- 移动：`packages/stm32f1_cmsis_driver-latest/` 到 `vendor/stm32f1-cmsis/`
- 移动：`packages/stm32f1_hal_driver-latest/` 到 `vendor/stm32f1-hal/`
- 移动并扩写：`packages/provenance.md` 到 `vendor/manifest.md`
- 修改：`SConstruct`、`SConscript`、`Kconfig`
- 删除：空的 `packages/` 及其旧构建入口

**接口：**
- CMSIS-Core commit：`39d8e01f0be84b83a8f11d33756e82ce1ef07a84`。
- STM32F1 CMSIS commit：`4d57f5017d2937f10d07331e90828d3a81f980b8`。
- STM32F1 HAL commit：`0b18f3336e7ef67e51080e72ae6805dba6cc7bb8`。
- direct HAL 的 SPI/TIM/TIM_EX 源继续显式加入。

- [ ] **步骤 1：增加三个快照路径和 manifest 静态断言并确认红灯**

要求三个目标目录和 `vendor/manifest.md` 存在，并禁止根 `packages/`。

运行：`sh tests/scripts/test_repository_layout.sh`

预期：失败，因为快照仍在 `packages/`。

- [ ] **步骤 2：移动三个快照并更新构建路径**

同步更新依赖存在性检查、根固定 `SConscript`、direct HAL 源和 include 路径。不得修改快照内容。

- [ ] **步骤 3：完成 manifest 中的三项依赖记录**

每项记录上游 URL、完整 commit、许可证、原上游路径和目标路径。SVD 记录原路径、commit `c65f8551e57c770344d229dcaa0bf838fa29aff4` 和 SHA-256 `1d92b65aaf397a18a599fb6a840812015ad379cdcc0cc3687f673f63e7445367`。

- [ ] **步骤 4：运行完整验证**

```sh
sh tests/scripts/test_repository_layout.sh
tools/test.sh
tools/build.sh
```

- [ ] **步骤 5：提交任务 9**

```sh
git add SConstruct SConscript Kconfig packages vendor/cmsis-core \
  vendor/stm32f1-cmsis vendor/stm32f1-hal vendor/manifest.md tests/scripts
git commit -m "refactor: move CMSIS and HAL snapshots under vendor"
```

---

### 任务 10：迁移 RT-Thread STM32 通用驱动并固化补丁

**文件：**
- 移动：`libraries/HAL_Drivers/` 到 `vendor/rt-thread-stm32-drivers/`
- 新建：`vendor/patches/rt-thread-stm32-drivers-exti15-10-owner.patch`
- 修改：`vendor/manifest.md`
- 修改：`SConstruct`
- 修改：`src/platform/board/stm32f103c8/Kconfig`
- 修改：`.config`、`rtconfig.h`（Kconfig 重新生成确实改变路径相关输出时）

**接口：**
- 上游仓库和 commit 与 RT-Thread 相同。
- `BSP_GPIO_EXTI15_10_EXTERNAL=y` 与 GPIO driver guard 必须一起保留。
- `HAL_GPIO_EXTI_Callback` 仍由通用 GPIO 驱动拥有，`EXTI15_10_IRQHandler` 仍由自研 BMI088 平台适配器拥有。

- [ ] **步骤 1：增加通用驱动、patch 和所有权静态断言并确认红灯**

要求 `vendor/rt-thread-stm32-drivers/` 和 patch 文件存在，禁止 `libraries/HAL_Drivers/`。

- [ ] **步骤 2：移动通用驱动并同步构建和 Kconfig 路径**

不得修改迁移目录中的第三方源码内容。

- [ ] **步骤 3：写入可重放的 EXTI 补丁**

patch 文件准确记录 `drv_gpio.c` 中：

```diff
+#ifndef BSP_GPIO_EXTI15_10_EXTERNAL
 void EXTI15_10_IRQHandler(void)
 {
     rt_interrupt_enter();
     HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
     HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);
     HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
     HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
     HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);
     HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
     rt_interrupt_leave();
 }
+#endif
```

补丁包含上游路径头和完整上下文，可以从固定 commit 对应的 STM32 driver 子树使用 `patch -p1` 重放。板级 Kconfig 改为引用 `vendor/rt-thread-stm32-drivers/drivers/Kconfig`。

- [ ] **步骤 4：完成 manifest 的通用驱动和补丁记录**

记录上游子路径 `bsp/stm32/libraries/HAL_Drivers`、Apache-2.0、固定 commit、patch 文件名、已应用状态、重放命令和补丁目的。

- [ ] **步骤 5：运行完整验证**

```sh
sh tests/scripts/test_repository_layout.sh
tools/test.sh
tools/build.sh
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
```

- [ ] **步骤 6：提交任务 10**

```sh
git add SConstruct src/platform/board libraries/HAL_Drivers \
  vendor/rt-thread-stm32-drivers vendor/patches vendor/manifest.md \
  .config rtconfig.h tests/scripts
git commit -m "refactor: move STM32 platform drivers under vendor"
```

---

### 任务 11：完成测试镜像、中文文档和硬件资料治理

**文件：**
- 移动：`tests/test_check_size.sh` 到 `tests/scripts/`
- 移动：`tests/test_test_runner.sh` 到 `tests/scripts/`
- 修改：`tools/test.sh`，纳入两个 shell 测试且避免自递归
- 移动：`demand/*.pdf`、`demand/*.xlsx` 到 `docs/hardware/source/`
- 移动并中文化：`docs/hardware-acceptance.md` 到 `docs/hardware/acceptance.md`
- 新建：`docs/architecture/overview.md`
- 新建：`docs/architecture/dependency-rules.md`
- 新建：`docs/development/build-test-debug.md`
- 新建：`docs/protocols/imu-telemetry-v1.md`
- 新建：`docs/requirements/firmware-behavior.md`
- 新建：`docs/archive/stm32f427/README.md`
- 重写：`README.md`

**接口：**
- README 只公布 `tools/test.sh`、`tools/build.sh`、`tools/flash.sh`、`tools/debug.sh`、`tools/console.sh`。
- 硬件验收状态保持“待上板验证”，不得因 host 测试改为通过。
- 协议文档必须与 32 字节黄金测试一致。

- [ ] **步骤 1：先扩展文档和旧路径静态检查**

在 `test_repository_layout.sh` 中禁止用户入口出现 `.script/`、根 `rt-thread.elf`、`rtthread.bin`、`rt-thread.map`、`tests/build`，并要求五份 active 中文文档存在。

- [ ] **步骤 2：运行布局检查并确认红灯**

运行：`sh tests/scripts/test_repository_layout.sh`

预期：缺少 active 分类文档。

- [ ] **步骤 3：迁移 shell 测试并纳入统一 test 命令**

`tools/test.sh` 在完成 C 测试后执行：

```sh
sh tests/scripts/test_check_size.sh
if [[ ${SKIP_TEST_RUNNER_SELF_TEST:-0} != 1 ]]; then
    SKIP_TEST_RUNNER_SELF_TEST=1 sh tests/scripts/test_test_runner.sh
fi
sh tests/scripts/test_repository_layout.sh
```

`test_test_runner.sh` 内部调用 `tools/test.sh` 时设置 `SKIP_TEST_RUNNER_SELF_TEST=1`，防止递归。

- [ ] **步骤 4：编写中文 active 文档并收敛 README**

README 只保留项目简介、五条命令、硬件/协议/架构文档链接和当前限制。详细接线、状态机、协议、构建说明分别进入目标文档。`docs/archive/stm32f427/README.md` 明确说明旧分析文档已从活动树删除，可通过 Git 历史查看，不恢复被删除文件。

- [ ] **步骤 5：迁移硬件原件和验收文档**

PDF/XLSX 保持二进制原样。`docs/hardware/source/` 通过中文 README 说明来源和用途。验收表全部保持未上板状态。

- [ ] **步骤 6：运行文档、测试和构建验证**

```sh
tools/test.sh
tools/build.sh
git grep -n '\.script/' -- README.md .vscode .devcontainer docs tests tools && exit 1 || true
git grep -nE 'rt-thread\.elf|rtthread\.bin|rt-thread\.map|tests/build' -- \
  README.md .vscode .devcontainer docs tools tests && exit 1 || true
```

- [ ] **步骤 7：提交任务 11**

```sh
git add README.md docs demand tests/scripts tools/test.sh .vscode .devcontainer
git commit -m "docs: add Chinese firmware documentation"
```

---

### 任务 12：架构门禁和最终验收

**文件：**
- 修改：`tests/scripts/test_repository_layout.sh`
- 修改：`tests/scripts/test_link_owners.py`
- 修改：`tools/test.sh`
- 修改：`.gitignore`
- 修改：`SConstruct`、根 `SConscript` 及 `src/**/SConscript`，消除最后的全局 include 泄漏和 Glob

**接口：**
- 不新增生产功能。
- 本任务只收紧自动化门禁并修复其发现的架构遗漏。

- [ ] **步骤 1：补齐最终仓库布局断言**

最终静态检查必须验证：

```text
自研生产 C/H 只在 src/
第三方生产 C/H 只在 vendor/
测试 C/H 只在 tests/
旧 algorithm、applications、drivers、protocol、include、board 不存在
根 SConscript 不使用 os.listdir
自研生产 SConscript 不使用 Glob
根目录无 .o、.elf、.bin、.map、.dblite、compile_commands.json
```

- [ ] **步骤 2：先运行最终门禁并记录所有失败项**

```sh
sh tests/scripts/test_repository_layout.sh
```

预期：若仍有旧入口或散落产物则失败；逐项修复，不得放宽断言来掩盖真实问题。

- [ ] **步骤 3：执行干净完整验证**

只清理生成目录：

```sh
rm -rf -- build
tools/test.sh
tools/build.sh --cdb
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
tools/check-size.sh build/firmware/huokong.elf
```

预期：

```text
全部 host C 测试通过
全部 shell/架构测试通过
huokong.elf、huokong.bin、huokong.map 位于 build/firmware
Flash <= 53248 字节
静态 SRAM <= 16384 字节
链接范围为 64 KB Flash / 20 KB SRAM
七个关键 IRQ/HAL callback 各有一个强定义且所有者正确
```

- [ ] **步骤 4：检查工作树和提交范围**

```sh
git status --short
git diff --check
git diff --stat f2631b9..HEAD
git log --oneline --decorate -12
```

确认没有第三方版本升级、无用户秘密、无忽略外生成物被误提交。

- [ ] **步骤 5：提交最终门禁**

```sh
git add tests/scripts tools/test.sh .gitignore SConstruct SConscript \
  src vendor
git commit -m "test: enforce firmware architecture boundaries"
```

- [ ] **步骤 6：提交后重新运行完整验证**

提交后再次执行步骤 3 的全部命令，以提交后的文件树作为最终证据。硬件验收继续报告为待上板，不得宣称硬件行为已经验证。
