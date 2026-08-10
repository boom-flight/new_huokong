# new_huokong 项目知识文档

> 记录时间：2026-08-10
> 工程路径：`/home/noskillzheng/laozhang_project/new_huokong`
> 深入分析姊妹篇：
> [云台控制架构与RMCS对比](new_huokong_云台控制架构与RMCS对比.md) ·
> [IMU融合滤波分析与RMCS对比](new_huokong_IMU融合滤波分析与RMCS对比.md) ·
> [火控系统分析](new_huokong_火控系统分析.md) ·
> [上位机通信与CAN电机驱动分析](new_huokong_上位机通信与CAN电机驱动分析.md)

## 1. 项目定位

基于 **RT-Thread RTOS** 的嵌入式固件工程，硬件平台为 **DJI RoboMaster A 板**（主控 STM32F427VGTx，Cortex-M4 @180MHz，2MB Flash / 128KB+192KB RAM）。

开发环境：**仅 Linux + VS Code**（SCons + GCC + OpenOCD），不使用 Keil。

实现的是一套**两轴云台（挂架）+ 四路火控投放系统**：

- **云台**：CAN 总线驱动瓴控 LK 系列电机，Yaw/Pitch 双轴；上电打机械限位标定零点，之后进入自稳（增稳）模式，支持角度定位与一键回中。
- **航向融合**：电机编码器角度 + 板载 IMU（JY62）姿态 + 上位机/飞机下发的正东航向角（含时间戳对齐）做融合滤波，得到绝对航向。协议中出现"平衡抛"，推测为挂载在飞行平台上的稳定投放挂架。
- **火控**：管理 4 个"蛋"（弹）的充电/点火/泄放电路，带严格互锁状态机：
  `0解总解锁(0x01) → 一级充电(0x02) → 二级充电(0x03) → 点火(0x04)`，另有泄放（0x08）、状态查询（0x05）、有弹检测与限位开关。

## 2. 目录结构

```
new_huokong/
├── applications/        # 业务代码（核心，全部自研）
├── board/               # 板级支持：board.c/h、CubeMX_Config、链接脚本、Kconfig
├── libraries/           # STM32F4xx_HAL 库 + HAL_Drivers（RT-Thread 驱动适配层）
├── rt-thread/           # RT-Thread 内核源码（整体 vendored，非子模块）
├── packages/            # RT-Thread 软件包目录（当前为空）
├── .script/             # 项目脚本（编译/烧录/调试/串口等，见第 6/7 节）
├── rtconfig.py          # SCons 工具链配置（RTT_CC=gcc）
├── rtconfig.h / .config # menuconfig 生成的内核与 BSP 配置
├── SConstruct/SConscript# SCons 构建脚本
├── project.uvprojx      # 遗留 Keil 工程，已不使用（仅历史存档）
└── rtthread.bin         # 编译产物（约 80KB）
```

注意：**不是 git 仓库**。

## 3. 应用层模块（applications/）

所有模块通过 `INIT_APP_EXPORT` 注册初始化函数，开机自动创建并启动线程；模块用中文拼音命名。

| 文件 | 职责 | 关键点 |
|------|------|--------|
| `main.c` | PID 初始化、触发上电标定、LED 心跳 | `PID_Init` 参数在此调；标定完成后置双轴 `AXIS_MODE_STABILIZE` |
| `motor.c` | CAN1 驱动 LK 电机 | 直接用 HAL（未走 RT-Thread CAN 框架），Yaw=0x141、Pitch=0x142；命令字 0xA4 角度/0xA2 速度/0x92 查询（0x80/0x81/0x88 仅有宏定义未使用）；CAN 发送任务 + 约 11ms 角度轮询任务 |
| `imu.c` | JY62 IMU 数据解析 | 直接用 HAL 的 USART2 @115200 中断收帧；输出欧拉角/角速度/加速度；msh 命令 `msh_imu_print_euler` |
| `pid.c` | 通用 PID 控制器 | `mpu_pid_pitch` / `mpu_pid_yaw` |
| `zengwen.c`（增稳） | 轴标定 + 控制模式管理 | 找左右机械限位算零点；`AxisCtrlMode_t`: STABILIZE / POSITION / CALIBRATION；全局 `axis[YAW_NO=0 / PITCH_NO=1]` |
| `gimbal_fusion.c` | Yaw 航向融合滤波 | 编码器 + IMU + 飞机航向角，PI 补偿（Kp=1.2, Ki=0.01, DT=10ms） |
| `hostpc.c` | 上位机通信协议 | UART4 + DMA + 空闲中断；帧头 0xA5，12/16/21 字节帧；4 线程：rx 解析 / 指令执行 / tx 发送 / 角度上报 |
| `dianhuo.c`（点火） | 火控执行 | Fire_1~4 / Charge_1~4 / Discharge / Weapon_STA 有弹检测 / Limit_L,R 限位等 GPIO；互锁状态机；反馈结构 `fire_state_feedback_t` |

