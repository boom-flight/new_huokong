# 固件仓库并行重构实施计划

> **供 agentic worker 使用：** REQUIRED SUB-SKILL：使用 `superpowers:subagent-driven-development` 执行本计划。Controller 在明确标注的 fan-out 步骤使用 `superpowers:dispatching-parallel-agents`，在执行时使用 `superpowers:using-git-worktrees` 创建隔离工作区。所有步骤使用复选框跟踪。

**目标：** 从已完成的任务 1 至 4 基线继续实施 `app + kernel + modules + platform + vendor` 架构，通过任务内 fan-out/fan-in 和 vendor/docs 双 lane 并行完成剩余迁移，同时保持 `master` 只包含可构建、已审查的任务提交。

**架构：** Worker 在互不重叠的 linked worktree 中只创建其拥有的组件文件；Integrator 从同一基线使用 `git cherry-pick -n` 汇入 worker 结果，完成共享构建接线和原子任务提交。任务 7 后，vendor lane 顺序执行依赖迁移，docs lane 并行编写无共享路径的中文文档，最后在任务 11 汇合。

**技术栈：** C11、RT-Thread 5.2.2、STM32 HAL、SCons、Kconfig、GNU Arm Embedded Toolchain、POSIX shell、Git linked worktree、OpenCode subagent。

**规格：**
- `docs/superpowers/specs/2026-08-26-firmware-repository-architecture-design.md`
- `docs/superpowers/specs/2026-08-26-parallel-subagent-execution-design.md`
- 原始精确任务要求：`docs/superpowers/plans/2026-08-26-firmware-repository-architecture.md`

## 全局约束

- 基线必须包含 `843c3d8`、`f2631b9`、任务 1 至 4 的已审查提交和并行设计提交 `f3f2690`。
- 当前目标固定为 STM32F103C8、64 KB Flash、20 KB SRAM、单板单固件。
- 保持 USART2 32 字节遥测帧、200 Hz、CRC、引脚、线程优先级、状态机和恢复行为不变。
- 不增加云台、电机、点火、CAN 或新火控功能。
- 不升级或格式化 RT-Thread、CMSIS、HAL 和 STM32 通用驱动。
- 自研生产 C/H 最终只在 `src/`，第三方生产 C/H 最终只在 `vendor/`，测试 C/H 只在 `tests/`。
- 所有生成物只能进入各 worktree 根 `build/` 或明确忽略的工具缓存。
- 生产构建禁止通过目录扫描、`Glob` 或递归 Glob 发现自研源码。
- `modules` 生产对象和主机测试不得获得 RT-Thread、HAL、CMSIS、board、platform 或旧源码目录 include 路径。
- `kernel` 公共头不得暴露 RT-Thread/HAL/CMSIS 类型；私有运行时 `.c` 可以使用 RT-Thread。
- 每个 IRQ 和 HAL callback 只能有一个强定义所有者。
- 用户和维护者文档使用中文；代码标识符、路径、命令和协议字段保留必要英文。
- Worker 不修改共享构建文件、不扩大文件所有权、不派发子代理。
- Integrator 独占共享构建文件、旧路径删除和任务级完整验证。
- 不运行 `git clean`、`git reset`、`git restore` 或 `git checkout --`。
- 不使用 `git add -A`；每个提交显式暂存允许路径。
- 当前 `master` 中用户并行修改的 `README.md`、`.vscode/launch.json`、`tools/debug.sh`、`tools/flash.sh`、`tools/setup-ubuntu.sh`、`tools/test.sh`、`tests/test_debug_probe.sh`、`tools/openocd.sh` 不得被覆盖、还原或误提交。
- Controller 不手工修复 worker 或 reviewer finding；修复由对应 implementer/integrator subagent 完成并复审。

## 文件结构和职责

| 单元 | 拥有内容 | 不拥有内容 |
| --- | --- | --- |
| Task 5 IMU worker | IMU snapshot、policy、service 目标文件和 IMU policy 测试 | 根 SCons、旧 applications 文件删除、telemetry 文件 |
| Task 5 Telemetry worker | telemetry policy、service 目标文件和 telemetry policy 测试 | 根 SCons、旧 applications 文件删除、IMU policy 文件 |
| Task 5 Integrator | kernel SConscript、根/测试接线、旧 applications 收敛 | 用户工具和文档改动 |
| Task 6 BMI worker | `bmi088_stm32.[ch]` | 旧 port、service、根 SCons |
| Task 6 Clock worker | `monotonic_clock_stm32.[ch]` | 旧 port、service、根 SCons |
| Task 6 UART worker | `telemetry_uart_stm32.[ch]` | 旧 port、service、根 SCons |
| Task 6 Integrator | 三个平台 SConscript、service 接线、旧 port 删除、owner 测试 | board 移动、vendor 移动 |
| Vendor lane | 任务 8 至 10 的第三方路径、manifest、patch 和构建/Kconfig 路径 | active 文档、README、用户工具改动 |
| Docs lane | active 分类文档、archive 索引、hardware assets | README、tools、tests、VS Code、构建文件 |
| Task 11 Integrator | README、统一 shell 测试/工具入口、用户改动兼容集成 | vendor 快照内容 |

