# new_huokong 火控系统分析（dianhuo.c）

> 记录时间：2026-08-10
> 分析对象：`applications/dianhuo.c` / `dianhuo.h`
> 姊妹篇：[new_huokong_项目知识.md](new_huokong_项目知识.md)（总览）、
> [new_huokong_上位机通信与CAN电机驱动分析.md](new_huokong_上位机通信与CAN电机驱动分析.md)（指令来源链路）

## 1. 系统概述

管理 4 个"蛋"（弹）的**充电 → 点火 → 泄放**电路，纯 GPIO 控制（无 ADC 电压采样）。
核心安全设计是**软件互锁状态机**：任何点火操作必须依次通过 0解（总解锁）→ 一级充电 → 二级充电三道门，且每一步校验"有弹检测"。

上电默认状态是安全的：`MX_GPIO_Init`（dianhuo.c:12，`INIT_BOARD_EXPORT` 板级初始化阶段执行）把所有火控输出拉低，唯独 **Discharge 默认拉高**（dianhuo.c:33）——即开机即处于泄放状态；`fire_control_task` 启动后还会再执行一次 `Discharge_control()`（dianhuo.c:305）。

## 2. GPIO 引脚映射（dianhuo.h:9-71）

| 功能 | 引脚 | 方向 | 说明 |
|------|------|------|------|
| Fire_1~4（点火） | PA2 / PB0 / PE12 / PD9 | 输出 | 100ms 高脉冲点火 |
| Charge_V（一级充电总开关） | PD10 | 输出 | 全炮位共用 |
| Charge_1~4（二级充电） | PA3 / PA5 / PE10 / PD8 | 输出 | 逐蛋独立 |
| Discharge（泄放） | PB12 | 输出 | **默认高电平（安全态）** |
| Check_V（检测供电） | PC5 | 输出 | 有弹检测时临时上电 |
| Weapon1~4_STA（有弹检测） | PA4 / PE9 / PE14 / PB11 | 输入下拉 | **低电平=有弹**（读到 0 置位 `Weapon_Sta`） |
| Limit_L / Limit_R | PC1 / PC3 | 输入上拉 | **已配置但全工程无人读取**（死代码，见 §6） |

`Weapon_Sta`（dianhuo.c:112）低 4 位缓存有弹状态：bit0=蛋1 … bit3=蛋4，1=有弹。

## 3. 互锁状态机

两级全局状态 + 每蛋充电层级（dianhuo.h:85-90）：

```
fire_unlock_flag: FIRE_UNLOCK_NONE(0x00) ──0x01 0解──▶ FIRE_UNLOCK_OK(0x01)   [无再上锁指令]
fire_charge_level[1..4]: NONE(0) ──0x02 一解──▶ LEVEL_1 ──0x03 二解──▶ LEVEL_2 ──0x04 点火──▶ NONE
```

各指令的准入条件与硬件动作：

| 指令 | 码 | 准入条件 | 硬件动作 |
|------|-----|---------|---------|
| 泄放 | 0x08 | **无条件**（未解锁也可执行） | 全 Charge 关 → Discharge 开 2s（dianhuo.c:225） |
| 0解 | 0x01 | 无条件 | 解锁 + 清空充电层级 + 泄放一次（dianhuo.c:338） |
| 查询 | 0x05 | 无条件（分支排在解锁检查之前，dianhuo.c:357） | 仅反馈状态 |
| 一解 | 0x02 | 已解锁 | Discharge 关 → **Charge_V 开**（全炮位总充电）延时 1s；`fire_charge_level[1..4]` **全部置 1**（dianhuo.c:391-394） |
| 二解 | 0x03 | 已解锁 + `fire_id` 掩码非 0 + 该蛋**有弹** + 已一级 | 对应 `Charge_x` 开，延时 1s |
| 点火 | 0x04 | 已解锁 + 掩码非 0 + 该蛋**有弹** + 已二级 | `Fire_x` 高 100ms 脉冲，层级清回 NONE（dianhuo.c:269） |

**`fire_id` 实际是位掩码而非 ID**：二解/点火分支按 `fire_mask & (0x01 << (egg_idx-1))` 遍历（dianhuo.c:418），0x03 表示蛋 1+2 同时操作、0x04 表示**蛋 3**。而 dianhuo.h:80-83 定义的 `FIRE_ID_1~4 = 0x01~0x04` 以及项目知识文档里"蛋 ID 0x01~0x04"的说法都是按枚举值理解的——**上位机若按枚举发 0x03 想操作蛋 3，实际会同时充蛋 1 和蛋 2**。这是协议歧义，必须与上位机侧确认。