## 4. 上位机协议速查（hostpc.h / dianhuo.h）

- 帧头 `0xA5`；帧类型 0x00=控制、0x01=反馈；末尾校验和（`hostpc_calc_checksum`）。
- 控制指令：`0xA4` Yaw 角度、`0xA6` Pitch 角度、`0x00` 一键回中、`0x06` 正东航向角、`0xAA` 读电机状态。
- 火控指令：`0x01` 0解、`0x02` 一解（一级充电）、`0x03` 二解（二级充电）、`0x04` 点火、`0x05` 查询状态、`0x08` 泄放（fire_id 字节实为**位掩码**：bit0~bit3 对应蛋 1~4，0x04=蛋3、0x0F=全部）。
- 反馈：`0x91` 姿态、`0x93` 航向角+时间戳（`0x92` 挂架角度已定义但为死代码，未被调用）。

## 5. 外设占用

| 外设 | 用途 | 接入方式 |
|------|------|----------|
| CAN1 | LK 电机（Yaw/Pitch） | 直接 HAL，自定义中断（`.config` 中 BSP_USING_CAN 关闭） |
| USART2 | JY62 IMU @115200 | 直接 HAL + 中断 |
| UART4 | 上位机协议 | 直接 HAL + DMA + 空闲中断 |
| UART6 | RT-Thread msh 控制台 | RT-Thread 设备框架（.config 唯一启用的 BSP 串口） |
| GPIO | 火控充放电/点火/检测/限位、LED | HAL 宏封装于 dianhuo.h |

架构特点：**业务外设全部绕过 RT-Thread 设备框架直接用 STM32 HAL**，RT-Thread 主要提供线程调度、时钟节拍和 msh 控制台。

## 6. 编译（唯一环境：Linux + SCons + GCC，不使用 Keil）