---

### 任务 1：关闭任务 4 审查并建立并行隔离基础

**文件：**
- 修改：`.gitignore`
- 读取：任务 4 brief、report、fix diff 和 SDD ledger
- 不修改：任务 4 已提交生产文件

**接口：**
- 输入：`f41e3a9 fix: scope BMI088 consumer include path` 和 `f3f2690 docs: add parallel subagent execution design`。
- 输出：任务 4 clean review verdict、忽略的 `.worktrees/` 根、可供所有 worker 使用的已验证基线 SHA。

- [ ] **步骤 1：初始化本计划专属 SDD workspace 和 ledger**

```sh
SDD_DIR=/home/ubuntu/.cache/opencode/packages/superpowers@git+https:/github.com/obra/superpowers.git/node_modules/superpowers/skills/subagent-driven-development
PLAN=docs/superpowers/plans/2026-08-26-parallel-firmware-repository-architecture.md
WORKSPACE=$("$SDD_DIR/scripts/sdd-workspace" "$PLAN")
perl -pe 's/^(### )任务 ([0-9]+)：/$1Task $2:/' "$PLAN" > "$WORKSPACE/plan-normalized.md"
```

使用 `apply_patch` 创建 `$WORKSPACE/progress.md`，首行必须是：

```text
# SDD ledger — plan: docs/superpowers/plans/2026-08-26-parallel-firmware-repository-architecture.md
```

Ledger 第二段记录原计划 ledger 路径和任务 1 至 3 已完成、任务 4 待复审；不得把旧计划 workspace 当成本计划 workspace。

- [ ] **步骤 2：重新执行任务 4 fix round 1 scoped re-review**

使用现有文件：

```text
.superpowers/sdd/2026-08-26-firmware-repository-architecture/task-4-brief.md
.superpowers/sdd/2026-08-26-firmware-repository-architecture/task-4-report.md
.superpowers/sdd/2026-08-26-firmware-repository-architecture/review-1a030a7..f41e3a9.diff
```

Finding 必须逐项 verdict：`src/modules/devices` 只能出现在 BMI088 生产对象和五个真实 consumer 的 CDB 项中，不得泄漏到其他对象。预期：ADDRESSED，无新 Critical/Important breakage。

- [ ] **步骤 3：记录任务 4 完成**

向原 SDD ledger 追加：

```text
Task 4: fix round 1/5 (1 addressed, 0 open — device include 已限制到真实消费者；commits 1a030a7..f41e3a9)
Task 4: complete (commits f7f5f61..f41e3a9, review clean)
```

- [ ] **步骤 4：为 linked worktree 编写失败检查**

运行：

```sh
git check-ignore -q .worktrees/probe
```

预期：退出 1，因为 `.worktrees/` 尚未忽略。

- [ ] **步骤 5：忽略并验证 worktree 根**

在 `.gitignore` 增加：

```gitignore
.worktrees/
```

运行：

```sh
git check-ignore -q .worktrees/probe
git diff --check -- .gitignore
```

预期：两条命令退出 0。

- [ ] **步骤 6：提交隔离基础**

```sh
git add -- .gitignore
git commit -m "build: ignore parallel worktrees"
```

- [ ] **步骤 7：记录并验证 worker 基线**

```sh
git merge-base --is-ancestor 843c3d8 HEAD
git merge-base --is-ancestor f2631b9 HEAD
git merge-base --is-ancestor f41e3a9 HEAD
git rev-parse HEAD
```

预期：三个祖先检查退出 0；最后一行记录为 `PARALLEL_BASE`。

---

### 任务 2：并行拆分 IMU 和遥测 kernel

**Worker 文件：**
- IMU worker 新建：`src/kernel/imu/imu_snapshot.h`
- IMU worker 新建：`src/kernel/imu/imu_policy.[ch]`
- IMU worker 新建：`src/kernel/imu/imu_service.[ch]`
- IMU worker 新建：`tests/kernel/imu/test_imu_policy.c`
- Telemetry worker 新建：`src/kernel/telemetry/telemetry_policy.[ch]`
- Telemetry worker 新建：`src/kernel/telemetry/telemetry_service.[ch]`
- Telemetry worker 新建：`tests/kernel/telemetry/test_telemetry_policy.c`

**Integrator 文件：**
- 新建：`src/kernel/imu/SConscript`
- 新建：`src/kernel/telemetry/SConscript`
- 修改：`SConscript`、`tests/SConstruct`、`applications/SConscript`、`applications/main.c`
- 删除：迁移后的 `applications/imu_service.[ch]`、`applications/telemetry_service.[ch]`、`applications/imu_service_logic.[ch]`

