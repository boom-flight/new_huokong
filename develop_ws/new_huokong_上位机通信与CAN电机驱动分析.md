# new_huokong 上位机通信与 CAN 电机驱动分析（hostpc.c / motor.c）

> 记录时间：2026-08-10
> 分析对象：`applications/hostpc.c`/`hostpc.h`、`applications/motor.c`/`motor.h`
> 姊妹篇：[new_huokong_项目知识.md](new_huokong_项目知识.md)（总览）、
> [new_huokong_火控系统分析.md](new_huokong_火控系统分析.md)（指令消费方）、
> [new_huokong_IMU融合滤波分析与RMCS对比.md](new_huokong_IMU融合滤波分析与RMCS对比.md)（航向角消费方）

## 1. 上位机通信（hostpc.c，UART4 @115200，PA0-TX / PA1-RX）

### 1.1 帧格式全表

所有帧：`0xA5 帧头 + 帧类型 + 指令码 + 数据域 + 8位累加校验和`（校验覆盖除校验位外的全帧）。长度由(帧类型, 指令码)决定，空闲中断按长度白名单收帧（只接受 12/16 字节入向帧，hostpc.c:440）。

**下行（上位机 → 板，控制帧 type=0x00，固定 12 字节）**：

| 偏移 | 0 | 1 | 2 | 3 | 4-5 | 6-9 | 10 | 11 |
|------|---|---|---|---|-----|-----|----|----|
| 内容 | 0xA5 | 0x00 | 指令码 | fire_id/保留 | speed u16 小端 | 角度 i32 小端(0.01°) | 保留 | 校验和 |

指令码：`0xA4` Yaw 角度、`0xA6` Pitch 角度、`0x00` 一键回中（**执行分支被注释**，hostpc.c:333）、`0xAA` 读电机状态（空实现）、火控 `0x01/0x02/0x03/0x04/0x05/0x08`（只用 fire_id 字节，语义见火控文档 §3——**实际是位掩码**）。

**入向反馈帧（飞机 → 板，type=0x01）**：

- `0x93` 航向角+时间戳，16 字节：偏移 3-6 航向 float、7-10 ts1（飞机取角时刻）、11-14 ts2（发送时刻）、15 校验。

**上行（板 → 上位机，type=0x01）**：

- `0x91` 姿态 21 字节：pitch/roll/yaw/time 四个 float + 有弹位图 1 字节（`hk_angle_report_task` 100ms 周期发，yaw 用融合值 `att->yaw_enu`，time 用本地时钟+`cmd_delta_time` 对齐）。注意有弹位图**发送前按位取反**：`(0x0F ^ Weapon_Sta) & 0x0F`（hostpc.c:420），线上 1=无弹，与内部约定相反。
- `0x92` 挂架角度 16 字节：**死代码**，`hostpc_send_angle_feedback` 全工程无调用（已被 0x91 取代）。
- `0x05` 火控状态 12 字节：数据域 6 字节 = 解锁 + 有弹位图（**不取反**，与 0x91 相反！）+ 4 蛋充电层级，每条火控指令执行后发。

### 1.2 数据流与线程架构

```
UART4 IDLE中断+DMA ──▶ hostpc_rx_temp_buf ──sem──▶ rx解析(prio20) ──▶ hostpc_cmd(全局单缓冲)
                                                                        ├─▶ exec任务(prio18, 5ms轮询)──▶ motor_set_multi_angle
                                                                        └─▶ fire_ctl(prio15) / gimbal_fusion(读cmd_yaw)
发送方组帧 ──▶ hostpc_tx_buf(线性缓冲168B) ──tx任务(prio20, 100ms轮询)──▶ DMA发送(sem_tx_idle同步)
```

注意：hostpc.c:575 注释写"指令执行17"，实际初始化传的是 18（hostpc.c:598）。

发送缓冲**不是环形**：读写指针只在"读空"时一起归零（hostpc.c:402），写满返回 `-RT_EFULL` 静默丢帧。发送节流为每 100ms 最多 1 帧 = 10 帧/s，恰好等于 0x91 上报的产生速率——火控反馈一旦突发，缓冲会短暂堆积，靠 8 帧容量吸收。

