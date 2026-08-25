# 并行 Subagent 执行补充设计

## 状态

本设计于 2026-08-26 在对话中获准采用“任务内 fan-out/fan-in + vendor/docs 双 lane”方案。它补充 `2026-08-26-firmware-repository-architecture-design.md` 的实施方式，不修改其中的固件架构、模块接口或验收标准。

## 背景

原实施计划按任务 1 至 12 串行迁移。任务 1 至 3 已完成并通过任务级审查；任务 4 已完成实现和一轮修复，仍需完成修复复审。剩余任务中既有严格前后依赖，也有可以按文件所有权拆分的独立工作。

多个 subagent 直接写同一个 `master` 工作区会共享 Git 索引、生成目录和路径迁移状态，无法安全并行。用户已批准使用 linked worktree 和临时分支，并允许重构尚未完成的实施任务以获得真正并发。

当前 `master` 还包含用户并行维护的 README、VS Code 和调试工具改动。并行实施不得读取后覆盖、暂存或提交这些改动。

## 目标

- 在不牺牲任务级构建、测试和审查门禁的前提下并行开发。
- 让每个 worker subagent 只拥有互不重叠的文件，消除共享工作树竞态。
- 让 `master` 始终只接收经过集成和审查的可构建任务提交。
- 通过任务内 fan-out/fan-in 缩短 kernel 和 platform 拆分的等待时间。
- 在 board 稳定后并行执行 vendor 迁移与中文文档主体编写。
- 保留原计划的协议、硬件、尺寸、第三方版本和行为约束。

## 非目标

- 不并行执行具有直接提交依赖的完整任务。
- 不让 worker 修改共享根构建文件、Git 索引或 `master`。
- 不将临时 worker 提交直接视为可集成固件提交。
- 不减少任务级规格审查、质量审查或最终全分支验收。
- 不借执行方式重构增加固件功能、依赖版本或兼容层。

## 总体模型

执行分为三类角色：

1. Controller 在当前 `master` 协调依赖、创建 worktree、记录 ledger、汇总裁决和执行最终验收。Controller 不手工修复审查问题。
2. Worker 在独立 worktree 中实现一个文件所有权明确的组件，只提交自身路径，不修改共享接线文件。
3. Integrator 在独立集成 worktree 中汇总同一任务的 worker 结果，完成共享构建声明、旧路径删除、服务接线、完整验证和任务级原子提交。

任务级 Reviewer 只审查 Integrator 形成的完整任务差异。worker 的聚焦验证和自审是输入证据，不能替代任务级审查。

## Worktree 和分支

所有并行 worktree 放在仓库 `.worktrees/` 下，并由根 `.gitignore` 忽略。命名约定：

```text
.worktrees/task-5-imu-worker/          sdd/task-5-imu-worker
.worktrees/task-5-telemetry-worker/    sdd/task-5-telemetry-worker
.worktrees/task-5-integration/         sdd/task-5-integration
.worktrees/task-6-bmi-worker/          sdd/task-6-bmi-worker
.worktrees/task-6-clock-worker/        sdd/task-6-clock-worker
.worktrees/task-6-uart-worker/         sdd/task-6-uart-worker
.worktrees/task-6-integration/         sdd/task-6-integration
.worktrees/vendor-lane/                sdd/vendor-lane
.worktrees/docs-lane/                  sdd/docs-lane
```

每一轮并行开始前，所有同轮 worker 从同一个已审查基线提交创建。Integrator 也从该基线创建，使用 `git cherry-pick -n` 汇入 worker 提交，使临时提交不会作为不可构建提交进入最终分支；Integrator 完成接线后形成一个任务级提交。

Controller 只把通过审查的 Integrator 提交以 fast-forward 或普通 cherry-pick 方式纳入 `master`。若 `master` 的用户未提交改动与目标提交直接冲突，停止集成并请求用户处理；不还原或覆盖用户改动。

## 文件所有权

### 任务 5

IMU worker 独占：

```text
src/kernel/imu/imu_snapshot.h
src/kernel/imu/imu_policy.c
src/kernel/imu/imu_policy.h
src/kernel/imu/imu_service.c
src/kernel/imu/imu_service.h
tests/kernel/imu/test_imu_policy.c
```