**接口：**
- `imu_snapshot.h` 提供原 `IMU_STATUS_*`、`imu_diagnostics_t`、`imu_snapshot_t`。
- `imu_service.h` 只导出 `bool imu_service_init(void)`、`bool imu_snapshot_read(imu_snapshot_t *out)`、`void imu_service_record_telemetry_drop(void)`。
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

- Task 5 Integrator 暂时保留旧 `bmi088_port` 调用，平台拆分只在任务 3 进行。

- [ ] **步骤 1：创建三个同基线 worktree**

先验证父目录并创建：

```sh
ls -ld .
mkdir -p .worktrees
git worktree add -b sdd/task-5-imu-worker .worktrees/task-5-imu-worker PARALLEL_BASE
git worktree add -b sdd/task-5-telemetry-worker .worktrees/task-5-telemetry-worker PARALLEL_BASE
git worktree add -b sdd/task-5-integration .worktrees/task-5-integration PARALLEL_BASE
```

运行：

```sh
for tree in \
  .worktrees/task-5-imu-worker \
  .worktrees/task-5-telemetry-worker \
  .worktrees/task-5-integration; do
    test "$(git -C "$tree" rev-parse HEAD)" = "$PARALLEL_BASE"
done
```

预期：退出 0。

- [ ] **步骤 2：同时派发两个 worker subagent**

在同一条 Controller 消息中发出两个 `task` 调用。

IMU worker brief 必须包含以下规则：

```text
只写 IMU worker 文件；旧 applications 文件只读。
把原 test_imu_service_logic.c 第 11-282、318-539 行对应测试移入独立 main。
imu_policy.h 不包含 imu_service.h。
service 私有实现可以使用 RT-Thread，公共头不能暴露 RT-Thread 类型。
不得创建 SConscript、修改 tests/SConstruct 或删除旧文件。
```

Telemetry worker brief 必须包含：

```text
只写 Telemetry worker 文件；旧 applications 文件只读。
把原 test_imu_service_logic.c 第 284-316 行 attempt/sequence 测试移入独立 main。
telemetry_policy.h 只能包含 <stdbool.h> 和 <stdint.h>。
telemetry_attempt_begin 每次返回并递增序号；dropped 增加 drops 并设置 sticky；queued 清除 sticky，保持原行为。
不得创建 SConscript、修改 tests/SConstruct 或删除旧文件。
```

- [ ] **步骤 3：每个 worker 记录 RED/GREEN**

IMU worker 在新 policy 源不存在时运行并记录 RED：

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -Isrc/kernel -Isrc/modules -Isrc/modules/devices \
  tests/kernel/imu/test_imu_policy.c \
  src/kernel/imu/imu_policy.c -lm -o /tmp/test_imu_policy
```

Telemetry worker 在新 policy 源不存在时运行并记录 RED：

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -Isrc/kernel \
  tests/kernel/telemetry/test_telemetry_policy.c \
  src/kernel/telemetry/telemetry_policy.c \
  -o /tmp/test_telemetry_policy
```

实现后重复命令并运行对应 `/tmp/test_*`，预期退出 0。

- [ ] **步骤 4：worker 分别提交**

IMU worker：

```sh
git add -- src/kernel/imu tests/kernel/imu
git commit -m "refactor: prepare IMU kernel"
```

Telemetry worker：

```sh
git add -- src/kernel/telemetry tests/kernel/telemetry
git commit -m "refactor: prepare telemetry kernel"
```

两个 worker 报告提交 SHA、测试证据和越界依赖；Controller 记录：

Controller 记录：

```sh
IMU_WORKER_SHA=$(git -C .worktrees/task-5-imu-worker rev-parse HEAD)
TELEMETRY_WORKER_SHA=$(git -C .worktrees/task-5-telemetry-worker rev-parse HEAD)
```

然后分别运行：

```sh
git diff --name-only "$PARALLEL_BASE..$IMU_WORKER_SHA"
git diff --name-only "$PARALLEL_BASE..$TELEMETRY_WORKER_SHA"
```

预期：两个文件集合互不重叠，并且都只包含任务 2 声明的 worker 路径。

- [ ] **步骤 5：Integrator 无提交汇入 worker 结果**

在 task-5 integration worktree：

```sh
git cherry-pick -n "$IMU_WORKER_SHA"
git cherry-pick -n "$TELEMETRY_WORKER_SHA"
git diff --check
```

预期：无冲突标记、无共享文件覆盖、无空白错误。

- [ ] **步骤 6：Integrator 完成原子迁移和构建声明**

创建两个 kernel `SConscript`，显式列出 service 和 policy 源。更新根 `SConscript`、`applications/main.c` 和 consumer include。`tests/SConstruct` 增加 `test_imu_policy`、`test_telemetry_policy` 显式目标。删除旧逻辑和已迁移 service 文件，最终不得存在重复定义或转发头。

