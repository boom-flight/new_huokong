# Firmware Review Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 `new_huokong` 审查发现的运行时、生命周期、构建门禁问题，并删除明确的重复抽象。

**Architecture:** 将修改拆为四条文件边界清晰的 lane：IMU 采样与生命周期、遥测 DMA、构建门禁、文档与低风险冗余。运行时 lane 只接受能够由软件验证的样本一致性条件；构建 lane 不改变 vendor 快照；主 agent 在各 lane 完成后统一整合和验证。

**Tech Stack:** C11、RT-Thread、STM32 HAL、SCons、Python 3、Bash、主机端 C 测试。

**Spec:** `docs/superpowers/specs/2026-08-27-firmware-review-remediation-design.md`

## Global Constraints

- 不改变 32 字节 USART2 IMU telemetry v1 协议、序号消耗规则、CRC 或状态位定义。
- 不修改 `vendor/` 固定第三方快照。
- `modules` 不得引入 RT-Thread、HAL、CMSIS 或 platform 依赖。
- 生产源文件必须继续使用显式 SCons 清单，不使用递归扫描。
- 所有新增行为必须先有可执行测试或静态门禁，再实现代码。
- 不提交、不重置、不覆盖工作区中与本任务无关的既有改动。

---

### Task 1: Add Runtime Consistency Policy Tests

**Files:**
- Modify: `tests/kernel/imu/test_imu_policy.c`
- Modify: `src/kernel/imu/imu_policy.h`

**Interfaces:**
- 继续使用现有 IMU policy 公共接口。
- 如需纯函数表示“读取前后 latch 序号是否一致”，在 policy 头文件声明一个只含标准类型的函数，供 kernel 使用。

- [ ] **Step 1: Write failing tests**

增加测试覆盖：读取前后序号相同则样本可接受；序号变化则样本必须被拒绝并报告 overrun；序号自然回绕时 `UINT32_MAX -> 0` 仍视为一次新事件而不是异常。

- [ ] **Step 2: Run the focused test**

Run: `scons -f tests/SConstruct build/scons/host-tests/test_imu_policy && build/scons/host-tests/test_imu_policy`

Expected: 新增测试在实现前失败，失败原因是策略接口或行为尚未存在。

- [ ] **Step 3: Implement only the pure policy decision**

使用无符号序号差值实现回绕安全判断；该函数不访问 RT-Thread、HAL 或平台对象。

- [ ] **Step 4: Run the focused test again**

Run: `scons -f tests/SConstruct build/scons/host-tests/test_imu_policy && build/scons/host-tests/test_imu_policy`

Expected: PASS。

---

### Task 2: Fix BMI088 Sample/Latch Correlation

**Files:**
- Modify: `src/platform/devices/bmi088_stm32.h`
- Modify: `src/platform/devices/bmi088_stm32.c`
- Modify: `src/kernel/imu/imu_service.c`
- Modify: `tests/fakes/fake_bmi088_bus.c`
- Modify: `tests/fakes/fake_bmi088_bus.h`
- Modify: `tests/kernel/imu/test_imu_policy.c` or add a focused policy test if the existing host boundary cannot exercise platform code

**Interfaces:**
- 平台 latch getter 必须在中断屏蔽下返回一致的 `{timestamp_us, sequence}` 快照。
- IMU 处理路径必须在 SPI read 前保存 latch，read 后再次读取；如果序号发生变化，不能把本次 raw sample 计为有效样本。

- [ ] **Step 1: Add a deterministic test seam for a read-time DRDY update**

让 fake bus 在指定的 SPI read 后模拟 latch 序号变化，或将“前后序号一致性”留在纯 policy 测试中验证，并明确 kernel 层只在一致时更新 `consumed_*_sequence` 和最新样本。

- [ ] **Step 2: Run the focused test and observe failure**

Run: `scons -f tests/SConstruct -j"$(nproc)" && build/scons/host-tests/test_imu_policy`

Expected: 新增并发更新场景在修复前不能通过。

- [ ] **Step 3: Implement read-before/read-after validation**

在 `process_accel()` 和 `process_gyro()` 中：读取初始 latch，执行 BMI088 burst read，读取结束 latch；若序号变化，增加对应 overrun，校准状态重置，并等待后续事件，不发布该次读取为有效样本。不要伪造一个新的 timestamp 来掩盖不确定性。

- [ ] **Step 4: Preserve normal and wraparound behavior**

确认无并发更新时现有采样计数、时间戳、校准和姿态积分不变；确认序号回绕不会误报持续 overrun。

