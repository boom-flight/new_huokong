# IMU Async Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 BMI088 IMU 实时线程中的同步控制台日志改为固定消息队列和低优先级 logger 线程，确保 UART 阻塞不会造成采样 overrun 或校准重置。

**Architecture:** 使用纯 C 的固定日志事件/格式化模块承载可测试的数据格式，使用 RT-Thread 静态 message queue 传递事件。IMU 线程只调用零等待的 `rt_mq_send()`，logger 线程以优先级 20 阻塞等待消息并执行 `rt_kprintf()`；队列满或 logger 初始化失败只影响日志，不影响 IMU 服务。

**Tech Stack:** C11, RT-Thread 5.2.2 IPC message queue, SCons, host tests with `cc` and `assert`.

**Spec:** `new_huokong/docs/superpowers/specs/2026-08-26-async-imu-logging-design.md`

## Global Constraints

- IMU 工作线程不直接调用 `rt_kprintf`。
- 日志提交必须使用 `rt_mq_send()` 的零等待路径；禁止动态内存和阻塞等待。
- 固定消息队列容量为 8，消息结构大小固定，队列满时丢日志并饱和累计丢弃数。
- logger 线程优先级为 20、栈大小为 512 字节；IMU 优先级保持 5。
- 不修改 BMI088 驱动、遥测协议 v1、校准阈值、样本数和 overrun 重置语义。
- 日志服务初始化失败时不得回退到 IMU 线程同步输出。
- 不创建提交；当前工作流明确要求由用户决定何时提交 Git。

---

## File Map

### New Files

- `src/kernel/logging/imu_log_event.h`: 日志事件类型、固定载荷和纯 C 格式化接口。
- `src/kernel/logging/imu_log_event.c`: 状态名映射和事件格式化实现，不依赖 RT-Thread。
- `src/kernel/logging/imu_log_service.h`: RT-Thread 日志服务公共接口。
- `src/kernel/logging/imu_log_service.c`: 静态消息队列、logger 线程、非阻塞提交和丢弃计数。
- `src/kernel/logging/SConscript`: 固件日志模块构建定义。
- `tests/kernel/logging/test_imu_log_event.c`: 日志事件格式和边界测试。
- `tests/kernel/logging/test_imu_log_service.c`: 使用 RT-Thread 测试替身验证零等待入队和队列满行为。
- `tests/kernel/logging/test_imu_log_service_failure.c`: 验证 logger 初始化失败后 IMU 日志提交仍立即返回。
- `tests/fakes/rtthread.h`: 仅供 logger service 主机测试使用的最小 RT-Thread 类型和 API 声明。
- `tests/fakes/fake_rtthread_log.c`: RT-Thread message queue/thread 测试替身实现。
- `tests/fakes/fake_rtthread_log.h`: 配置测试替身初始化和启动返回值。
- `tests/scripts/test_no_imu_thread_logging.sh`: 防止 IMU 实时线程重新引入同步日志。

### Modified Files

- `src/kernel/imu/imu_service.c`: 移除所有同步日志输出，改为提交固定事件。
- `src/kernel/imu/SConscript`: 为 IMU service 添加 `src/kernel` logging include path。
- `SConscript`: 加入 `src/kernel/logging/SConscript`。
- `rtconfig.h`: 启用 `RT_USING_MESSAGEQUEUE`。
- `.config`: 同步记录 `CONFIG_RT_USING_MESSAGEQUEUE=y`。
- `tests/SConstruct`: 注册事件 formatter、logger 成功路径和 logger 失败路径主机测试。
- `tools/test.sh`: 将 IMU 实时线程同步日志检查纳入完整测试入口。
- `docs/requirements/firmware-behavior.md`: 将诊断日志行为改为异步、可丢弃，并说明不参与 IMU 实时路径。

---

### Task 1: Define And Test Log Event Formatting

**Files:**
- Create: `tests/kernel/logging/test_imu_log_event.c`
- Create: `src/kernel/logging/imu_log_event.h`
- Create: `src/kernel/logging/imu_log_event.c`
- Modify: `tests/SConstruct:26-113`