- [ ] **步骤 7：运行任务 5 完整验证**

```sh
tools/test.sh test_imu_policy
tools/test.sh test_telemetry_policy
tools/test.sh
tools/build.sh --cdb
sh tests/scripts/test_repository_layout.sh
```

检查 CDB：两个 policy 对象不得包含 HAL/CMSIS/board/platform include；service 公共头不得包含 `rtthread.h` 或 `struct rt_`。

- [ ] **步骤 8：形成任务级原子提交**

```sh
git add -- SConscript applications src/kernel tests/kernel tests/SConstruct
git commit -m "refactor: separate IMU and telemetry kernels"
```

- [ ] **步骤 9：任务级审查和集成**

记录并以精确范围生成 review package：

```sh
TASK5_SHA=$(git rev-parse HEAD)
```

以 `PARALLEL_BASE..$TASK5_SHA` 执行规格与质量双 verdict。修复循环只恢复 Task 5 Integrator。Review clean 后将任务提交 cherry-pick 到 `master`，记录新的 `TASK5_BASE`。

---

### 任务 3：并行拆分三个 STM32 平台适配器

**Worker 文件：**
- BMI worker 新建：`src/platform/devices/bmi088_stm32.[ch]`
- Clock worker 新建：`src/platform/time/monotonic_clock_stm32.[ch]`
- UART worker 新建：`src/platform/transport/telemetry_uart_stm32.[ch]`

**Integrator 文件：**
- 新建：三个平台 `SConscript`
- 新建：`tests/scripts/test_link_owners.py`
- 修改：两个 kernel service、根 `SConscript`、`tools/test.sh`
- 删除：`drivers/bmi088_port.[ch]` 和空 `drivers/SConscript`

**接口：**
- BMI 公开接口：

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

- Clock 公开接口：

```c
bool monotonic_clock_stm32_init(void);
void monotonic_clock_stm32_deinit(void);
uint32_t monotonic_clock_stm32_now_us(void);
```

- UART 公开接口：

```c
bool telemetry_uart_stm32_init(void);
void telemetry_uart_stm32_deinit(void);
bool telemetry_uart_stm32_try_start(const uint8_t *frame, size_t length);
bool telemetry_uart_stm32_busy(void);
bool telemetry_uart_stm32_take_failure(void);
```

- 初始化顺序固定为 clock、UART、IMU RTOS objects、BMI EXTI；失败反序释放。
- 七个 IRQ/HAL callback owner 固定为：

```text
EXTI15_10_IRQHandler           bmi088_stm32
TIM2_IRQHandler                monotonic_clock_stm32
HAL_TIM_PeriodElapsedCallback  monotonic_clock_stm32
DMA1_Channel7_IRQHandler       telemetry_uart_stm32
USART2_IRQHandler              telemetry_uart_stm32
HAL_UART_TxCpltCallback        telemetry_uart_stm32
HAL_UART_ErrorCallback         telemetry_uart_stm32
```

- [ ] **步骤 1：从 TASK5_BASE 创建四个 worktree**

```sh
git worktree add -b sdd/task-6-bmi-worker .worktrees/task-6-bmi-worker TASK5_BASE
git worktree add -b sdd/task-6-clock-worker .worktrees/task-6-clock-worker TASK5_BASE
git worktree add -b sdd/task-6-uart-worker .worktrees/task-6-uart-worker TASK5_BASE
git worktree add -b sdd/task-6-integration .worktrees/task-6-integration TASK5_BASE
```

四个 HEAD 必须等于 `TASK5_BASE`。

- [ ] **步骤 2：同时派发三个 adapter worker**

BMI worker 只提取 SPI1、片选、DRDY latch、EXTI IRQ、accel 后 gyro 通知顺序和 `bmi088_bus_t` 实现。Clock worker 只提取 TIM2、`timestamp_high_word`、时间读取、TIM2 IRQ 和 period callback。UART worker 只提取 USART2、DMA1 Channel 7、`dma_tx_state_t`、32 字节发送检查、IRQ 和 UART callbacks。

三个 worker 都不得修改旧 port、kernel service、根构建文件或 owner 测试。

- [ ] **步骤 3：worker 对新翻译单元做聚焦交叉编译**

每个 worker 使用旧 port 的 CDB 命令作为已验证 flags/include 模板，将输入源和输出对象替换为自身文件：

```sh
for source in \
  src/platform/devices/bmi088_stm32.c \
  src/platform/time/monotonic_clock_stm32.c \
  src/platform/transport/telemetry_uart_stm32.c; do
SOURCE="$source" python3 <<'PY'
import json
import os
import pathlib
import shlex
import subprocess

source = pathlib.Path(os.environ['SOURCE'])
entries = json.loads(pathlib.Path('build/compile_commands.json').read_text())
entry = next(item for item in entries if item['file'] == 'drivers/bmi088_port.c')
args = shlex.split(entry['command'])
args[args.index('-o') + 1] = '/tmp/' + source.stem + '.o'
source_index = next(
    index for index, arg in enumerate(args)
    if arg == 'drivers/bmi088_port.c' or arg.endswith('/drivers/bmi088_port.c')
)
args[source_index] = str(source)
subprocess.run(args, check=True)
PY
done
```