Telemetry worker 独占：

```text
src/kernel/telemetry/telemetry_policy.c
src/kernel/telemetry/telemetry_policy.h
src/kernel/telemetry/telemetry_service.c
src/kernel/telemetry/telemetry_service.h
tests/kernel/telemetry/test_telemetry_policy.c
```

两个 worker 都可以读取旧 `applications/imu_service_logic.*` 和服务文件，但不得修改或删除它们。Integrator 独占：

```text
src/kernel/imu/SConscript
src/kernel/telemetry/SConscript
SConscript
tests/SConstruct
applications/SConscript
applications/main.c
applications/imu_service_logic.c
applications/imu_service_logic.h
applications/imu_service.c
applications/imu_service.h
applications/telemetry_service.c
applications/telemetry_service.h
```

Integrator 负责处理 worker 新文件与旧文件的移动关系、消除重复实现，并确保最终提交显示为合理移动或拆分。

### 任务 6

BMI worker 独占：

```text
src/platform/devices/bmi088_stm32.c
src/platform/devices/bmi088_stm32.h
```

Clock worker 独占：

```text
src/platform/time/monotonic_clock_stm32.c
src/platform/time/monotonic_clock_stm32.h
```

UART worker 独占：

```text
src/platform/transport/telemetry_uart_stm32.c
src/platform/transport/telemetry_uart_stm32.h
```

三个 worker 只从旧 `drivers/bmi088_port.*` 提取自身职责，不修改旧 port、kernel service、根构建文件或链接所有权测试。Integrator 独占：

```text
src/platform/devices/SConscript
src/platform/time/SConscript
src/platform/transport/SConscript
src/kernel/imu/imu_service.c
src/kernel/telemetry/telemetry_service.c
drivers/bmi088_port.c
drivers/bmi088_port.h
drivers/SConscript
SConscript
tests/scripts/test_link_owners.py
tools/test.sh
```

Integrator 负责合并平台私有状态、服务通知桥、初始化和反向回滚、删除旧 port，并验证七个 IRQ/HAL callback 的单一所有者。

### Vendor 和 Docs Lane

任务 7 必须先在单个集成 worktree 中完成，因为 app、board、Kconfig、linker 和尺寸门禁必须原子移动。

任务 7 通过审查后创建两条 lane：

- Vendor lane 从任务 7 基线顺序执行任务 8、9、10，并在自身分支上逐任务构建、审查和提交。该 lane 独占 `vendor/`、第三方旧目录、`SConstruct`、根 `SConscript`、`Kconfig` 和板级 Kconfig 中的依赖路径。
- Docs lane 从同一基线只创建任务 11 的分类中文文档、归档索引并迁移硬件资料。该 lane 不修改 `README.md`、`tools/`、`tests/`、`.vscode/`、`.devcontainer/` 或构建文件。

Vendor lane 完成后先集成到 `master`，再无冲突汇入 Docs lane 的文档提交。任务 11 Integrator 最后基于最终目录完成 README、shell 测试、统一测试入口和活动文档静态检查，同时显式保留用户并行工具改动。

## 并行阶段

### 阶段 0：固定当前基线

1. 重新执行任务 4 修复复审。
2. 若复审通过，将任务 4 标记完成并记录基线提交。
3. 检查任务 1 至 4 的提交和 ledger 一致。
4. 将 `.worktrees/` 加入根 `.gitignore`；只提交这一隔离基础设施变更，不包含用户改动。

### 阶段 1：任务 5 fan-out/fan-in

1. 并行派发 IMU worker 和 Telemetry worker。
2. 每个 worker 运行其 policy 的独立主机编译和测试，并提交组件结果。
3. Integrator 使用 `cherry-pick -n` 汇入两个 worker，完成服务移动、构建接线和旧逻辑删除。
4. Integrator 运行两个聚焦测试、完整主机测试、固件构建、CDB 隔离和布局门禁。
5. 对完整任务 5 差异执行独立任务审查和修复循环。

### 阶段 2：任务 6 fan-out/fan-in