**Interfaces:**
- Produces `imu_log_event_kind_t`, `imu_log_event_t`, and
  `size_t imu_log_event_format(const imu_log_event_t *event, char *buffer, size_t capacity)`.
- `imu_log_event_t` contains one event kind, two IMU state values, two chip IDs,
  one `imu_diagnostics_t`, and one telemetry drop count. It contains no pointer.
- State values use the existing `imu_state_t`; initial state uses
  `IMU_INITIALIZING` and a separate `IMU_LOG_INITIAL_STATE` kind.

- [ ] **Step 1: Write the failing formatter tests**

Add tests for these exact outputs:

```c
assert_format(
    (imu_log_event_t){
        .kind = IMU_LOG_INITIAL_STATE,
        .state = IMU_INITIALIZING,
    },
    "IMU state: initializing\n");
assert_format(
    (imu_log_event_t){
        .kind = IMU_LOG_STATE,
        .previous_state = IMU_CALIBRATING,
        .state = IMU_RUNNING,
    },
    "IMU state: calibrating -> running\n");
assert_format(
    (imu_log_event_t){
        .kind = IMU_LOG_IDS,
        .accel_id = 0x1e,
        .gyro_id = 0x0f,
    },
    "BMI088 IDs: accel=0x1e gyro=0x0f\n");
assert_format(
    (imu_log_event_t){
        .kind = IMU_LOG_CALIBRATION_COMPLETE,
    },
    "IMU calibration complete\n");
```

Add a diagnostic event test with all counters set to known values and assert the
exact existing line format, including `telemetry_drop` and `reinit`.

Add a bounded-buffer test that passes a buffer smaller than the output and checks
that the return value is the required formatted length while the buffer remains
NUL-terminated. Add invalid-argument tests for `NULL` event, `NULL` buffer, and
zero capacity.

- [ ] **Step 2: Register and run only the new test to verify it fails**

Add this host target to `tests/SConstruct`:

```python
test_imu_log_event = host_test(
    'test_imu_log_event',
    [
        'tests/kernel/logging/test_imu_log_event.c',
        'src/kernel/logging/imu_log_event.c',
    ],
    ['src/kernel', 'src/modules'],
)
Alias('test_imu_log_event', test_imu_log_event)
```

Run:

```bash
./tools/test.sh test_imu_log_event
```

Expected result before implementation: compilation fails because the event
header and formatter do not exist.

- [ ] **Step 3: Implement the minimal pure C event formatter**

Define the four event kinds and the fixed event structure in the header. In the
implementation, use `snprintf` with a local state-name helper and return the
would-have-been formatted length. For an invalid kind, format nothing and return
0. For `NULL` or zero capacity, return 0 without dereferencing the arguments.

Use the existing exact diagnostic format:

```text
IMU errors: spi=%u accel_overrun=%u gyro_overrun=%u rejected_dt=%u long_gap=%u telemetry_drop=%u reinit=%u\n
```

- [ ] **Step 4: Run the focused test to verify it passes**

Run:

```bash
./tools/test.sh test_imu_log_event
```

Expected result: the new formatter test passes with exit code 0.

---

### Task 2: Add The Nonblocking RT-Thread Logger Service

**Files:**
- Create: `src/kernel/logging/imu_log_service.h`
- Create: `src/kernel/logging/imu_log_service.c`
- Create: `src/kernel/logging/SConscript`
- Create: `tests/kernel/logging/test_imu_log_service.c`
- Create: `tests/kernel/logging/test_imu_log_service_failure.c`
- Create: `tests/fakes/rtthread.h`
- Create: `tests/fakes/fake_rtthread_log.c`
- Create: `tests/fakes/fake_rtthread_log.h`
- Modify: `SConscript:9-22`
- Modify: `rtconfig.h:14-18`
- Modify: `.config:9-18`

**Interfaces:**
- `bool imu_log_service_init(void)` initializes the static queue and starts the
  logger thread once. A failure disables logging and returns `false`.
- `bool imu_log_submit(imu_log_event_t event)` performs one zero-wait enqueue;
  it returns `false` and increments a saturating drop count on a full queue or
  disabled service.
- `uint32_t imu_log_drop_count(void)` returns the current drop count.