每个 worker 只运行循环中属于自己的一个 source；若 worktree 尚无 CDB，先运行 `tools/build.sh --cdb`。预期分别生成 `/tmp/bmi088_stm32.o`、`/tmp/monotonic_clock_stm32.o`、`/tmp/telemetry_uart_stm32.o` 且退出 0。

- [ ] **步骤 4：worker 分别提交**

```sh
git add -- src/platform/devices && git commit -m "refactor: prepare BMI088 STM32 adapter"
git add -- src/platform/time && git commit -m "refactor: prepare STM32 monotonic clock"
git add -- src/platform/transport && git commit -m "refactor: prepare STM32 telemetry UART"
```

命令分别在对应 worktree 执行。Controller 检查三个 diff 文件集合互不重叠。

Controller 记录：

```sh
BMI_WORKER_SHA=$(git -C .worktrees/task-6-bmi-worker rev-parse HEAD)
CLOCK_WORKER_SHA=$(git -C .worktrees/task-6-clock-worker rev-parse HEAD)
UART_WORKER_SHA=$(git -C .worktrees/task-6-uart-worker rev-parse HEAD)
```

- [ ] **步骤 5：Integrator 汇入并先写 owner RED**

```sh
git cherry-pick -n "$BMI_WORKER_SHA"
git cherry-pick -n "$CLOCK_WORKER_SHA"
git cherry-pick -n "$UART_WORKER_SHA"
```

创建 `tests/scripts/test_link_owners.py` 后，对尚未接线的当前固件运行：

```sh
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
```

预期：失败并报告 owner 仍为 `bmi088_port` 或目标 adapter 尚未链接。

- [ ] **步骤 6：Integrator 完成接线和旧 port 删除**

为三个 adapter 建立显式平台 `SConscript`。IMU service 注册私有 callback，把 event mask 转换为原 RT-Thread event send；平台头不包含 RT-Thread。Telemetry service 使用 UART adapter。按固定初始化顺序和反序回滚处理失败。删除旧 port 和空 driver 入口。

- [ ] **步骤 7：运行任务 6 完整验证**

```sh
tools/test.sh
tools/build.sh --cdb
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
tools/check-size.sh build/firmware/huokong.elf
sh tests/scripts/test_repository_layout.sh
```

预期七个关键符号各有一个强定义、owner 匹配 adapter、Flash <= 53248、静态 SRAM <= 16384。

- [ ] **步骤 8：提交、审查和集成任务 6**

```sh
git add -- SConscript drivers src/platform src/kernel tests/scripts tools/test.sh
git commit -m "refactor: split STM32 platform adapters"
```

记录 `TASK6_SHA=$(git rev-parse HEAD)`，以 `TASK5_BASE..$TASK6_SHA` 审查。修复循环只恢复 Task 6 Integrator。Review clean 后 cherry-pick 到 `master`，记录 `TASK6_BASE`。

---

### 任务 4：原子迁移 app 和 board

**文件和接口：** 完整采用原计划任务 7 第 603-665 行。该任务不 fan-out，因为 `applications/main.c`、`board/`、`libraries/Kconfig`、根 `Kconfig`、linker、尺寸脚本和 VS Code 路径必须在一个提交切换。

- [ ] **步骤 1：创建 Task 7 Integrator worktree**

```sh
git worktree add -b sdd/task-7-integration .worktrees/task-7-integration TASK6_BASE
```

- [ ] **步骤 2：执行原计划任务 7 的 RED、迁移和验证**

先修改尺寸/布局测试并确认旧 board 路径 RED，再移动 app、board 和 Kconfig。保留 HSE 8 MHz、PLL x9、APB1 36 MHz、APB2 72 MHz、64 KB ROM、20 KB RAM；`STM32F103xB` 移入板级 SConscript。

运行：

```sh
sh tests/scripts/test_check_size.sh
sh tests/scripts/test_repository_layout.sh
scons --menuconfig
tools/test.sh
tools/build.sh --cdb
tools/check-size.sh build/firmware/huokong.elf
```

交互 menuconfig 无法自动退出时，记录限制并运行 Kconfig 非交互解析；不得改变 `.config` 有效选项。

- [ ] **步骤 3：提交、审查和集成任务 7**

```sh
git add -- SConscript Kconfig rtconfig.py applications board libraries/Kconfig \
  src/app src/platform/board tools/check-size.sh tests/scripts .vscode
git commit -m "refactor: move application and board platform"
```