- [ ] **Step 5: Run focused and full host tests**

Run: `./tools/test.sh`

Expected: PASS。

---

### Task 3: Make Fault Retry and Service Startup Transactional

**Files:**
- Modify: `src/kernel/imu/imu_service.c`
- Modify: `src/kernel/imu/imu_service.h`
- Modify: `src/kernel/telemetry/telemetry_service.c`
- Modify: `src/kernel/telemetry/telemetry_service.h`
- Modify: `src/kernel/logging/imu_log_service.c`
- Modify: `src/kernel/logging/imu_log_service.h`
- Modify: `src/app/main.c`
- Modify: `tests/kernel/logging/test_imu_log_service_failure.c`
- Modify: `tests/kernel/logging/test_imu_log_service_startup_failure.c`

**Interfaces:**
- 各 service 提供与现有 `*_init()` 对称的停止/回滚接口；接口必须对“尚未成功初始化”和“已初始化一次”安全。
- `imu_service_init()` 失败时只留下零资源状态；`telemetry_service_deinit()` 能停止遥测线程并释放 UART；日志服务失败后可以重新调用 init。

- [ ] **Step 1: Extend fake RT-Thread tests for retry and cleanup**

验证队列初始化失败后恢复为成功结果时，第二次 `imu_log_service_init()` 能成功；线程初始化/启动失败时已初始化的消息队列被回收；重复 deinit 不崩溃。

- [ ] **Step 2: Run logging tests before implementation**

Run: `scons -f tests/SConstruct build/scons/host-tests/test_imu_log_service_failure build/scons/host-tests/test_imu_log_service_startup_failure && build/scons/host-tests/test_imu_log_service_failure && build/scons/host-tests/test_imu_log_service_startup_failure`

Expected: 新的 retry/cleanup 断言失败。

- [ ] **Step 3: Implement rollback in log service**

只有消息队列和线程成功启动后才设置 initialized/enabled；每个失败分支按已完成步骤逆序释放；不允许失败状态阻止后续重试。

- [ ] **Step 4: Clear stale IMU events after sensor reinitialization**

在故障等待结束或重新初始化成功前清除 `imu_event` 的 accel/gyro 标志，并同步 latch 序号；不要消费重初始化前遗留的事件。

- [ ] **Step 5: Add service teardown and fix app startup order**

选择可被现有 RT-Thread 静态线程 API 正确停止的顺序：优先初始化遥测，再初始化 IMU；任一后续 init 失败时释放已成功初始化的服务。若必须暂停线程，先阻止其继续运行，再释放其依赖的平台句柄和 IPC 对象。

- [ ] **Step 6: Run full host tests and firmware build**

Run: `./tools/test.sh && ./tools/build.sh`

Expected: PASS，且静态 SRAM/Flash 不超过原有门限。

---

### Task 4: Simplify DMA and Telemetry Result Handling

**Files:**
- Modify: `src/modules/transport/dma_tx_state.h`
- Modify: `src/modules/transport/dma_tx_state.c`
- Modify: `src/platform/transport/telemetry_uart_stm32.h`
- Modify: `src/platform/transport/telemetry_uart_stm32.c`
- Modify: `src/kernel/telemetry/telemetry_service.c`
- Modify: `tests/modules/transport/test_dma_tx_state.c`

**Interfaces:**
- DMA 状态模块保留 reserve、release、async error、take failure 的最小状态转换；正常完成和同步启动失败不能产生异步 failure_pending。
- UART 层返回单一、可判别的发送结果，telemetry service 不再先 busy 检查再重复 try-start。

- [ ] **Step 1: Update state-machine tests**

测试 idle reserve、busy reserve、normal release、同步取消、异步 error、take failure 的状态和 failure_pending 结果。

- [ ] **Step 2: Run focused DMA tests**

Run: `scons -f tests/SConstruct build/scons/host-tests/test_dma_tx_state && build/scons/host-tests/test_dma_tx_state`

Expected: 若移除重复接口，旧调用或新增语义断言先失败。

- [ ] **Step 3: Replace duplicate cancel/complete implementation**

使用一个表达“释放 busy 状态”的内部/公共操作，按调用语义保留必要的命名；不要为了形式合并而丢失同步启动失败与异步错误的区别。

- [ ] **Step 4: Make telemetry loop consume one send result**

让一次调度最多消耗一次异步 failure；发送尝试的 sequence、drop sticky、drop counter 和双缓冲切换保持现有协议语义。

- [ ] **Step 5: Run all host tests and firmware build**

Run: `./tools/test.sh && ./tools/build.sh`