### 首次准备（Arch，只需一次）

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-newlib arm-none-eabi-gdb openocd bear picocom
```

### 日常编译（三选一）

```bash
# ① 项目脚本（推荐）
.script/build.sh          # 编译，额外参数透传给 scons
.script/clean.sh          # 清理
.script/rebuild-index.sh  # 全量重编 + 生成 compile_commands.json（clangd 索引）
.script/menuconfig.sh     # 内核/BSP 配置界面
```

- ② VS Code 内 **Ctrl+Shift+B**（默认 build 任务）；
- ③ 手动：`cd new_huokong && RTT_EXEC_PATH=/usr/bin scons -j$(nproc)`。

产物：`rt-thread.elf`（调试用）+ `rtthread.bin`（烧录用，约 80KB，POST_ACTION 自动 objcopy 生成，rtconfig.py:62）。所有脚本与 VS Code 任务已内置 `RTT_EXEC_PATH=/usr/bin`、`RTT_CC=gcc`，无需手动传环境变量。

### GCC 迁移修复记录（2026-08-10，已全部修复）

历史代码只过过 armcc，迁到 GCC 时修了以下问题，如遇回归可对照排查：

1. **`rt-thread/tools/gcc.py` 的 `GetGCCRoot`**：`EXEC_PATH=/usr/bin` 时硬编码返回 `/usr/lib/arm-none-eabi`，但 Arch 的 newlib 头在 `/usr/arm-none-eabi/include/`，导致 `cconfig.h` 缺 `HAVE_SIGEVENT` 等宏，`libc_signal.h` 与 newlib 重复定义报错。已加路径回退。**`scons -c` 会删除 `cconfig.h`，重新生成依赖此补丁**。
2. **`applications/SConscript` 原先不存在**：SCons 从不编译业务代码，`main` 落到内核弱符号导致链接失败（旧 60KB 的 bin 里根本没有业务代码）。已补建，并加 `LIBS=['m']` 链接数学库（gimbal_fusion 用 `sinf/cosf/atan2f`）。
3. **`hostpc.h` 多余声明**：`hostpc_rx_parse_task` 在 .c 中为 static，头文件声明冲突（armcc 容忍、gcc 报错），已删除声明。

### Ubuntu 环境差异（本工程主环境为 Arch，如换 Ubuntu 参照此节）

工作流完全相同（`.script/` 脚本、VS Code 任务、`RTT_EXEC_PATH=/usr/bin` 都不变），差异只在系统层：

1. **安装命令**（对应 Arch 那条 pacman）：

   ```bash
   sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi gdb-multiarch openocd bear picocom
   ```

2. **GDB 名字不同**：Ubuntu 没有 `arm-none-eabi-gdb` 包，用 **`gdb-multiarch`** 代替。
   - `.script/gdb.sh` 已自动探测两者，无需改动；
   - Cortex-Debug（F5 调试）需在 `launch.json` 各配置里加 `"gdbPath": "gdb-multiarch"`（或 VS Code 设置 `cortex-debug.gdbPath`）。
3. **newlib 头文件路径**：Ubuntu/Debian 在 `/usr/lib/arm-none-eabi/`——这正是 RT-Thread `gcc.py` 原本硬编码的路径，所以第 6 节修复记录中的路径补丁在 Ubuntu 上不触发（回退分支只在 Arch 生效），两边都能正确生成 `cconfig.h`。
4. **串口权限**：Ubuntu 串口属 `dialout` 组（Arch 是 `uucp`）：

   ```bash
   sudo usermod -aG dialout $USER   # 注销重登生效
   ```

   ST-Link 的 udev 规则随 openocd 包自带（`60-openocd.rules`，要求用户在 `plugdev` 组，Ubuntu 默认已在）；若烧录报 USB 权限错，检查 `groups` 输出并重插调试器。
5. **CH340/CP210x USB 串口被占用**：Ubuntu 桌面版的 `brltty` 服务会抢占这类转串口芯片导致 `/dev/ttyUSB0` 消失，`sudo apt remove brltty` 即可。
6. **apt 的 GCC 版本较旧**（如 22.04 为 GCC 10）：一般够用；若需新版本，从 ARM 官网下载工具链 tarball 解压后，把 `RTT_EXEC_PATH`（`.script/env.sh` 与 `.vscode/tasks.json` 中）改为其 `bin/` 目录即可，`gcc.py` 对非 `/usr/bin` 路径按标准工具链布局探测，无需再打补丁。

### 容器化开发（qzhhhi/rmcs-develop，与 creeper-flight 同款）

复用 creeper-flight（RMCS）的开发镜像 **`qzhhhi/rmcs-develop:latest-full`**（ros:jazzy / Ubuntu 24.04 底座，本机已有，约 9.9GB），不自建 Dockerfile。镜像本身没有嵌入式工具链，靠 `.script/setup-ubuntu.sh` 在容器首次创建时自动安装（走的就是上面 Ubuntu 差异那套 apt 包）。

**配置文件**：

| 文件 | 作用 |
|------|------|
| `.devcontainer/devcontainer.json` | VS Code Dev Container 入口；postCreate 自动跑 `setup-ubuntu.sh`；预装 clangd + Cortex-Debug 扩展 |
| `.devcontainer/docker-compose.yml` | 容器定义：privileged + host 网络 + 绑定 `/dev`（ST-Link/串口直通）+ 工程挂载到 `/workspaces/new_huokong`；另挂载宿主机 AI 工具链配置（见下） |
| `.script/setup-ubuntu.sh` | Ubuntu 一键配置（容器 postCreate 或裸 Ubuntu 机器通用，可重复执行）；额外做 `arm-none-eabi-gdb → gdb-multiarch` 符号链接，Cortex-Debug 与 `gdb.sh` 无需改配置；有 npm 时顺带全局安装 claude / codex / opencode CLI |
| `.script/container.sh` | 不经 VS Code 直接进容器（compose up + exec zsh，首次自动装工具链） |

**AI 工具链（与 creeper-flight 思路一致）**：容器内可直接用 **claude / codex / opencode** 三个 CLI（`setup-ubuntu.sh` 经镜像自带的 Node 24 npm 全局安装），登录态与配置通过 compose 挂载宿主机目录复用，无需容器内重新登录：

| 宿主机路径 | 容器内路径 | 供谁用 |
|-----------|-----------|--------|
| `~/.claude/`、`~/.claude.json`、`~/CLAUDE.md` | `/home/ubuntu/` 同名 | Claude Code（含记忆与全局规则） |
| `~/.codex/` | `/home/ubuntu/.codex` | Codex |
| `~/.config/opencode/`、`~/.local/share/opencode/` | `/home/ubuntu/` 同名 | opencode（配置 + auth/存储） |

**使用方式（二选一）**：

- VS Code：装 Dev Containers 扩展 → 打开 new_huokong → **Reopen in Container**，进去后 Ctrl+Shift+B / F5 / `.script/` 全部照常；
- 命令行：`.script/container.sh` 直接进入容器 zsh。

**注意**：

- 工具链装在容器层，`docker compose down`/Rebuild Container 后会重装（postCreate 自动触发，命令行方式由 `container.sh` 自动补装）；`docker compose stop` 只停不删，工具链保留；
- **宿主机与容器共享 `build/` 和 `cconfig.h`**：两边 GCC 版本不同（Arch 16.x / 容器 13.2），切换环境后必须先 `.script/clean.sh` 再编译，避免混用两套编译器的目标文件（cconfig.h 会随 clean 重新按当前环境生成，宏内容两边一致）；
- privileged + `/dev` 绑定下 openocd 烧录、picocom 串口在容器内直接可用；如遇设备权限报错（宿主机组 ID 不匹配），命令前加 `sudo` 即可（容器内 ubuntu 用户免密 sudo）；
- 宿主机（Arch）与容器（Ubuntu）用同一份 `.script/` 与 `.vscode/` 配置，无需任何切换。

## 7. 烧录与运行（ST-Link + OpenOCD）

### 烧录（三选一）

```bash
# ① 项目脚本（推荐）：自动先编译再烧录
.script/flash.sh
```

- ② VS Code 任务 **"flash (ST-Link)"**：自动先构建再烧录；
- ③ VS Code **F5**（Debug 配置）：构建 → 烧录 → 复位断在 `main`，含变量/断点/SVD 外设寄存器视图；已运行的板子用 **Attach** 配置不复位附加。命令行调试可用 `.script/gdb.sh`（后台起 OpenOCD + 前台 GDB）。

底层命令等价于：

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
        -c "program rtthread.bin 0x08000000 verify reset exit"
```

