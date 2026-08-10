# new_huokong IMU 融合滤波分析与 RMCS 对比

> 记录时间：2026-08-10
> 对比对象：[Alliance-Algorithm/RMCS](https://github.com/Alliance-Algorithm/RMCS)（ROS2 上位机架构，BMI088）

## 1. 本项目的融合滤波策略

采用**"模块内置解算 + MCU 端一维航向互补滤波"两层结构**，MCU 不做原始六轴融合。

### 第一层：JY62 模块内部（黑盒）

六轴姿态解算全部在 JY62（维特智能，内部 MPU6050 + 自带 MCU 跑"动态卡尔曼滤波"）内完成。
`imu.c` 经 USART2 @115200 收三帧连发的 33 字节数据：

| 帧头+类型 | 内容 | 换算 |
|-----------|------|------|
| 0x55 0x51 | 加速度 + 温度 | raw/32768×16g |
| 0x55 0x52 | 角速度 | raw/32768×2000°/s |
| 0x55 0x53 | 欧拉角 roll/pitch/yaw | raw/32768×180° |

roll/pitch/yaw 是模块算好的成品，MCU 直接使用。实际输出率约 100Hz。

### 第二层：MCU 端 Yaw 一维融合（gimbal_fusion.c）

每周期三步：

1. **几何解算 δψ**（`CalcDeltaPsi`）：机头单位向量 [1,0,0] 经编码器角（扣零点）逆旋转到载荷系，再用 IMU roll/pitch 拉平到水平面，`δψ = -atan2(vLy, vLx)`——载荷相对机头的水平偏航角。
2. **绝对航向基准**：`yaw_abs_ref = 飞机正东航向(hostpc 0x06 下发) + δψ`。
3. **一维 PI 互补滤波**（`Yaw_ComplementaryFilter`）：
   `psi_dot = gyro_z + Kp·err + Ki·∫err`，积分得 `psi_fused`。
   参数：Kp=1.2，Ki=0.01，积分限幅 ±100，DT=0.01s（硬编码）。
   数学上这是 **Mahony 滤波器退化到一维标量的形式**（低频信外部基准、高频信陀螺）。

Pitch/Roll **无融合**，直接透传 JY62 欧拉角；增稳 PID 反馈也直接用 `imu_jy62.roll`（zengwen.c:158）。融合结果经双缓冲原子切换发布（`Gimbal_GetAtt()`）。

### 实现代码精讲

#### (a) 数据入口：imu.c 的 DMA + 空闲中断 + 信号量链路

JY62 每次连发 3 帧共 33 字节，代码用"DMA 收满 + 串口空闲中断切帧"的经典组合，中断里只做拷贝和发信号量，解析放在线程里：

```c
// imu.c:216  USART2 空闲中断：一包收完（总线空闲）才触发
void USER_IMU_UART_IRQHandler(UART_HandleTypeDef *huart)
{
    if (RESET != __HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart2);
        HAL_UART_DMAStop(&huart2);
        uint8_t data_length = IMU_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
        if (data_length >= IMU_RX_BUF_SIZE) {          // 只接受完整33字节包
            memcpy(imu_temp_buffer, imu_rx_buffer, IMU_RX_BUF_SIZE);
            rt_sem_release(&sem_imu_data_ready);       // 唤醒解析线程
        }
        HAL_UART_Receive_DMA(&huart2, imu_rx_buffer, IMU_RX_BUF_SIZE); // 重启DMA
    }
}
```

解析函数按协议逐帧校验（帧头 0x55 + 类型字节 + 累加和校验），换算公式与量程绑定：

```c
// imu.c:118  以角速度帧(0x52)为例：±2000°/s 量程
if (buffer[12] == 0x52) {
    check_sum = 0;
    for (uint8_t i = 11; i < 21; i++) check_sum += buffer[i]; // 累加和校验
    if (check_sum == buffer[21]) {
        raw_data = (int16_t)(buffer[14] << 8 | buffer[13]);   // 小端拼int16
        imu_jy62.omega_x = raw_data / 32768.0f * 2000.0f;     // 归一化到°/s
        // omega_y / omega_z 同理
    }
}
```

注意一个细节：三帧校验是**各自独立**的——某一帧校验失败只是该帧数据不更新（沿用旧值），但函数末尾 `imu_jy62.data_valid = 1` 仍会置位。也就是说 `data_valid` 只代表"收到过包"，不代表"本包三帧全部有效"。

#### (b) 几何解算：CalcDeltaPsi 的旋转链

目标：算出**载荷（挂架炮口）相对飞机机头的水平偏航角 δψ**。做法是把机头单位向量 [1,0,0] 经两段旋转搬到"载荷水平系"，再取其方位角：

```c
// gimbal_fusion.c:62
static float CalcDeltaPsi(const GimbalAtt_t *att) {
    float vx, vy, vz, vLx, vLy, vLz;

    // 步骤A：机头向量从机头系(GB)转到载荷机体系(PB)——用编码器角逆变换
    //        R = Rx(-α_roll)·Ry(-α_pitch)·Rz(-α_yaw)
    RotateZ(-att->alpha_yaw,   1.0f, 0.0f, 0.0f, &vx, &vy, &vz);
    RotateY(-att->alpha_pitch, vx, vy, vz, &vx, &vy, &vz);
    RotateX(-att->alpha_roll,  vx, vy, vz, &vx, &vy, &vz);

    // 步骤B：用IMU的roll/pitch把PB系向量"拉平"到载荷水平系(PL)
    //        R = Ry(β_pitch)·Rx(β_roll)
    RotateX(att->beta_roll,  vx, vy, vz, &vLx, &vLy, &vLz);
    RotateY(att->beta_pitch, vLx, vLy, vLz, &vLx, &vLy, &vLz);

    // 步骤C：水平面内取方位角
    float delta = -atan2f(vLy, vLx) * 180.0f / M_PI;
    return AngleWrap180(delta);
}
```

两段旋转各司其职：**步骤 A 用编码器角**（电机轴角度，即载荷相对机头转了多少），**步骤 B 用 IMU 姿态角**（载荷相对水平面歪了多少）。串起来后 δψ 就是纯水平面内"炮口偏离机头"的角度——它与飞机自身姿态无关，所以可以直接和飞机的绝对航向相加。

其中 `RotateX/Y/Z` 是标准的单轴旋转矩阵展开（gimbal_fusion.c:31-56），`AngleWrap180` 用 while 循环把角度归一到 (-180°, 180°]：

```c
// gimbal_fusion.c:22
float AngleWrap180(float ang) {
    while (ang > 180.0f)  ang -= 360.0f;
    while (ang <= -180.0f) ang += 360.0f;
    return ang;
}
```

#### (c) 核心滤波：Yaw_ComplementaryFilter（一维 Mahony）

```c
// gimbal_fusion.c:85
static void Yaw_ComplementaryFilter(YawFilter_t *filt, float psi_abs, float gyro_z, float dt)
{
    float err = AngleWrap180(psi_abs - filt->psi_fused);  // 基准与估计的差，绕回±180°

    filt->err_integral += err * dt;                        // 积分项（吸收陀螺零偏）
    if (filt->err_integral >  INTEGRAL_LIMIT) filt->err_integral =  INTEGRAL_LIMIT;
    if (filt->err_integral < -INTEGRAL_LIMIT) filt->err_integral = -INTEGRAL_LIMIT;

    float comp    = Kp_YAW * err + Ki_YAW * filt->err_integral; // PI修正量
    float psi_dot = gyro_z + comp;                         // 修正后的角速度
    filt->psi_fused = AngleWrap180(filt->psi_fused + psi_dot * dt); // 积分出航向
}
```

逐行对应滤波原理：

- `err`：绝对基准（飞机航向+δψ）与当前估计的偏差。**这就是 Mahony 里"加计重力叉积误差"的一维版**——只是观测源换成了外部航向。
- `err_integral`（Ki=0.01，限幅 ±100）：慢速吸收陀螺 z 轴零偏。限幅 100 × Ki 0.01 = 最多提供 1°/s 的常值修正，正好覆盖消费级 MEMS 陀螺的典型零偏量级。
- `comp`（Kp=1.2）：比例项决定"拉向基准"的带宽，约 1.2 rad/s 量级的收敛速率——高于此频率的运动信陀螺，低于此频率信基准，这就是互补分频点。
- `psi_dot = gyro_z + comp`：修正后的角速度积分——与 RMCS `bmi088.hpp` 中 `gx += double_kp_ * halfex; ...` 后做四元数积分的结构完全同构。

#### (d) 主更新流程：Gimbal_Update（每周期一次）

```c
// gimbal_fusion.c:105（节选）
void Gimbal_Update(float drone_yaw_enu) {
    uint8_t write_idx = (g_gimbal_db.active_idx + 1) & 1;   // 双缓冲：写非活跃区
    GimbalAtt_t *att = &g_gimbal_db.att[write_idx];

    // 1. 编码器角扣零点（enc_zero_raw单位0.01°，来自上电限位标定），符号取反适配安装方向
    att->alpha_yaw   = -(ANG_FLOAT(MOTOR_YAW_NO)   - (float)(axis[YAW_NO].enc_zero_raw + 50) / 100.0f);
    att->alpha_pitch = -(ANG_FLOAT(MOTOR_PITCH_NO) - (float)(axis[PITCH_NO].enc_zero_raw)    / 100.0f);
    att->alpha_roll  = 0.0f;                                 // 无Roll电机

    // 2. IMU姿态：注意轴映射——IMU的roll对应云台pitch（安装方向决定）
    att->beta_pitch   = imu_jy62.roll;
    att->beta_roll    = imu_jy62.pitch;
    att->gyro_z_world = imu_jy62.omega_z;    // ⚠️机体系z角速度，命名的"world"并未实现

    // 3~5. 几何解算 → 绝对基准 → 互补滤波
    att->psi_delta = CalcDeltaPsi(att);
    float yaw_abs_ref = AngleWrap180(drone_yaw_enu + att->psi_delta);
    Yaw_ComplementaryFilter(&att->yaw_filter, yaw_abs_ref, att->gyro_z_world, DT);

    // 6. 输出：yaw是融合值，pitch/roll直接透传IMU
    att->yaw_enu   = att->yaw_filter.psi_fused;
    att->pitch_enu = att->beta_pitch;
    att->roll_enu  = att->beta_roll;

    // 7. 原子切换缓冲区，读端(Gimbal_GetAtt)永远拿到完整一致的快照
    rt_enter_critical();
    g_gimbal_db.active_idx = write_idx;
    rt_exit_critical();
}
```

三个值得注意的实现点：

- **零点补偿**：`enc_zero_raw` 是 `zengwen.c` 上电打机械限位标定出来的编码器零点（0.01° 单位），Yaw 轴额外 `+50`（即 +0.5°）是手工修正的安装偏差。
- **轴映射交叉**：`beta_pitch = imu_jy62.roll`——IMU 物理安装方向决定其 roll 轴对应云台 pitch 轴，代码里用 `#if 1/#else` 保留了两种安装方向的分支。
- **双缓冲**：写完整个结构体后才在临界区里翻转 `active_idx`，PID/上报任务通过 `Gimbal_GetAtt()` 读到的永远是一致快照，避免"半更新"数据。

#### (e) 初始化与任务：Gimbal_Init / gimbal_fusion_task

```c
// gimbal_fusion.c:170（节选）：初始航向直接用当前基准对齐，不从0收敛
void Gimbal_Init(void) {
    ...
    float delta = CalcDeltaPsi(&temp_att);
    float initial_psi_abs = AngleWrap180(hostpc_cmd.cmd_yaw + delta);
    g_gimbal_db.att[0].yaw_filter.psi_fused = initial_psi_abs;  // 两个缓冲区同值
    g_gimbal_db.att[1].yaw_filter.psi_fused = initial_psi_abs;
    ...
}
```

这一步等价于 RMCS EKF 的 `reset_from_accel`——**用首次观测直接构造初始状态**，跳过滤波器冷启动的缓慢收敛过程，是正确的做法。

```c
// gimbal_fusion.c:208：周期任务——dt失配bug所在
static void gimbal_fusion_task(void *parameter) {
    while (1) {
        Gimbal_Update(hostpc_cmd.cmd_yaw);
        rt_thread_mdelay(2);   // 注释写"周期10ms"，实际tick=1000Hz → 真2ms
    }                          // 而DT硬编码0.01s → 积分速率×5（见下方问题1）
}
```

#### (f) 下游消费：两轴增稳反馈源不同

```c
// zengwen.c:158  Pitch增稳PID反馈 = JY62原始欧拉角（不经融合）
float speed = IncPID_Calc(&mpu_pid_pitch, mpu_pid_pitch.set_point, imu_jy62.roll);

// zengwen.c:232  Yaw增稳PID反馈 = 融合输出（经Gimbal_GetAtt取快照）
float feedback = att->yaw_enu;
float speed = -IncPID_Calc(&mpu_pid_yaw, mpu_pid_yaw.set_point, feedback);
```

**Pitch 闭环走短通路**（JY62 欧拉角 → PID → 电机速度指令），不依赖融合；**Yaw 闭环则直接以融合航向 `att->yaw_enu` 为反馈**（旧写法 `imu_jy62.yaw - imu_zero_raw` 被注释保留在 zengwen.c:231）。因此 gimbal_fusion 的输出质量直接决定 Yaw 增稳精度——前述 dt 失配等问题会实打实影响 Yaw 控制环。

### ⚠️ 发现的问题

1. **dt 失配 bug**：`gimbal_fusion_task` 用 `rt_thread_mdelay(2)`（注释却写 10ms），系统 tick=1000Hz，任务实际 ~2ms 一跑，而 DT 硬编码 0.01s → **陀螺积分与 PI 修正等效速率放大约 5 倍**。稳态被 PI 拉回不易察觉，但动态响应不是设计值。（gimbal_fusion.c:217）
2. **无量测有效性检查**：`Gimbal_Update` 无条件信任 `hostpc_cmd.cmd_yaw` 与 IMU 数据，不检查 `data_valid`、不检查航向超时。
3. **`gyro_z_world` 名不副实**：直接用机体系 `imu_jy62.omega_z` 当大地系 Z 角速度（gimbal_fusion.c:133），大俯仰/横滚时有误差，应先用姿态把角速度矢量旋到世界系。
4. **时间戳对齐字段闲置**：`cmd_yaw_time`/`cmd_delta_time`/`cmd_time_flag` 已定义但融合中未用于陀螺外推补偿链路延迟。

## 2. RMCS 的 BMI088 滤波策略（两套并存）

代码位置：`rmcs_ws/src/rmcs_core/src/hardware/device/bmi088.hpp`、`bmi088_ekf.hpp`、`rmcs_ws/src/rmcs_core/src/filter/imu_ekf.hpp`。

### 2.1 bmi088.hpp — Mahony AHRS（Madgwick 实现版）

- 六轴四元数解算，~1kHz；加计归一化后与四元数估计的重力方向做**叉积**得姿态误差，PI 反馈修正陀螺角速度后一阶四元数积分。
- 零偏无离线校准，靠 Ki 积分项在线吸收；`std::atomic` 无锁跨线程发布。

### 2.2 bmi088_ekf.hpp + imu_ekf.hpp — 四元数全状态 EKF

- 状态量 = 4 维四元数（**不含零偏**，非 error-state）；范数约束靠归一化 + 协方差投影到单位球切空间。
- 过程模型：陀螺驱动四元数运动学（dt 按时间戳实测）；量测模型：加计观测重力方向。
- 噪声参数：过程 1.22e-3·I₃，量测 R=50·I₃（压低加计权重），门限噪声 0.02·I₃，初始协方差 0.15·I₄。
- 工程亮点：
  - **χ² ≥ 3.0 剔除机动加速度污染的量测**（门限用独立 gate 协方差算）；
  - **显式屏蔽四元数 yaw 分量更新**（重力对 yaw 不可观）；
  - Joseph 形式协方差更新；
  - **陀螺饱和（>98% 量程）→ 协方差膨胀回初值**，让加计快速重新收敛；
  - 陈旧帧（时间戳跳变 >1ms）丢弃；全程 allFinite/半正定检查。

## 3. 异同对比

| 维度 | new_huokong | RMCS |
|------|-------------|------|
| 融合层级 | 六轴融合在 JY62 内（黑盒），MCU 只融合 Yaw 一维 | 从原始六轴自解算完整四元数 |
| 算法 | 一维标量 PI 互补滤波 | Mahony（四元数 PI 互补）/ 四元数全状态 EKF |
| 姿态表示 | 欧拉角标量 | 四元数（无万向节锁） |
| Yaw 绝对基准 | 飞机航向 + 编码器几何链（**有绝对 yaw**） | 加计只定 roll/pitch，yaw 相对（陀螺积分） |
| 更新率 | ~100Hz（JY62 串口限制）+ dt 失配 | ~1kHz，dt 实测 |
| 量测有效性 | 无 | χ² 门限、饱和处理、陈旧帧丢弃、数值防御 |
| 零偏 | 依赖 JY62 内部 + Ki 缓慢吸收 | Mahony 版 Ki 在线补偿；EKF 版无零偏状态（同为短板） |

**同源**：本项目的 `Yaw_ComplementaryFilter` 与 Mahony 是同一互补思想（PI 反馈 + 陀螺积分），只是观测源不同：RMCS 用重力修 roll/pitch，本项目用"飞机航向+几何链"修 yaw——两者解决的是互补的问题，本项目反而具备 RMCS 没有的绝对航向能力。

## 4. 改进空间（按优先级)

1. **修 dt 失配**：`mdelay(2)`→10ms，或用 `rt_tick_get()` 实测帧间 dt 传入滤波器。
2. **量测门限（χ² 思想低配版）**：飞机航向超时/`data_valid==0` 时 err 置零，退化为纯陀螺积分。
3. **补世界系角速度旋转**：用 roll/pitch 把机体角速度旋到世界系再取 z 分量。
4. **启用时间戳外推**：用 `gyro_z × 链路延迟` 外推飞机航向基准，降低动态误差。
5. **长期**：换 BMI088/ICM-42688 直连 SPI，移植 RMCS 的 Mahony（纯 C 化容易），1kHz 采样，增稳带宽/延迟质变。

## 5. JY62 黑盒可查性结论

- **滤波算法本身不可查**：动态卡尔曼滤波跑在模块自带 MCU 的闭源固件里，维特智能不公开源码，滤波参数（带宽/增益）在 JY62 这个基础型号上也不可调（JY901 等高端型号才有更多寄存器）。
- **可查/可控的部分**：
  - 串口协议完全公开（0x55 帧，官网可下规格书+通讯协议 PDF：[wit-motion.cn JY62 产品页](https://wit-motion.cn/proztmz/53.html)）；
  - 有限配置指令：Z 轴航向归零（0xFF 0xAA 0x52）、加计校准、波特率、休眠/唤醒、输出速率（10~100Hz）等，可用官方上位机或串口指令下发；
  - 本项目代码里 `imu_send_cmd_dma()`（imu.c:62）已具备下发能力但**从未被调用**——模块目前跑出厂默认配置。
- **绕过黑盒的出路**：JY62 同时输出原始加速度（0x51）和角速度（0x52）帧，本项目已解析并存入 `imu_jy62`。可以**不换硬件**，直接用原始六轴在 MCU 端自跑 Mahony/EKF，把黑盒欧拉角仅作对照——这是验证和替换黑盒的最低成本路径（但仍受 100Hz 串口输出率限制）。
