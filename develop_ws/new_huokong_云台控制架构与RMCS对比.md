# new_huokong 云台控制架构与 RMCS simple_gimbal_controller 对比

> 记录时间：2026-08-10
> 对比对象：[Alliance-Algorithm/RMCS](https://github.com/Alliance-Algorithm/RMCS) 的
> [simple_gimbal_controller.cpp](https://github.com/Alliance-Algorithm/RMCS/blob/main/rmcs_ws/src/rmcs_core/src/controller/gimbal/simple_gimbal_controller.cpp) +
> [two_axis_gimbal_solver.hpp](https://github.com/Alliance-Algorithm/RMCS/blob/main/rmcs_ws/src/rmcs_core/src/controller/gimbal/two_axis_gimbal_solver.hpp)
> 姊妹篇：[new_huokong_IMU融合滤波分析与RMCS对比.md](new_huokong_IMU融合滤波分析与RMCS对比.md)

## 1. RMCS 侧结构（三层分工）

### 1.1 SimpleGimbalController — 意图解析层（不做闭环）

读取遥控器摇杆/拨杆、鼠标、自瞄接口，每周期**无状态仲裁**四种模式（按优先级）：

1. **失能**：任一拨杆 UNKNOWN 或双拨杆 DOWN → `SetDisabled()`，输出 NaN；
2. **自瞄**：请求（右键/拨杆UP）+ 模块就绪 + `allFinite()` 数据有效三者同时满足 → `SetControlDirection(方向向量)`；
3. **回水平**：solver 未使能时 → `SetToLevel()`；
4. **手动**：摇杆×0.006 + 鼠标×0.5 增量 → `SetControlShift(yaw_shift, pitch_shift)`。

输出为 `/gimbal/yaw|pitch/control_angle_error`（**角度误差**，非力矩），下游另有电机 PID 组件闭环——串级结构的上半部分。

### 1.2 TwoAxisGimbalSolver — 解算与限位层

- 目标以 **OdomImu 系方向向量**表示，姿态源为 TF 树中的 IMU 变换（yaw 轴方向做 0.1 系数低通）；
- 误差：yaw = `atan2(y,x)`（目标在 YawLink 水平面内偏角），pitch 用 cos/sin 作差；
- **每周期对目标方向做软限位钳制**：pitch 用预存 `(cosθ, -sinθ)` 检查 z 分量、超限时把方向重构到限位面上；yaw 可选（`enable_yaw_limit`），编码器角取模后 `std::clamp` 再把方向转回允许范围；
- 限位数值来自 ROS 参数（launch 可覆盖），钳制后的方向存回供下周期增量使用。

### 1.3 下游电机 PID — 误差 → 力矩/电流

## 2. new_huokong 侧对应关系

RMCS 的一层在本项目中拆散在三个文件里：

| RMCS 层/功能 | new_huokong 对应位置 | 备注 |
|--------------|---------------------|------|
| SimpleGimbalController（模式仲裁） | `hostpc_cmd_exec_task`（hostpc.c:305） | 收 0xA4/0xA6 → 设目标 → 切 `AXIS_MODE_POSITION` |
| `SetControlDirection`（外部目标） | `CMD_YAW_CTRL`/`CMD_PITCH_CTRL` 角度指令 | 目标从"方向向量"换成"角度值" |
| `SetToLevel`（回水平） | 上电标定 Pitch 找平（zengwen.c:51） | 运行期 `CMD_MID_BACK` **被注释掉**（hostpc.c:333） |
| Solver 姿态解算 | `gimbal_fusion.c`（CalcDeltaPsi + 互补滤波） | 对应 solver 从 TF 取姿态 |
| angle_error → 下游 PID | `motor_correction_task`（zengwen.c:100） | 见 2.1 状态机 |
| Solver 软限位钳制 | **无真正对应物** | 见 3.1 |
| `SetDisabled`（失能保护） | **无对应物** | 见 3.2 |

### 2.1 执行层状态机（zengwen.c motor_correction_task，10ms 周期）

每轴独立跑三段流程：

```c
// zengwen.c:119  POSITION：粗定位交给电机内部位置环，只做到位判定
if (axis[PITCH_NO].ctrl_mode == AXIS_MODE_POSITION) {
    int32_t diff = motors[PITCH_NO].multi_angle/10 - pitch_target_angle_abs;
    if (abs(diff) < PITCH_ARRIVED_THRESHOLD) {        // 到位（0.1°单位阈值）
        pitch_setpoint_now = imu_jy62.roll;           // 以当前姿态为软启动起点
        /* 清空PID历史误差 */
        pitch_fade_cnt = 20;                          // 启动20周期淡入
        axis[PITCH_NO].ctrl_mode = AXIS_MODE_STABILIZE;
    }
}
// zengwen.c:146  STABILIZE：setpoint 20周期线性淡入到最终目标，再增量PID出速度
float ratio = (float)(20 - pitch_fade_cnt) / 20.0f;
pitch_setpoint_now = imu_jy62.roll * (1.0f - ratio) + pitch_final_target * ratio;
float speed = IncPID_Calc(&mpu_pid_pitch, mpu_pid_pitch.set_point, imu_jy62.roll);
motor_set_speed(MOTOR_PITCH_NO, (int32_t)(speed * 100));  // 速度环闭在LK电机内部
```

串级划分：**MCU 姿态/位置环 → LK 电机内部速度/电流环**（RMCS 是 error → 上位机 PID → DJI 电机电流）。

**两轴反馈源不同**（这是关键差异，另见 IMU 文档 (f) 节）：

```c
// Pitch 反馈 = JY62 原始欧拉角（zengwen.c:158）
float speed = IncPID_Calc(&mpu_pid_pitch, mpu_pid_pitch.set_point, imu_jy62.roll);
// Yaw 反馈 = 融合输出（zengwen.c:232，旧写法 imu_jy62.yaw - imu_zero_raw 注释保留在231行）
float feedback = att->yaw_enu;
float speed = -IncPID_Calc(&mpu_pid_yaw, mpu_pid_yaw.set_point, feedback);
```

→ **gimbal_fusion 的输出质量（含 dt 失配 bug）直接影响 Yaw 控制环动态品质。**

### 2.2 过流保护（当前唯一的"撞限位"防线）

```c
// zengwen.c:165  过流时速度置零，但锁存方向：允许反向指令退出卡死
if (abs(motors[1].current) > 88) {
    speed = 0;
    dir_speed_set = last_dir_speed_set0;      // 沿用过流前方向
} else {
    dir_speed_set = dir_speed_temp;
    last_dir_speed_set0 = dir_speed_set;
}
if (dir_speed_set != dir_speed_temp && dir_speed_temp != 0)
    speed = speed_temp;                        // 新方向与锁定方向相反 → 放行（退出）
```

### 2.3 结构差异小结

- **仲裁方式**：RMCS 每周期无状态仲裁（拨杆状态直接映射模式）；本项目是**事件驱动状态机**（收指令切 POSITION，到位自动回 STABILIZE）。
- **淡入软切换**：本项目 20 周期 setpoint 线性混合是 RMCS 没有的细节，避免模式切换瞬间的阶跃冲击，处理得好。
- **目标表示**：RMCS 用方向向量（天然适配自瞄）；本项目用角度值（适配上位机角度指令）。

## 3. 缺失项与改进建议

### 3.1 运行期软限位（优先级最高）

标定辛苦找出的 `enc_left_limit_raw`/`enc_right_limit_raw`（zengwen.c:78/88，堵转电流探测）目前是**死数据**——运行期从不校验目标角度，越界指令全靠过流保护硬扛。照 solver 的 clamp 思路，在 `hostpc_cmd_exec_task` 收到角度指令处加一行即可：

```c
target_abs = CLAMP(target_abs, axis[YAW_NO].enc_right_limit_raw + MARGIN,
                               axis[YAW_NO].enc_left_limit_raw  - MARGIN);
```

### 3.2 失能/断链保护

RMCS 拨杆异常即 `SetDisabled()` 输出 NaN；本项目上位机断链后云台**维持最后目标，无任何失控保护**。建议给 hostpc 加接收超时（如 500ms 无有效帧 → 速度置零或锁当前位），对挂飞行平台的设备尤其必要。

### 3.3 恢复一键回中

`CMD_MID_BACK`（0x00）协议已定义、处理分支已写好但被注释（hostpc.c:333-337），等价于 RMCS 的 `SetToLevel`，建议恢复并让其复用标定零点。

### 3.4 自瞄式数据校验

RMCS 对外部目标做 `ready() && allFinite()` 双重校验才采用；本项目对上位机角度值无范围/有效性检查，可与 3.1 的 clamp 一并补上。