Review clean 后 cherry-pick 到 `master`，记录 `TASK7_BASE`。创建 vendor/docs lane 前再次确认用户未提交 `.vscode/launch.json` 未被覆盖；若有直接冲突，保留用户内容并停止该路径集成。

---

### 任务 5：并行启动 Docs lane 和 Vendor 任务 8

**Docs lane 文件：**
- 新建：`docs/architecture/overview.md`、`docs/architecture/dependency-rules.md`
- 新建：`docs/development/build-test-debug.md`
- 新建：`docs/protocols/imu-telemetry-v1.md`
- 新建：`docs/requirements/firmware-behavior.md`
- 新建：`docs/archive/stm32f427/README.md`
- 移动并中文化：`docs/hardware-acceptance.md` 到 `docs/hardware/acceptance.md`
- 移动：`demand/*.pdf`、`demand/*.xlsx` 到 `docs/hardware/source/`
- 新建：`docs/hardware/source/README.md`

**Vendor 任务 8 文件：** 完整采用原计划任务 8 第 669-705 行。

- [ ] **步骤 1：从 TASK7_BASE 创建两个 lane worktree**

```sh
git worktree add -b sdd/vendor-lane .worktrees/vendor-lane TASK7_BASE
git worktree add -b sdd/docs-lane .worktrees/docs-lane TASK7_BASE
```

- [ ] **步骤 2：在同一消息并行派发 Docs worker 和 Task 8 implementer**

Docs worker 只修改本任务列出的 docs/demand 路径。协议文档必须逐字记录 32 字节、版本 1、payload 26、状态掩码 `0x01FF`、CRC-16/CCITT-FALSE、200 Hz；hardware acceptance 保持“待上板验证”。不得修改 README、tools、tests、VS Code、devcontainer 或构建文件。

Task 8 implementer 移动 `rt-thread/` 到 `vendor/rt-thread/`，同步切换 `SConstruct` 的 `RTT_ROOT` 和根 Kconfig `RTT_DIR`，不修改快照内部源码。RT-Thread commit 固定为 `ddf52e2cdd977f14fc04035c88672ac204aec713`。

- [ ] **步骤 3：Docs worker 验证二进制与中文文档**

迁移前后为每个 PDF/XLSX 记录并比较：

```sh
sha256sum demand/*.pdf demand/*.xlsx
sha256sum docs/hardware/source/*.pdf docs/hardware/source/*.xlsx
```

按文件名对应的 SHA-256 必须一致。运行：

```sh
for file in \
  docs/architecture/overview.md \
  docs/architecture/dependency-rules.md \
  docs/development/build-test-debug.md \
  docs/protocols/imu-telemetry-v1.md \
  docs/requirements/firmware-behavior.md \
  docs/hardware/acceptance.md; do
    test -s "$file"
done
grep -q '待上板验证' docs/hardware/acceptance.md
```

- [ ] **步骤 4：Docs worker 提交但暂不集成**

```sh
git add -- docs demand
git commit -m "docs: prepare Chinese firmware documentation"
```

记录 `DOCS_SHA`，保持 docs lane worktree，不 cherry-pick 到 master。

- [ ] **步骤 5：Task 8 TDD、验证、提交和任务审查**

先扩展 layout test 要求 `vendor/rt-thread/tools/building.py` 存在并禁止根 `rt-thread/`，确认 RED。移动后运行：

```sh
sh tests/scripts/test_repository_layout.sh
tools/test.sh
tools/build.sh
```

提交：

```sh
git add -- SConstruct Kconfig rt-thread vendor/rt-thread tests/scripts
git commit -m "refactor: move RT-Thread under vendor"
```

在 vendor lane 内完成任务级审查和修复，记录 `TASK8_SHA`；不立即集成 master。

---

### 任务 6：Vendor lane 任务 9

**文件和接口：** 完整采用原计划任务 9 第 709-755 行。

- [ ] **步骤 1：增加三个快照和 manifest RED 断言**

要求存在 `vendor/cmsis-core/`、`vendor/stm32f1-cmsis/`、`vendor/stm32f1-hal/`、`vendor/manifest.md`，禁止根 `packages/`。运行 layout test，预期在移动前失败。

- [ ] **步骤 2：移动快照并更新全部构建路径**

固定 commit：

```text
CMSIS-Core: 39d8e01f0be84b83a8f11d33756e82ce1ef07a84
STM32F1 CMSIS: 4d57f5017d2937f10d07331e90828d3a81f980b8
STM32F1 HAL: 0b18f3336e7ef67e51080e72ae6805dba6cc7bb8
SVD: c65f8551e57c770344d229dcaa0bf838fa29aff4
SVD SHA-256: 1d92b65aaf397a18a599fb6a840812015ad379cdcc0cc3687f673f63e7445367
```

保留 direct HAL 的 SPI/TIM/TIM_EX 显式源，不修改快照内容。