### 1.3 ⚠️ 发现的问题

1. **`ts1` 未赋值先使用（严重）**：航向帧解析中，时间对齐块在 `memcpy` 取出 ts1 **之前**执行（hostpc.c:263-274）：

   ```c
   float angle, ts1, ts2;              // 局部变量，未初始化
   if (hostpc_cmd.cmd_time_flag == 0) {
       hostpc_cmd.cmd_delta_time = ts1 - get_tick_s();   // ts1 此刻是栈上垃圾值！
       hostpc_cmd.cmd_time_flag = 1;
   }
   memcpy(&angle, &pbuf[3], 4);
   memcpy(&ts1,   &pbuf[7], 4);        // 真正的赋值在后面
   ```

   `cmd_delta_time` 因此是垃圾值，0x91 上报的 time 字段随之失真。修复：把 `if` 块移到三个 `memcpy` 之后。
2. **控制帧 memset 连带清空融合输入（严重）**：解析任务收到**每一条**合法控制帧（以及任何坏帧）都 `memset(&hostpc_cmd, 0, ...)`（hostpc.c:209/220/230），而 `cmd_yaw`（飞机航向）、`cmd_delta_time`、`cmd_time_flag` 与控制指令共用这个结构体。后果：上位机每发一条云台/火控指令，`gimbal_fusion` 的航向基准就跳变为 0，互补滤波把 Yaw 估计往 0° 拉，直到下一帧 0x93 到来；时间对齐也被迫重做（且每次重做都踩问题 1）。这是 IMU 文档"问题 2（无量测有效性检查）"的一个具体放大器。修复：控制指令与航向状态拆成两个结构体，或 memset 改为只清指令相关字段。
3. **解析期间的缓冲竞态**：解析任务用 `rt_enter_critical()`（只锁调度器，不关中断）保护，而 UART ISR 在中断上下文里直接覆写 `hostpc_rx_temp_buf`（hostpc.c:445）。两帧背靠背到达时，第二帧可能在第一帧解析中途覆盖缓冲。低波特率下概率小，但正确做法是解析前把 temp_buf 再拷贝到线程私有缓冲，或用双缓冲。
4. **断链无保护**：无接收超时检测，上位机断链后云台维持最后目标、火控保持解锁（见云台文档 §3.2、火控文档 §5.4）。建议统一加 500ms 级超时：速度置零 + 火控回锁。
5. **两条链路的有弹位图语义相反**：0x91 取反（1=无弹）、0x05 火控反馈不取反（1=有弹），上位机侧极易踩坑，协议文档必须写明。
6. **非对齐指针转型**：`*(int32_t *)&pbuf[6]`（hostpc.c:240）在 Cortex-M4 上能跑（硬件支持非对齐访问），但属 UB 写法，建议统一用 `memcpy`（同文件航向解析已经是 memcpy 风格）。

## 2. CAN 电机驱动（motor.c，CAN1 @1Mbps，PA11/PA12）

### 2.1 硬件与协议

瓴控 MG 系列电机，标准帧：Yaw=0x141、Pitch=0x142。波特率 42MHz / (5×(1+4+4)) = 1Mbps；过滤器 0 掩码全通，FIFO0 中断接收。

| 命令字 | 方向 | 内容 |
|--------|------|------|
| 0xA4 | 发 | 多圈绝对角度闭环：[2-3] 最大限速 u16、[4-7] 目标角度 i32 |
| 0xA2 | 发 | 速度闭环：[4-7] 目标转速 i32（0.01 dps） |
| 0x92 | 发/收 | 多圈角度查询；回帧 [1-4] 取 4 字节 i32 存入 `multi_angle` |
| 0x9C/0xA2/0xA4 | 收 | 状态回帧：温度 i8、转矩电流 i16、转速 i16、编码器 u16 |
| 0x80/0x81/0x88 | — | 失能/停止/使能：**宏已定义但全工程从未发送**（motor.h:16-18；电机上电默认使能，靠这一点工作） |