1. 从已审查任务 5 基线并行派发 BMI、Clock、UART 三个 worker。
2. worker 使用交叉编译器对自身新文件做聚焦语法/对象编译；不得链接完整固件或删除旧 port。
3. Integrator 汇入三个 worker，完成 service 接线、旧 port 删除和链接所有权测试。
4. Integrator 运行完整主机测试、固件构建、Map/nm 所有权和尺寸门禁。
5. 对完整任务 6 差异执行独立任务审查和修复循环。

### 阶段 3：任务 7 原子迁移

任务 7 由单个 Integrator 执行并审查，不拆分共享路径。完成后才允许创建 vendor/docs 双 lane。

### 阶段 4：vendor/docs 双 lane

Vendor lane 与 Docs lane 并行运行。Vendor lane 内部任务 8、9、10 保持顺序；Docs lane 只处理无共享路径的文档主体。Controller 可以在等待实现时并行准备各自 review package，但同一 lane 内不并行写共享文件。

### 阶段 5：汇合和最终门禁

1. 集成并复核 Vendor lane。
2. 汇入 Docs lane 的无冲突文档提交。
3. 执行任务 11 Integrator 和任务级审查。
4. 串行执行任务 12 架构门禁。
5. 执行一次全分支审查、一次修复波次和提交后干净总验收。

## Worker 合同

每个 worker brief 必须包含：

- 精确拥有路径和禁止修改路径。
- 可读取的旧实现来源。
- 必须保持的公开接口、硬件顺序和行为。
- 可在未集成状态执行的聚焦编译或纯逻辑测试。
- 提交和报告要求。
- 禁止派发子代理、禁止修改共享构建文件、禁止触碰用户并行改动。

worker 遇到跨所有权依赖时必须报告给 Integrator，不得自行扩大文件范围。

## 集成和审查门禁

每个 Integrator 必须确认：

- worker 提交都基于同一个任务基线。
- `cherry-pick -n` 后没有意外共享文件或冲突标记。
- 旧实现只在新实现和构建接线完成后删除。
- 最终任务提交从父提交开始始终可完整测试和构建。
- worker 聚焦证据、Integrator 完整验证和任务 Reviewer 双重 verdict 均已记录。
- Critical/Important finding 按现有五轮修复规则处理；Minor 进入 ledger 供最终审查。

最终全分支 Reviewer 仍以原架构设计和原实施计划的全局约束为权威，并额外检查本设计中的文件所有权、临时提交隔离和用户改动保护。

## 失败和恢复

- worker 失败不会污染其他 worktree；修复优先恢复同一 agent 和同一 worker 分支。
- worker 产出违反所有权时，Integrator 不汇入越界部分，要求 worker 重做，不在 Controller 中手工修复。
- Integrator 发现两个 worker 对同一语义做出不兼容假设时，以原规格为准做最小裁决并记录 ledger；无法确定时才停止。
- cherry-pick 冲突只允许出现在 Integrator worktree；不得在 `master` 上试合并。
- Vendor/Docs lane 汇合若触碰用户未提交路径，先保留用户内容并停止该路径集成，不使用 restore、checkout 或 reset。
- 构建失败只清理对应 worktree 的根 `build/`，不清理源码树或其他 worktree。

## 生命周期和清理

临时 worker 分支和 worktree 保留到其 Integrator 提交通过任务审查。之后可删除该任务的 worker worktree，但集成 worktree保留到提交进入 `master`。Vendor/Docs lane 保留到任务 11 通过审查。

所有任务和最终审查完成后，按 worktree 技能检查分支已集成且工作树干净，再删除 `.worktrees/` 下本计划创建的 worktree 和对应临时分支。不得删除不属于本计划的 worktree。

## 验收标准

- 至少任务 5 有两个实现 subagent 真正并行运行。
- 至少任务 6 有三个实现 subagent 真正并行运行。
- Vendor 和 Docs lane 在任务 7 后并行运行。
- 任意时刻没有两个写入 subagent 修改同一文件或同一 worktree。
- `master` 不出现 worker 的不可构建中间提交。
- 每个集成任务继续通过原计划规定的测试、固件构建、尺寸和审查门禁。
- 用户并行的 README、VS Code 和调试工具改动未被覆盖、还原或误提交。
- 最终仓库架构和固件行为满足原架构设计全部验收标准。