- [ ] **步骤 3：验证、提交和任务审查**

```sh
sh tests/scripts/test_repository_layout.sh
tools/test.sh
tools/build.sh
git add -- SConstruct SConscript Kconfig packages vendor/cmsis-core \
  vendor/stm32f1-cmsis vendor/stm32f1-hal vendor/manifest.md tests/scripts
git commit -m "refactor: move CMSIS and HAL snapshots under vendor"
```

完成 task review/fix loop 后记录 `TASK9_SHA`。

---

### 任务 7：Vendor lane 任务 10 并汇入 master

**文件和接口：** 完整采用原计划任务 10 第 759-825 行。

- [ ] **步骤 1：增加 driver、patch 和 owner RED 断言**

要求 `vendor/rt-thread-stm32-drivers/` 和 `vendor/patches/rt-thread-stm32-drivers-exti15-10-owner.patch` 存在，禁止 `libraries/HAL_Drivers/`。确认移动前 layout test 失败。

- [ ] **步骤 2：移动固定快照并写可重放 patch**

上游 commit 使用 RT-Thread 的 `ddf52e2cdd977f14fc04035c88672ac204aec713`。Patch 必须包含原计划第 786-799 行完整 guard diff，记录 `BSP_GPIO_EXTI15_10_EXTERNAL=y`，可从固定上游子树执行：

```sh
patch -p1 < vendor/patches/rt-thread-stm32-drivers-exti15-10-owner.patch
```

快照内保留已应用 guard；不得除此之外修改第三方源码。

- [ ] **步骤 3：完成 manifest、验证和提交**

```sh
sh tests/scripts/test_repository_layout.sh
tools/test.sh
tools/build.sh
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
git add -- SConstruct src/platform/board libraries/HAL_Drivers \
  vendor/rt-thread-stm32-drivers vendor/patches vendor/manifest.md \
  .config rtconfig.h tests/scripts
git commit -m "refactor: move STM32 platform drivers under vendor"
```

- [ ] **步骤 4：审查完整任务 10 和 vendor lane**

先执行任务 10 scoped review/fix loop，再以 `TASK7_BASE..HEAD` 检查三项 vendor 提交：版本未升级、快照内部只有已记录 EXTI guard 差异、所有旧第三方根路径消失、manifest 记录完整。

- [ ] **步骤 5：顺序汇入 master**

在确认 master 仍以 `TASK7_BASE` 为代码基线后，按 Task 8、9、10 顺序 cherry-pick 三个已审查提交。若文档设计提交位于其后，不影响代码基线；若共享构建文件发生冲突，只在专用 integration worktree 重演，不在 master 解决猜测性冲突。记录 `VENDOR_BASE`。

---

### 任务 8：汇入 Docs lane 并完成任务 11

**文件：**
- 汇入：Docs lane 的 docs/demand 变更
- 移动：`tests/test_check_size.sh`、`tests/test_test_runner.sh` 到 `tests/scripts/`
- 修改：`tools/test.sh`、`README.md`、`tests/scripts/test_repository_layout.sh`
- 兼容检查：用户修改的 `.vscode/`、`.devcontainer/` 和 debug/flash/openocd 工具路径

**接口：**
- README 只公布 `tools/test.sh`、`tools/build.sh`、`tools/flash.sh`、`tools/debug.sh`、`tools/console.sh`。
- shell runner 使用 `SKIP_TEST_RUNNER_SELF_TEST=1` 防止递归。
- 活动文档旧路径 grep 按 ledger ruling 排除 `docs/superpowers/` 和 `docs/archive/`。

- [ ] **步骤 1：创建任务 11 integration worktree 并汇入 docs**

```sh
git worktree add -b sdd/task-11-integration .worktrees/task-11-integration VENDOR_BASE
git -C .worktrees/task-11-integration cherry-pick -n DOCS_SHA
```

预期：只出现 docs/demand 路径，无构建文件冲突。

- [ ] **步骤 2：写 active 文档 RED 断言**

扩展 layout test，要求六份 active 中文文档和 hardware source README 存在；只在以下活动入口禁止 `.script/`、根旧固件产物名和 `tests/build`：

```text
README.md
.vscode/
.devcontainer/
docs/architecture/
docs/development/
docs/hardware/
docs/protocols/
docs/requirements/
```

在汇入 docs 之前的基线运行应失败；汇入后重复应通过文档存在性检查。

- [ ] **步骤 3：迁移 shell 测试并统一入口**

`tools/test.sh` 在 C 测试后执行：

```sh
sh tests/scripts/test_check_size.sh
if [[ ${SKIP_TEST_RUNNER_SELF_TEST:-0} != 1 ]]; then
    SKIP_TEST_RUNNER_SELF_TEST=1 sh tests/scripts/test_test_runner.sh
fi
sh tests/scripts/test_repository_layout.sh
```