- [ ] **Step 1: Add failing service tests with an explicit RT-Thread fake**

Create `tests/fakes/rtthread.h` with only the types and declarations used by
`imu_log_service.c`: `rt_err_t`, `rt_size_t`, `rt_uint8_t`, `rt_uint32_t`,
`rt_int32_t`, `struct rt_messagequeue`, `struct rt_thread`, `rt_mq_init`,
`rt_mq_send`, `rt_mq_recv`, `rt_thread_init`, `rt_thread_startup`, `rt_kprintf`,
`RT_EOK`, `RT_EFULL`, `RT_WAITING_FOREVER`, `RT_ALIGN_SIZE`, and `rt_align`.

Define the fake queue struct with message size, capacity, and count. Define
`RT_MQ_BUF_SIZE(msg_size, max_msgs)` in the fake header as
`((msg_size) * (max_msgs))`; the fake `rt_mq_init` then computes capacity as
`pool_size / msg_size`. Implement the fake functions once in
`tests/fakes/fake_rtthread_log.c`: `rt_mq_send` increments count when space
exists and returns `-RT_EFULL` when full; `rt_mq_recv` decrements count or returns
`-RT_EEMPTY`; thread startup returns the configured result without running the
logger entry; and `rt_kprintf` records nothing. Expose
`fake_rtthread_log_reset()`, `fake_rtthread_log_set_thread_init_result()`, and
`fake_rtthread_log_set_thread_startup_result()` in the fake helper header.

Compile the service test with sources
`tests/kernel/logging/test_imu_log_service.c`,
`src/kernel/logging/imu_log_service.c`, and
`src/kernel/logging/imu_log_event.c`, plus
`tests/fakes/fake_rtthread_log.c`, using `tests/fakes` before production include
paths. Compile the failure test with the same sources and fake.

`test_imu_log_service.c` must call `imu_log_service_init()`, submit exactly 8
events, assert all 8 succeed, submit a ninth event, assert it returns immediately
as false, and assert `imu_log_drop_count() == 1`.

`test_imu_log_service_failure.c` must configure fake thread initialization to
return an error, assert `imu_log_service_init()` returns false, then call
`imu_log_submit()` and assert false. Do not depend on UART output or a sleeping
thread in either test.

Register `test_imu_log_service` in `tests/SConstruct` and run the focused test;
register `test_imu_log_service_failure` with the same source list and run both;
verify they fail before the service implementation because the service header and
source do not exist.

Use these concrete target definitions:

```python
logging_test_include_paths = ['tests/fakes', 'src/kernel', 'src/modules']
logging_service_sources = [
    'src/kernel/logging/imu_log_service.c',
    'src/kernel/logging/imu_log_event.c',
    'tests/fakes/fake_rtthread_log.c',
]
test_imu_log_service = host_test(
    'test_imu_log_service',
    ['tests/kernel/logging/test_imu_log_service.c'] + logging_service_sources,
    logging_test_include_paths,
)
Alias('test_imu_log_service', test_imu_log_service)
test_imu_log_service_failure = host_test(
    'test_imu_log_service_failure',
    ['tests/kernel/logging/test_imu_log_service_failure.c'] + logging_service_sources,
    logging_test_include_paths,
)
Alias('test_imu_log_service_failure', test_imu_log_service_failure)
```

- [ ] **Step 2: Implement the static queue and logger thread**

Use these constants and storage:

```c
enum {
    IMU_LOG_THREAD_PRIORITY = 20u,
    IMU_LOG_THREAD_STACK_SIZE = 512u,
    IMU_LOG_QUEUE_CAPACITY = 8u,
    IMU_LOG_BUFFER_SIZE = 128u,
};

static struct rt_messagequeue log_queue;
static rt_uint8_t log_queue_buffer[RT_MQ_BUF_SIZE(
    sizeof(imu_log_event_t), IMU_LOG_QUEUE_CAPACITY)];
static struct rt_thread log_thread;
rt_align(RT_ALIGN_SIZE)
static rt_uint8_t log_thread_stack[IMU_LOG_THREAD_STACK_SIZE];
```