**角度单位链**（注释多处互相矛盾，以下以代码实际换算为准）：`multi_angle` 为 0.001°/LSB（`ANG_FLOAT` ×0.001f 得度，motor.h:58）；上位机协议角度为 0.01°；`motor_set_multi_angle` 入参经 `MOTOR_ABS_FROM_REL`（×10）换到 0.001° 发给电机。motor.c:161 注释写"0.01° 单位"、motor.h:49 注释自问"0.1°单位？"，均与实换算不符，重构时应统一注释。

### 2.2 控制模型：模式字段即指令队列

每电机一个 `motor_real_ctrl_t{ctrl_mode, target_angle, target_speed, max_speed}`（互斥锁保护）。`can_tx_task`（优先级 16，5ms 循环）按模式发帧：ANGLE/SPEED 模式**持续重发**（~200Hz），READ_STATE 发一次 0x92 后自动回 IDLE。

角度轮询由 `multi_angle_read_task`（优先级 17）驱动：查 Yaw → 5ms → 查 Pitch → 6ms，round 11ms（宏注释写 10ms，实际 11ms，motor.h:10）。

**关键设计特征：查询会抢占控制模式。** `motor_state_read` 直接把 `ctrl_mode` 覆写为 READ_STATE（motor.c:214），一次性发完回 IDLE——**不会恢复**之前的 ANGLE/SPEED 模式。因此：

- "持续 200Hz 重发"实际上最多维持 11ms 就被查询打断；系统能正常工作**依赖两点**：① 电机内部会锁存最后一条 0xA4/0xA2 指令持续执行；② 增稳模式下 zengwen 的 10ms 循环每周期都调 `motor_set_speed` 重新武装 SPEED 模式。
- POSITION 模式下 `hostpc_cmd_exec_task` 只在收指令时调一次 `motor_set_multi_angle`，之后全靠电机锁存——若这期间 CAN 总线出错丢帧，指令即丢失且无重发。
- `ctrl_mode` 同时充当"待发指令"和"当前状态"，三个写者（exec 任务、zengwen、read 任务）互相覆盖，行为正确性靠时序默契而非结构保证。重构方向：查询与控制分离（查询不占模式字段），或改为显式指令队列。

### 2.3 其他实现细节与问题

1. **`motor_set_multi_angle` 的 `max_speed==0` 语义**：被解释为"速度模式目标 0"（即停转，motor.c:180），上位机发限速 0 的角度指令等于急停——是特性还是巧合，协议上应写明。
2. **CAN 无错误恢复**：`AutoBusOff = DISABLE` 且无 `HAL_CAN_ErrorCallback`，总线错误积累进入 bus-off 后不会自愈，电机指令静默失效（云台维持惯性）。建议开 AutoBusOff 或加错误中断重启。
3. **发送不检查邮箱**：`HAL_CAN_AddTxMessage` 失败（3 个邮箱全满）只返回非 0，调用方全部忽略返回值。200Hz×2 电机远低于 1Mbps 容量，正常不会满，但与问题 2 叠加时丢帧无感知。
4. **ISR 内关中断做全部解析**：`HAL_CAN_RxFifo0MsgPendingCallback` 用 `rt_hw_interrupt_disable` 包住取 FIFO+解析全过程（motor.c:115）。数据量小（8 字节）尚可接受，但 memcpy 局部再单次赋值全局的写法（motor.c:139-141）本身已保证原子性，外层关中断可收窄。
5. **`motor_angle_read` 错误值歧义**：出错返回 `-RT_ERROR`（-1），与合法角度 -0.001° 无法区分（motor.c:224）。当前调用方都不判错，暂无实害。
6. **`READ_ANGLE_PERIOD_MS` 等注释失配**：见 §2.2；另 motor.h:24-28 自带"以下没有用到"标注的遗留宏（项目知识文档 §9 已提及）。

## 3. 与项目知识文档的勘误对照

- 项目知识 §3 称电机"命令字 0x80 失能/0x81 停止/0x88 使能"——这三个命令字**只有宏定义，从未使用**。
- 项目知识 §4 称火控指令"带蛋 ID 0x01~0x04"——实为**位掩码**（0x04 = 蛋 3，0x0F = 全部），详见火控文档 §3。
- 项目知识 §4 称反馈"0x92 挂架角度"——该帧为死代码，实际在用的上行帧只有 0x91 姿态和 0x05 火控状态。