`test_test_runner.sh` 内部调用统一入口时设置同一环境变量。保留用户 debug-probe 测试调用，不覆盖用户 `tools/test.sh` 逻辑；若其语义与递归门禁冲突，停止并请求用户裁决。

- [ ] **步骤 4：重写 README 并保留用户工具能力**

README 只保留项目简介、五个公开命令、架构/硬件/协议文档链接和当前限制。把详细接线、协议、状态机和构建说明链接到 active 文档。用户新增 OpenOCD/probe 能力通过五个公开命令内部调用，不把额外内部脚本列为第六个公开入口。

- [ ] **步骤 5：验证任务 11**

```sh
tools/test.sh
tools/build.sh
git grep -n '\.script/' -- README.md .vscode .devcontainer \
  docs/architecture docs/development docs/hardware docs/protocols docs/requirements \
  && exit 1 || true
git grep -nE 'rt-thread\.elf|rtthread\.bin|rt-thread\.map|tests/build' -- \
  README.md .vscode .devcontainer docs/architecture docs/development \
  docs/hardware docs/protocols docs/requirements tools tests \
  && exit 1 || true
```

硬件验收必须仍显示待上板。

- [ ] **步骤 6：提交、审查和集成任务 11**

```sh
git add -- README.md docs demand tests/scripts tools/test.sh .vscode .devcontainer
git commit -m "docs: add Chinese firmware documentation"
```

任务审查必须同时确认 DOCS_SHA 的内容和 Integrator 接线。Review clean 后，在不覆盖 master 用户未提交改动的前提下集成；有直接冲突则停止该路径集成并请求用户处理。

---

### 任务 9：任务 12 架构门禁和最终验收

**文件和接口：** 完整采用原计划任务 12 第 901-982 行，并增加并行设计的 worktree/用户改动检查。

- [ ] **步骤 1：补齐最终 layout 和 dependency 断言**

验证自研/第三方/测试 C/H 边界、旧目录消失、所有自研 SConscript 无 Glob、根目录无生成物、所有 planned objects 位于 `build/`。修复任务 1 deferred Minor：全仓对象扫描必须捕获并检查 `find` 失败，不能由 `find | awk` 掩盖非零状态。

- [ ] **步骤 2：执行干净提交前验证**

只删除当前 integration worktree 的生成目录：

```sh
rm -rf -- build
tools/test.sh
tools/build.sh --cdb
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
tools/check-size.sh build/firmware/huokong.elf
```

预期全部 host/shell/架构测试通过，ELF/BIN/MAP 路径正确，Flash <= 53248，静态 SRAM <= 16384，链接范围 64 KB/20 KB，七个 owner 正确。

- [ ] **步骤 3：提交最终门禁**

```sh
git add -- tests/scripts tools/test.sh .gitignore SConstruct SConscript src vendor
git commit -m "test: enforce firmware architecture boundaries"
```

- [ ] **步骤 4：执行任务 12 审查和一次修复波次**

Task Reviewer clean 后集成到 master。Critical/Important finding 最多按 SDD 规则五轮；Minor 进入 ledger。

- [ ] **步骤 5：全分支最终审查**

以并行执行起始 merge base 到 HEAD 生成单一 review package。Final Reviewer 必须检查：

```text
原架构规格全部验收标准
ledger 中全部 deferred Minor 和 Ruling
worker 文件所有权没有越界进入 master
vendor 版本/patch provenance
用户并行工具能力没有被静默删除
没有 worker 不可构建临时提交进入 master
```

若有 finding，派发一个 final fixer 处理完整列表，只做一次 scoped re-review；剩余 finding 按 SDD breaker 规则裁决并报告。

- [ ] **步骤 6：提交后重新执行完整验证**

```sh
rm -rf -- build
tools/test.sh
tools/build.sh --cdb
python3 tests/scripts/test_link_owners.py \
  build/firmware/huokong.elf build/firmware/huokong.map
tools/check-size.sh build/firmware/huokong.elf
git status --short
git diff --check
git log --oneline --decorate -20
```

未实际上板的硬件验收继续报告“待上板验证”。

- [ ] **步骤 7：清理本计划 worktree**

收集 ledger 全部 `Ruling:` 到最终报告。对下列本计划 worktree 逐个验证 `git status --short` 为空且提交已进入 task Integrator 或 master，然后执行 `git worktree remove "$path"`：

```text
.worktrees/task-5-imu-worker
.worktrees/task-5-telemetry-worker
.worktrees/task-5-integration
.worktrees/task-6-bmi-worker
.worktrees/task-6-clock-worker
.worktrees/task-6-uart-worker
.worktrees/task-6-integration
.worktrees/task-7-integration
.worktrees/vendor-lane
.worktrees/docs-lane
.worktrees/task-11-integration
```

再用 `git branch -d` 删除对应 `sdd/` 临时分支。不删除其他计划的 worktree，不使用强制删除。