Initialize the queue with `rt_mq_init()` using the static buffer. Start the
logger thread with `rt_thread_init()` and `rt_thread_startup()`. The logger loop
must call `rt_mq_recv(&log_queue, &event, sizeof event, RT_WAITING_FOREVER)`,
format into its local buffer with `imu_log_event_format()`, and call
`rt_kprintf("%s", buffer)` only from this thread.

`imu_log_submit()` must call `rt_mq_send(&log_queue, &event, sizeof event)`;
`rt_mq_send()` is the zero-wait API in the vendored RT-Thread implementation.
Do not use `rt_mq_send_wait()` with a nonzero timeout. Increment the drop count
on any non-`RT_EOK` result, saturating at `UINT32_MAX`.

Make initialization idempotent. If queue or thread initialization fails, set a
disabled flag and never perform a synchronous fallback. Keep the logger thread
below telemetry priority so a blocked console cannot delay telemetry scheduling.

- [ ] **Step 3: Run focused queue/service tests to verify they pass**

Run:

```bash
./tools/test.sh test_imu_log_service
./tools/test.sh test_imu_log_service_failure
```

Expected result: all 8 enqueues succeed, the ninth enqueue fails without waiting,
the drop counter increments, and disabled-service behavior passes.

- [ ] **Step 4: Add the firmware build group and configuration**

The logging `SConscript` must compile both event and service sources with the
same C11 warning flags as the kernel services and expose `src/kernel` and
`src/modules` include paths. Add `RT_USING_MESSAGEQUEUE` to both `rtconfig.h`
and `.config`, then add the logging SConscript to the top-level script before
the IMU service group.

Run:

```bash
./tools/build.sh
```

Expected result: the firmware compiles and the size check passes. If the ARM
toolchain is unavailable, record that exact toolchain error and continue with
host tests; do not replace the verification with a host-only claim.

---

### Task 3: Move Every IMU Log Call To The Async Service

**Files:**
- Modify: `src/kernel/imu/imu_service.c:88-205`
- Modify: `src/kernel/imu/imu_service.c:443-450`
- Modify: `src/kernel/imu/imu_service.c:621-715`
- Modify: `src/kernel/imu/SConscript:19-25`
- Create: `tests/scripts/test_no_imu_thread_logging.sh`

**Interfaces:**
- Consumes `imu_log_submit()` and `imu_log_event_t` from
  `logging/imu_log_service.h` and `logging/imu_log_event.h`.
- Does not change `imu_service_init()` public signature or any IMU policy API.

- [ ] **Step 1: Write a static regression check for forbidden direct logging**

Create an executable shell test containing this exact check:

```bash
if rg -n 'rt_kprintf' src/kernel/imu/imu_service.c; then
    printf 'IMU service must not call rt_kprintf\n' >&2
    exit 1
fi
```

This specifically prevents future reintroduction of blocking I/O into the
real-time thread.

Run the check before the migration and verify it fails because the current file
contains direct logs.

- [ ] **Step 2: Replace initialization, state, ID, and completion logs**

Replace each direct output with a fixed event submission:

```c
(void)imu_log_submit((imu_log_event_t){
    .kind = IMU_LOG_INITIAL_STATE,
    .state = runtime->state,
});
```

Use `IMU_LOG_STATE` for `transition_state`, `IMU_LOG_IDS` for `log_ids_once`,
and `IMU_LOG_CALIBRATION_COMPLETE` for completion. Keep the existing one-shot
flags so the event frequency and observable messages remain unchanged.

- [ ] **Step 3: Replace periodic diagnostics without changing scheduling**

In `log_diagnostics_if_due()` preserve the current due check and deadline update,
then construct one `IMU_LOG_DIAGNOSTICS` event from the current diagnostics and
`telemetry_drops_read()`:

```c
runtime->diagnostics_deadline = now + RT_TICK_PER_SECOND;
(void)imu_log_submit((imu_log_event_t){
    .kind = IMU_LOG_DIAGNOSTICS,
    .diagnostics = *diagnostics,
    .telemetry_drops = telemetry_drops_read(),
});
```

The event submission must not be placed inside an interrupt handler and must not
alter `housekeeping_wait()` or any sample-processing order.