Expected: PASS。

---

### Task 5: Repair Build Reproducibility and Default Gates

**Files:**
- Modify: `SConstruct`
- Modify: `tools/env.sh`
- Modify: `tools/test.sh`
- Modify: `tools/build.sh`
- Modify: `tools/rebuild-index.sh`
- Modify: `.clangd`
- Modify: `tests/scripts/test_link_owners.py` only if needed for stable invocation
- Modify: `tests/scripts/test_repository_layout.sh`
- Add or modify: `tests/scripts/test_build_configuration.sh`

**Interfaces:**
- 所有公开脚本默认使用 `vendor/rt-thread`；外部 `RTT_ROOT` 要么被拒绝并给出明确错误，要么不参与路径解析。
- `tools/test.sh` 必须在固件 ELF/MAP 存在时执行链接所有权检查；缺失产物必须 fail closed。
- `.clangd` 的 `CompilationDatabase` 必须指向实际生成的 `build/scons` 子目录。

- [ ] **Step 1: Add failing gate tests**

覆盖：默认测试脚本包含链接所有权检查；外部 `RTT_ROOT` 不会改变构建源；布局扫描拒绝根目录或源码目录中的 ELF/BIN/MAP/Keil 产物；clangd 路径与生成位置一致。

- [ ] **Step 2: Run the gate tests before implementation**

Run: `sh tests/scripts/test_build_configuration.sh`

Expected: 至少对当前缺口失败。

- [ ] **Step 3: Fix fixed-vendor resolution**

在 SCons 入口中使用仓库根下的 `vendor/rt-thread`，保留本地 vendor package 检查；同步修正文档和脚本环境说明。

- [ ] **Step 4: Wire link ownership into public testing**

在尺寸检查和固件存在性前置条件满足后调用 `python3 tests/scripts/test_link_owners.py`，并在缺失 ELF/MAP 时明确失败，而不是跳过。

- [ ] **Step 5: Fix compilation database and layout checks**

将 `.clangd` 指向 `build/scons`；扩展 layout 扫描的产物扩展名，同时排除合法 `build/` 目录。

- [ ] **Step 6: Run all static gates and build**

Run: `./tools/test.sh && ./tools/build.sh && sh tests/scripts/test_build_configuration.sh`

Expected: PASS。

---

### Task 6: Remove Low-Risk Redundancy and Synchronize Documentation

**Files:**
- Delete or reduce: `readme.md`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`
- Modify: `docs/architecture/dependency-rules.md`
- Modify: `docs/development/build-test-debug.md`
- Modify: `.gitignore`
- Modify: repeated module `SConscript` files only where a shared existing helper can remove exact duplication without hiding source ownership

**Interfaces:**
- 生产代码公共接口不因文档整理改变。
- 文档必须描述实际启动顺序、实际门禁和实际编译数据库路径。

- [ ] **Step 1: Identify exact duplicate text and rules**

保留 `README.md` 作为唯一项目入口；将架构细节保留在 `docs/architecture/`，删除重复架构文档或改为短链接指引；删除重复 `.gitignore` 规则。

- [ ] **Step 2: Update documentation after code interfaces settle**

同步启动失败回滚、DRDY 样本拒绝策略、链接所有权门禁、固定 vendor 依赖和 clangd 数据库路径。

- [ ] **Step 3: Run repository layout and documentation references checks**

Run: `sh tests/scripts/test_repository_layout.sh && sh tools/keil-project-check.sh`

Expected: PASS，且没有活动入口引用已删除的 `readme.md` 或旧路径。

---

### Task 7: Integrate and Review All Lanes

**Files:**
- Modify only files required to resolve cross-lane compile/test conflicts.

- [ ] **Step 1: Inspect all worktree changes**

Run: `git status --short` and `git diff --stat`;确认没有 vendor 修改、无意外删除和无关文件改动。

- [ ] **Step 2: Run complete verification**

Run: `./tools/test.sh`

Run: `./tools/build.sh`

Run: `python3 tests/scripts/test_link_owners.py build/scons/firmware/huokong.elf build/scons/firmware/huokong.map`

Expected: 全部 PASS。

- [ ] **Step 3: Review runtime-sensitive changes**

重点检查：事件清除是否发生在重新初始化边界；读取期间序号变化是否不会发布错误样本；服务停止是否先停线程再释放底层句柄；遥测 sequence 是否仍按每次尝试递增。

- [ ] **Step 4: Run diff hygiene checks**

Run: `git diff --check`

Expected: 无空白错误、无 vendor 快照变更。