### 上电流程

1. 各模块线程经 `INIT_APP_EXPORT` 自启动；
2. `main` 等 2 秒电机上电 → 依次 Pitch、Yaw 打限位标定；
3. LED3 快闪 7 次 = 标定完成；
4. 双轴进入自稳模式 + `Gimbal_Init()`，等待上位机指令；主循环仅 LED 心跳（500ms）。

### 串口控制台

UART6 接 USB 转串口后：`.script/console.sh`（默认 `/dev/ttyUSB0`，可传设备参数；等价 `picocom -b 115200 /dev/ttyUSB0`，也可用 VS Code 任务"msh 串口控制台"），msh 里可用 `msh_imu_print_euler` 等命令。退出 picocom：Ctrl+A Ctrl+X。

## 8. 工程配置文件清单（2026-08-10 配置，与第 6/7 节配套）

技术栈：**clangd + SCons + Cortex-Debug + OpenOCD（ST-Link）**。

| 文件 | 作用 |
|------|------|
| `new_huokong/.script/` | 项目脚本：`build.sh` / `clean.sh` / `rebuild-index.sh` / `flash.sh` / `menuconfig.sh` / `console.sh` / `gdb.sh`；公共环境在 `env.sh`（自动 cd 工程根目录并导出 `RTT_EXEC_PATH` / `RTT_CC`） |
| `new_huokong/.clangd` | clangd 交叉编译配置（`--target=arm-none-eabi`，查询 arm-none-eabi-gcc） |
| `new_huokong/.vscode/tasks.json` | 6 个任务：build（默认）/ 重建 clangd 索引 / clean / flash（ST-Link）/ menuconfig / picocom 串口；`RTT_EXEC_PATH=/usr/bin` 已内置 |
| `new_huokong/.vscode/launch.json` | Cortex-Debug：Debug（构建→烧录→断在 main）+ Attach（不复位附加） |
| `new_huokong/.vscode/STM32F427.svd` | 外设寄存器视图数据（源自 cmsis-svd-data，2.2MB） |

VS Code 扩展：**clangd** + **Cortex-Debug**（禁用微软 C/C++ 扩展的 IntelliSense 避免冲突）。

## 9. 已知注意事项

- 工程未纳入 git 版本管理，建议 `git init` 并为 `build/`、`*.o`、`.sconsign.dblite` 等加 .gitignore。
- `applications.zip`、`applications地理成功未反.zip` 是历史手工备份，佐证无版本管理。
- `.config` 与实际代码不完全一致（CAN 在 config 中关闭但代码直接用 HAL 开启），改 menuconfig 时注意不要与手写 HAL 初始化冲突。
- `motor.h` 中有标注"以下没有用到"的遗留宏，清理时可删。
- GCC 路径补丁打在 vendored 的 `rt-thread/tools/gcc.py` 内（见第 6 节修复记录），若日后整体替换 rt-thread 目录需重新打上。
- `project.uvprojx` 等 Keil 文件仅作历史存档，不再维护、不保证可编译。