## 4. 任务时序（fire_control_task，优先级 15，栈 512B）

启动序列：`check_control()`（首次有弹检测）→ `Discharge_control()`（2s 泄放）→ 500ms 延时 → 进主循环。

主循环名义 `mdelay(3)`，但每轮都调 `check_control()`（dianhuo.c:311），其内部：

```c
// dianhuo.c:115
Check_V_ON;
rt_thread_mdelay(100);        // 检测电路上电稳定
for (i = 0; i < 20; i++) { …连续读4路STA，计数>15才翻转 Weapon_Sta… }
Check_V_OFF;
```

因此**实际循环周期 ≥103ms**，`Check_V` 占空比约 97%（基本常开）。所谓"防抖"是 20 次背靠背读取（无采样间隔，整段耗时微秒级），只能滤掉纳秒级毛刺，**对机械触点抖动（毫秒级）无效**——真正的抖动抑制其实来自 103ms 的循环节拍。

每轮循环把 `fire_charge_level` / `fire_unlock_flag` / `Weapon_Sta` 同步进 `fire_feedback` 缓存；每条指令执行完都调 `hostpc_send_fire_state()` 主动上报一帧 6 字节状态（解锁 + 有弹位图 + 4 蛋层级，hostpc.c:57）。

指令消费模型：轮询全局 `hostpc_cmd`（单缓冲），处理完置 `cmd_valid = RT_FALSE`。临界区（`rt_enter_critical`，调度器锁）只包住标志读写，长阻塞动作（1s/2s 充放电延时）都在临界区外执行。

## 5. ⚠️ 发现的问题

1. **点火后充电通道不关断**：`fire_shot` 只发点火脉冲并清软件层级标志（dianhuo.c:296），**对应的 `Charge_x` 和总开关 `Charge_V` 仍保持 ON**。点火瞬间充电回路仍接通，且此后软件标志（NONE）与硬件引脚（充电中）失配——若再次走一解→二解流程，`Charge_x` 其实早已开着。只有泄放/0解会真正复位引脚。建议 `fire_shot` 内先 `Charge_x_OFF`（必要时连同 `Charge_V_OFF`）再点火。
2. **一级充电给无弹炮位也置层级标志**：一解不检查 `Weapon_Sta`，四个蛋无条件全标 `CHARGE_LEVEL_1`（dianhuo.c:391）。虽然二解/点火有有弹校验兜底，但反馈帧里"无弹却已一级充电"的状态会误导上位机。
3. **指令覆盖窗口**：泄放/充电期间任务阻塞 1~2s，此间上位机新指令会直接覆盖单缓冲的 `hostpc_cmd`（接收解析任务优先级 20 仍在跑），阻塞结束后处理的是"最新一条"，中间指令静默丢失且无 NACK。对火控这种安全敏感链路，建议改消息队列或至少反馈"忙"状态。
4. **无再上锁机制**：`fire_unlock_flag` 一旦置 OK 永不回退（泄放也不清），断链/任务重启前系统一直处于解锁态。结合上位机通信文档 §"断链保护缺失"，建议通信超时后自动回锁 + 泄放。
5. **`fire_id` 掩码/枚举歧义**：见 §3 加粗部分，协议层面需要与上位机对齐。
6. **未解锁提示分支的死代码**：未解锁分支把 `CMD_QUERY_FIRE_STATE` 也列入提示条件（dianhuo.c:515），但查询在更早的 else-if 分支已被无条件处理，永远走不到这里。

## 6. 死代码清单（清理候选）

- `Singer_fire` / `Double_fire` / `All_fire`：空函数（dianhuo.c:202-210）。
- `Charge_control()`（全炮位充电，dianhuo.c:213）：全工程无调用。
- `Limit_L` / `Limit_R` 限位开关：GPIO 已初始化（dianhuo.c:103）、读取宏已定义（dianhuo.h:70-71），但全工程无人读取——云台标定实际用的是堵转电流探测（zengwen.c），这两个开关是预留未接线或废弃方案。
- `fire_charge_level[5]` / `egg_charge[5]`：用 1~4 下标存 4 个蛋，[0] 恒为 0 浪费一字节（换来下标即蛋号的可读性，可保留但应注释）。