- [ ] **Step 4: Start logging before the IMU thread and make failure nonfatal**

Call `imu_log_service_init()` from `imu_service_init()` before
`rt_thread_startup(&imu_thread)`. Do not fail `imu_service_init()` solely because
the logger cannot start; the logger remains disabled and no synchronous fallback
is allowed. Preserve all existing failure cleanup for clock, UART, event,
thread, and BMI088 adapter resources.

- [ ] **Step 5: Run the forbidden-call check and focused tests**

Run:

```bash
./tools/test.sh test_imu_log_event
./tools/test.sh test_imu_log_service
./tools/test.sh test_imu_log_service_failure
```

Expected result: no direct `rt_kprintf` remains in `imu_service.c`, both logging
tests pass, and the IMU policy tests still assert overrun-reset behavior.

---

### Task 4: Update Requirements And Complete Regression Verification

**Files:**
- Modify: `docs/requirements/firmware-behavior.md:71-81`
- Modify: `tests/scripts/test_no_imu_thread_logging.sh`
- Modify: `tools/test.sh:41-46`

- [ ] **Step 1: Extend the logging regression assertion**

Append checks that `docs/requirements/firmware-behavior.md` contains the terms
`异步`, `队列`, and `gyro_overrun`, so the runtime requirement cannot silently
revert to synchronous diagnostic output. Keep the existing diagnostic field
names and one-second cadence.

The script must use fixed-string checks and return nonzero when any term is
missing:

```bash
for required in '异步' '队列' 'gyro_overrun'; do
    if ! rg -Fq "$required" docs/requirements/firmware-behavior.md; then
        printf 'logging requirement missing: %s\n' "$required" >&2
        exit 1
    fi
done
```

- [ ] **Step 2: Update the requirements document**

Change the diagnostics statement to specify that IMU processing only enqueues a
fixed event, logger output runs at a lower priority, and UART backpressure cannot
block BMI088 processing.

- [ ] **Step 3: Run the focused static check before wiring it into the test runner**

Run:

```bash
sh tests/scripts/test_no_imu_thread_logging.sh
```

Expected result after Task 3: the check passes and confirms that
`src/kernel/imu/imu_service.c` contains no `rt_kprintf` call and the requirements
document contains all three required terms.

- [ ] **Step 4: Add the static check to the full test runner**

Append this command to `tools/test.sh` after the existing repository checks:

```bash
sh tests/scripts/test_no_imu_thread_logging.sh
```

- [ ] **Step 5: Run the full host test suite**

Run:

```bash
./tools/test.sh
```

Expected result: all host tests, size fixture checks, test-runner checks,
repository layout checks, debug-probe checks, and the IMU logging static check
exit 0.

- [ ] **Step 6: Build the firmware and inspect resource usage**

Run:

```bash
./tools/build.sh
```

Confirm the final output contains a successful firmware size check and that
static SRAM remains below the 20 KiB device limit.

- [ ] **Step 7: Perform hardware acceptance checks**

With BMI088 stationary, test both a readable USART1 console and a disconnected
or non-consuming console. Confirm calibration completes in approximately two
seconds, `gyro_overrun` does not increase due to logging, telemetry continues,
and a deliberately full log queue only increments log drops.

## Plan Self-Review

- Spec coverage: the plan covers asynchronous ownership, fixed events, zero-wait
  enqueue, queue-full behavior, logger priority, initialization failure, resource
  limits, tests, documentation, and hardware acceptance.
- Placeholder scan: commands, paths, constants, and interfaces are concrete;
  no implementation step relies on an unspecified choice.
- Type consistency: `imu_log_event_t` is produced by the event module and consumed
  by both the service and formatter; `imu_log_submit()` accepts that exact type;
  `imu_log_event_format()` returns a bounded `size_t` length as used by logger.

## Execution Record (2026-08-26)

- Software implementation, focused tests, full host suite, static logging gate,
  SCons firmware build, and Flash/SRAM verification are complete.
- Final SCons size: Flash 43,976 bytes, static SRAM 6,592 bytes.
- Hardware acceptance in Task 4 Step 7 was not executed because no target board
  or console setup is available in this environment.
