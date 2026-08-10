/**
 * @file hostpc_comm.c
 * @brief UART4上位机通信驱动（RT-Thread + STM32 HAL DMA空闲中断收发）
 * @uart 硬件配置：PA0-TX PA1-RX 115200 8N1 DMA1
 * @thread 5个线程：接收解析/指令执行/串口发送/角度周期上报/火控逻辑任务
 * @protocol 自定义帧协议：0xA5帧头 + 类型 + 指令 + 数据域 + 8位累加校验和
 */
#include "hostpc.h"
#include "motor.h"
#include "zengwen.h"
#include "gimbal_fusion.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ======================================================================
 * 全局硬件句柄、同步信号量、收发缓存、任务栈、全局状态变量
 * ====================================================================== */
// UART、DMA硬件句柄
UART_HandleTypeDef huart4;
DMA_HandleTypeDef hdma_uart4_rx;
DMA_HandleTypeDef hdma_uart4_tx;
// 线程同步信号量
static struct rt_semaphore sem_hostpc_rx_ready;  // 接收完成信号量
static struct rt_semaphore sem_hostpc_tx_idle;   // DMA发送空闲信号量
// 数据缓冲区
hostpc_cmd_t hostpc_cmd = {0};						// 上位机下发指令全局存储
uint8_t hostpc_rx_buf[HOSTPC_MAX_FRAME_SIZE] = {0};     // DMA原始接收缓冲区
uint8_t hostpc_tx_buf[HOSTPC_TX_BUF_LEN] = {0};		// DMA发送缓冲区
uint8_t hostpc_rx_temp_buf[HOSTPC_MAX_FRAME_SIZE] = {0};// 解析专用拷贝缓存

static uint16_t tx_hostpc_write_idx = 0;            // 环形缓冲区写指针
static uint16_t tx_hostpc_read_idx = 0;             // 环形缓冲区读指针

volatile uint8_t g_hostpc_rx_len = 0;				// 实际接收长度

// 通信线程栈与TCB
static uint8_t hostpc_rx_task_stack[512];
static uint8_t hostpc_exec_task_stack[512];
static uint8_t hostpc_tx_task_stack[512];
static uint8_t hk_angle_report_task_stack[512];		// 角度周期上报线程栈

static struct rt_thread hostpc_rx_task_tcb;
static struct rt_thread hostpc_exec_task_tcb;
static struct rt_thread hostpc_tx_task_tcb;
static struct rt_thread hk_angle_report_task_tcb;	// 角度周期上报线程
// 系统滴答时钟转秒(s)
static float get_tick_s(void)
{						
	rt_tick_t tick = rt_tick_get();
    return (float)tick / (float)RT_TICK_PER_SECOND;
}

/* ======================================================================
 * 上报全套火控状态：总解锁、有无弹药、四弹充电层级
 * 参数：fire_feedback 火控状态结构体指针
 * ====================================================================== */
rt_err_t hostpc_send_fire_state(fire_state_feedback_t *fire_feedback)
{
    uint8_t tx_frame[HOSTPC_FRAME_SIZE] = {0};
    uint8_t data_buf[6] = {0}; // 数据域：1(解锁)+1(有无弹)+4(充电)=6字节（协议精简）

    // 填充数据域（严格按新协议）：
    data_buf[0] = fire_feedback->fire_unlock_state; // 字节0：总解锁状态
    data_buf[1] = fire_feedback->egg_has_byte;      // 字节1：有无弹（4位）
    data_buf[2] = fire_feedback->egg_charge[1];     // 字节2：蛋1充电层级
    data_buf[3] = fire_feedback->egg_charge[2];     // 字节3：蛋2充电层级
    data_buf[4] = fire_feedback->egg_charge[3];     // 字节4：蛋3充电层级
    data_buf[5] = fire_feedback->egg_charge[4];     // 字节5：蛋4充电层级

    // 组包+发送（帧类型=反馈，指令码=查询火控状态）
    hostpc_pack_tx_frame(tx_frame, FRAME_TYPE_FEEDBACK, CMD_QUERY_FIRE_STATE, data_buf, 6);
    return hostpc_send_frame(tx_frame);
}
/* ======================================================================
 * 上报Yaw、Pitch双轴实时多圈角度
 * 参数：pitch_angle俯仰轴角度 yaw_angle水平轴角度 int32单位0.01°
 * ====================================================================== */
rt_err_t hostpc_send_angle_feedback(int32_t pitch_angle, int32_t yaw_angle)
{
    uint8_t tx_frame[HOSTPC_FRAME_SIZE] = {0};
    uint8_t data_buf[8] = {0};

    // 小端序填充Pitch(3-6) + Yaw(7-10)
    memcpy(&data_buf[0], &pitch_angle, 4);
    memcpy(&data_buf[4], &yaw_angle, 4);

    // 组包+入队
    hostpc_pack_tx_frame(tx_frame, FRAME_TYPE_FEEDBACK, CMD_FEEDBACK_ANGLE, data_buf, 8);
    return hostpc_send_frame(tx_frame);
}
/* ======================================================================
* 上报带时间戳的飞机姿态（21字节帧）
* ====================================================================== */
rt_err_t hostpc_send_posture_feedback(float pitch, float roll, float yaw, float time, uint8_t fire)
{
    uint8_t tx_frame[HOSTPC_POSTURE_FRAME_SIZE] = {0};
    uint8_t data_buf[17] = {0};  // 4*4 + 1 = 17字节
    
    memcpy(&data_buf[0],  &pitch, 4);
    memcpy(&data_buf[4],  &roll,  4);
    memcpy(&data_buf[8],  &yaw,   4);
    memcpy(&data_buf[12], &time,  4);
    memcpy(&data_buf[16], &fire,  1);  // 注意偏移是16，不是13！
    
    hostpc_pack_tx_frame(tx_frame, FRAME_TYPE_FEEDBACK, CMD_FEEDBACK_POSTURE, data_buf, 17);
    return hostpc_send_frame(tx_frame);
}
/* ======================================================================
 * 协议底层工具函数：校验和计算
 * ====================================================================== */
uint8_t hostpc_calc_checksum(uint8_t *buf, uint8_t len)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++)
        sum += buf[i];
    return sum;
}
/* ======================================================================
 * 协议底层工具函数：通用协议组包函数
 * 参数：tx_buf 输出帧缓存 frame_type 帧类型 cmd_code子指令码 data数据域 data_len数据长度
 * 规则：帧头0xA5，数据域最大9字节，前11字节计算校验放入第11位
 * ====================================================================== */
rt_err_t hostpc_pack_tx_frame(uint8_t *tx_buf, uint8_t frame_type, uint8_t cmd_code, uint8_t *data, uint8_t data_len)
{
    memset(tx_buf, 0x00, data_len + 4);
    tx_buf[0] = FRAME_HEAD;
    tx_buf[1] = frame_type;
    tx_buf[2] = cmd_code;
    if (data != RT_NULL && data_len > 0)
    {
        memcpy(&tx_buf[3], data, data_len);
    }
    // 校验和覆盖整个帧（不包括校验位本身）
    tx_buf[data_len + 3] = hostpc_calc_checksum(tx_buf, data_len + 3);
    return RT_EOK;
}
/* ======================================================================
 * 协议底层工具函数：将完整待发送帧写入环形发送缓冲区（支持变长帧）
 * ====================================================================== */
rt_err_t hostpc_send_frame(uint8_t *frame_buf)
{
    // 根据帧头后的帧类型和指令码动态计算实际帧长度
    uint8_t frame_len = HOSTPC_FRAME_SIZE; // 默认12字节
    
    // 检查是否为反馈帧
    if (frame_buf[1] == FRAME_TYPE_FEEDBACK)
    {
        switch (frame_buf[2])
        {
            case CMD_FEEDBACK_ANGLE:
                frame_len = HOSTPC_FEEDBACK_FRAME_SIZE;  // 16字节
                break;
            case CMD_FEEDBACK_POSTURE:
                frame_len = HOSTPC_POSTURE_FRAME_SIZE;   // 21字节
                break;
            case CMD_HEADING_ANGLE:
                frame_len = HOSTPC_FEEDBACK_FRAME_SIZE;  // 16字节
                break;
            default:
                frame_len = HOSTPC_FRAME_SIZE;           // 默认12字节
                break;
        }
    }
    
    rt_base_t level = rt_hw_interrupt_disable();
    // 检查缓冲区是否有足够空间
    if ((tx_hostpc_write_idx + frame_len) < HOSTPC_TX_BUF_LEN)
    {
        memcpy(&hostpc_tx_buf[tx_hostpc_write_idx], frame_buf, frame_len);
        tx_hostpc_write_idx += frame_len;
        rt_hw_interrupt_enable(level);
        return RT_EOK;
    }
    rt_hw_interrupt_enable(level);
    return -RT_EFULL;
}
/* ======================================================================
 * RT-Thread任务函数 1：上位机帧接收解析任务
 * 阻塞等待接收信号量，校验帧头、校验和，解析指令存入全局 hostpc_cmd
* 这里将接收到来自上位机的数据帧解析保存到储存上位机要求数据的结构体 hostpc_cmd
 * ====================================================================== */
/**
 * @brief 上位机接收解析任务（阻塞等待接收信号量）
 * @param parameter 线程入口参数（未使用）
 * @note  根据帧长度和帧类型分别处理：
 *        - 12字节控制帧（FRAME_TYPE_CTRL）：解析指令存入 hostpc_cmd 供执行任务使用
 *        - 16字节反馈帧（FRAME_TYPE_FEEDBACK + CMD_HEADING_ANGLE）：解析航向角和时间戳存入全局变量
 */
static void hostpc_rx_parse_task(void *parameter)
{
    uint8_t *pbuf = hostpc_rx_temp_buf;
    uint8_t len;
    uint8_t checksum;

    while (1)
    {
        // 阻塞等待空闲中断释放信号量
        if (rt_sem_take(&sem_hostpc_rx_ready, RT_WAITING_FOREVER) == RT_EOK)
        {
            // 进入临界区，保护共享数据
            rt_enter_critical();

            len = g_hostpc_rx_len;  // 实际接收到的字节数（12 或 16）

            // 1. 基本校验：帧头必须为 0xA5，且长度合法
            if (pbuf[0] != FRAME_HEAD || (len != HOSTPC_FRAME_SIZE && len != HOSTPC_FEEDBACK_FRAME_SIZE))
            {
                // 非法帧，清空控制指令（如果有）
                memset(&hostpc_cmd, 0, sizeof(hostpc_cmd));
                hostpc_cmd.cmd_valid = RT_FALSE;
                rt_exit_critical();
                continue;
            }

            // 2. 校验和校验（前 len-1 字节累加和）
            checksum = hostpc_calc_checksum(pbuf, len - 1);
            if (checksum != pbuf[len - 1])
            {
                // 校验失败，清空控制指令
                memset(&hostpc_cmd, 0, sizeof(hostpc_cmd));
                hostpc_cmd.cmd_valid = RT_FALSE;
                rt_exit_critical();
                continue;
            }

            // 3. 根据帧类型（偏移1）区分处理
            if (pbuf[1] == FRAME_TYPE_CTRL)   // === 控制帧（上位机下发） ===
            {
                // 清空旧指令，填充新指令
                memset(&hostpc_cmd, 0, sizeof(hostpc_cmd));
                hostpc_cmd.cmd_code = pbuf[2];
                hostpc_cmd.fire_id  = pbuf[3];
                hostpc_cmd.cmd_valid = RT_TRUE;

                // 根据指令码解析额外数据域（偏移3~10）
                switch (hostpc_cmd.cmd_code)
                {
                    case CMD_YAW_CTRL:
                        hostpc_cmd.speed_val = pbuf[4] | (pbuf[5] << 8);
                        hostpc_cmd.yaw_angle_val = *(int32_t *)&pbuf[6];
                        break;
                    case CMD_PITCH_CTRL:
                        hostpc_cmd.speed_val = pbuf[4] | (pbuf[5] << 8);
                        hostpc_cmd.pitch_angle_val = *(int32_t *)&pbuf[6];
                        break;
                    case CMD_FIRE_2_DEC:        // 火控二级充电等只需 fire_id
                        // fire_id 已在上面赋值，无需额外操作
                        break;
                    // 其他控制指令（如 CMD_QUERY_FIRE_STATE 等）暂无需额外数据域
                    default:
                        break;
                }
                // 注意：火控指令（0x01~0x05,0x08）的数据域只用到 fire_id，已处理
            }
            else if (pbuf[1] == FRAME_TYPE_FEEDBACK)   // === 反馈帧（飞机上报） ===
            {
                // 根据指令码区分具体反馈类型
                if (pbuf[2] == CMD_HEADING_ANGLE && len == HOSTPC_FEEDBACK_FRAME_SIZE)
                {
                    // 数据域：偏移3~14，共12字节（三个float）
                    float angle, ts1, ts2;// 飞机获取yaw角度、飞机获取角度时间、飞机发送获取角度时间
					
					if(hostpc_cmd.cmd_time_flag == 0)
					{
//						// 1.计算上位机从获取到发送的时间
//						float hostpc_time_delta	= ts2 - ts1; 
//						// 2.获得当前时间并减去上位机从获取到发送的时间,得到上位机获得角度时,下位机对应的时间
//						float salvepc_time_get = get_tick_s() - hostpc_time_delta;
						hostpc_cmd.cmd_delta_time = ts1-get_tick_s();
						hostpc_cmd.cmd_time_flag = 1;
					}						
					memcpy(&angle, &pbuf[3], 4);
                    memcpy(&ts1,   &pbuf[7], 4);
					memcpy(&ts2,   &pbuf[11], 4);
                    // 原子更新全局变量（关中断保护，因为可能被其他任务读取）
                    rt_base_t level = rt_hw_interrupt_disable();
                    hostpc_cmd.cmd_yaw		= angle;
                    hostpc_cmd.cmd_yaw_time	= ts1;
                    hostpc_cmd.cmd_rec_time	= ts2;
                    rt_hw_interrupt_enable(level);
					
                    // 可选的调试打印（需谨慎，避免影响实时性）
                    // rt_kprintf("Heading: %.2f, ts1: %.3f, ts2: %.3f\n", angle, ts1, ts2);
                }
                // 其他反馈帧（如 0x92 角度反馈）由发送任务负责，接收时忽略
                // 如果需要处理，可在此添加分支
            }
            else
            {
                // 未知帧类型，丢弃
                // 可以打印错误信息，但为了性能通常忽略
            }

            // 退出临界区
            rt_exit_critical();
        }
    }
}
/* ======================================================================
 * RT-Thread任务函数 2：上位机下发指令执行任务
 * 5ms轮询全局指令缓存，执行云台回中、电机角度控制、电机状态读取
 * ====================================================================== */
extern int32_t motor_yaw_zero;
extern float motor_pitch_zero;
void hostpc_cmd_exec_task(void *parameter)
{
    while (1)
    {
        rt_thread_mdelay(5); // 5ms轮询，低CPU占用
        if (hostpc_cmd.cmd_valid == RT_TRUE)
        {
            switch (hostpc_cmd.cmd_code)
            {
				case CMD_YAW_CTRL: // Yaw轴角度控制
				{
					int32_t target_abs = hostpc_cmd.yaw_angle_val + 0;///motor_yaw_zero; // 加上机械零偏
					yaw_target_angle_abs = target_abs;
					axis[YAW_NO].ctrl_mode = AXIS_MODE_POSITION;// 切换到角度定位模式
					motor_set_multi_angle(MOTOR_YAW_NO, target_abs, hostpc_cmd.speed_val);
					hostpc_cmd.cmd_valid = RT_FALSE;
					break;
				}
			   case CMD_PITCH_CTRL: // Pitch轴角度控制
				{
					// 计算绝对目标角度（考虑机械零位偏移）
					int32_t target_abs = hostpc_cmd.pitch_angle_val + 0;///(int32_t)motor_pitch_zero/10;
					pitch_target_angle_abs = target_abs;
					axis[PITCH_NO].ctrl_mode = AXIS_MODE_POSITION;// 切换到角度定位模式
					motor_set_multi_angle(MOTOR_PITCH_NO, target_abs, hostpc_cmd.speed_val);
					hostpc_cmd.cmd_valid = RT_FALSE;
					break;
				}
//				case CMD_MID_BACK: // 一键回中
//					motor_set_multi_angle(MOTOR_PITCH_NO,0,100);
//					motor_set_multi_angle(MOTOR_YAW_NO,0,100);
//					hostpc_cmd.cmd_valid = RT_FALSE;
//					break;
				case CMD_READ_MOTOR_STATUS:
					/// void
					hostpc_cmd.cmd_valid = RT_FALSE;
					break;
				default:
					break;
            }
        }
    }
}
/* ======================================================================
 * RT-Thread任务函数 3：串口发送轮询任务（支持变长帧）
 * ====================================================================== */
void hostpc_tx_send_task(void *parameter)
{
    uint8_t send_buf[HOSTPC_MAX_FRAME_SIZE] = {0};
    uint8_t frame_len;
    
    while (1)
    {
        rt_thread_mdelay(100);
        
        if (tx_hostpc_read_idx < tx_hostpc_write_idx)
        {
            // 读取帧头，判断帧长度
            uint8_t frame_type = hostpc_tx_buf[tx_hostpc_read_idx + 1];
            uint8_t cmd_code = hostpc_tx_buf[tx_hostpc_read_idx + 2];
            
            // 计算实际帧长度
            if (frame_type == FRAME_TYPE_FEEDBACK)
            {
                switch (cmd_code)
                {
                    case CMD_FEEDBACK_ANGLE:
                        frame_len = HOSTPC_FEEDBACK_FRAME_SIZE;
                        break;
                    case CMD_FEEDBACK_POSTURE:
                        frame_len = HOSTPC_POSTURE_FRAME_SIZE;
                        break;
                    case CMD_HEADING_ANGLE:
                        frame_len = HOSTPC_FEEDBACK_FRAME_SIZE;
                        break;
                    default:
                        frame_len = HOSTPC_FRAME_SIZE;
                        break;
                }
            }
            else
            {
                frame_len = HOSTPC_FRAME_SIZE; // 控制帧固定12字节
            }
            
            rt_sem_take(&sem_hostpc_tx_idle, RT_WAITING_FOREVER);
            memcpy(send_buf, &hostpc_tx_buf[tx_hostpc_read_idx], frame_len);
            tx_hostpc_read_idx += frame_len;
            
            if (HAL_UART_Transmit_DMA(&huart4, send_buf, frame_len) != HAL_OK)
            {
                // 发送失败释放信号量
                rt_sem_release(&sem_hostpc_tx_idle);
            }
        }
        
        // 缓冲区读空重置
        if (tx_hostpc_read_idx >= tx_hostpc_write_idx)
        {
            tx_hostpc_read_idx = 0;
            tx_hostpc_write_idx = 0;
        }
    }
}
/* ======================================================================
 * RT-Thread任务函数 4：云台角度周期上报任务
 * 每100ms读取一次双轴电机实时角度，自动组包上报上位机
 * ====================================================================== */
static void hk_angle_report_task(void *parameter)
{
	const GimbalAtt_t *att = Gimbal_GetAtt();   // 新增：获取最新融合姿态
	float time = 0;
	uint8_t egg = 0;
    while (1)
    {
		egg = (0x0FU ^ Weapon_Sta) & 0x0FU;
		time = get_tick_s() + hostpc_cmd.cmd_delta_time;
		hostpc_send_posture_feedback(att->pitch_enu, att->roll_enu, att->yaw_enu, time, egg);
        rt_thread_mdelay(100);
    }
}
/* ======================================================================
 * UART4空闲中断自定义处理函数
 * 检测一帧数据接收完毕，拷贝缓存并释放接收信号量唤醒解析任务
 * ====================================================================== */
void USER_HK_UART_IRQHandler(UART_HandleTypeDef *huart)
{
    rt_base_t level;
    if (UART4 == huart->Instance && RESET != __HAL_UART_GET_FLAG(&huart4, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart4);
        HAL_UART_DMAStop(&huart4);
        uint8_t recv_len = HOSTPC_MAX_FRAME_SIZE - __HAL_DMA_GET_COUNTER(&hdma_uart4_rx);

        // 只接受有效长度（12 或 16）且帧头正确
        if ((recv_len == HOSTPC_FRAME_SIZE || recv_len == HOSTPC_FEEDBACK_FRAME_SIZE) &&
            hostpc_rx_buf[0] == FRAME_HEAD)
        {
            level = rt_hw_interrupt_disable();
            // 拷贝整帧到临时缓冲区
            memcpy(hostpc_rx_temp_buf, hostpc_rx_buf, recv_len);
            g_hostpc_rx_len = recv_len;   // 保存实际长度
            rt_sem_release(&sem_hostpc_rx_ready);
            rt_hw_interrupt_enable(level);
        }
        // 其他长度直接丢弃（可增加调试）

        HAL_UART_Receive_DMA(&huart4, hostpc_rx_buf, HOSTPC_MAX_FRAME_SIZE);
    }
}
/* ======================================================================
 * UART4全局中断总入口
 * ====================================================================== */
void UART4_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart4);
    USER_HK_UART_IRQHandler(&huart4);
}
/* ======================================================================
 * DMA1 Stream2 RX中断入口
 * ====================================================================== */
void DMA1_Stream2_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_uart4_rx);
}
/* ======================================================================
 * DMA1 Stream4 TX发送完成中断
 * 发送完成释放tx空闲信号量，允许下一帧发送
 * ====================================================================== */
void DMA1_Stream4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_uart4_tx);
    rt_sem_release(&sem_hostpc_tx_idle);
}
int MX_UART4_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    /* Peripheral clock enable */
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**UART4 GPIO Configuration
    PA0/WKUP     ------> UART4_TX
    PA1     ------> UART4_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART6 interrupt Init */
    HAL_NVIC_SetPriority(UART4_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);

    /* UART4 DMA Init */
    /* UART4_RX Init */
    hdma_uart4_rx.Instance = DMA1_Stream2;
    hdma_uart4_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_uart4_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_uart4_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_uart4_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_uart4_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_uart4_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_uart4_rx.Init.Mode = DMA_NORMAL;
    hdma_uart4_rx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_uart4_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_uart4_rx) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LINKDMA(&huart4, hdmarx, hdma_uart4_rx);

    /* UART4_TX Init */
    hdma_uart4_tx.Instance = DMA1_Stream4;
    hdma_uart4_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_uart4_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_uart4_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_uart4_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_uart4_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_uart4_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_uart4_tx.Init.Mode = DMA_NORMAL;
    hdma_uart4_tx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_uart4_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_uart4_tx) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LINKDMA(&huart4, hdmatx, hdma_uart4_tx);

    huart4.Instance = UART4;
    huart4.Init.BaudRate = 115200;
    huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1;
    huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX_RX;
    huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart4) != HAL_OK)
    {
        Error_Handler();
    }

    /* DMA2_Stream1_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);

    /* DMA2_Stream6_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Stream2_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream2_IRQn);

    __HAL_UART_ENABLE_IT(&huart4, UART_IT_IDLE);
    HAL_UART_Receive_DMA(&huart4, hostpc_rx_buf, HOSTPC_MAX_FRAME_SIZE);
	
	return RT_EOK;
}
INIT_BOARD_EXPORT(MX_UART4_Init);
/* ======================================================================
 * RT-Thread应用层总初始化：信号量创建、全部线程初始化并启动
 * 开机自启动标记：INIT_APP_EXPORT
 * ====================================================================== */
int hostpc_sys_init(void)
{
    // 创建同步信号量
    rt_sem_init(&sem_hostpc_rx_ready, "hostpc_rx", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&sem_hostpc_tx_idle, "hostpc_tx", 1, RT_IPC_FLAG_FIFO);

    // 通信三线程 优先级：解析20 > 发送20 > 指令执行17
    rt_thread_init(	&hostpc_rx_task_tcb,
					"hostpc_rx",
					hostpc_rx_parse_task,
					RT_NULL,
					hostpc_rx_task_stack,
					sizeof(hostpc_rx_task_stack),
					20,
					10);
    rt_thread_init(	&hostpc_tx_task_tcb,
					"hostpc_tx",
					hostpc_tx_send_task,
					RT_NULL,
					hostpc_tx_task_stack,
					sizeof(hostpc_tx_task_stack),
					20,
					10);
    rt_thread_init(	&hostpc_exec_task_tcb,
					"hostpc_exec",
					hostpc_cmd_exec_task,
					RT_NULL,
					hostpc_exec_task_stack,
					sizeof(hostpc_exec_task_stack),
					18,
					10);
    rt_thread_init(	&hk_angle_report_task_tcb,    // 角度上报线程 优先级19
					"hk_ang",
					hk_angle_report_task,
					RT_NULL,
					hk_angle_report_task_stack,
					sizeof(hk_angle_report_task_stack),
					19,
					10);					
    rt_thread_startup(&hostpc_rx_task_tcb);
    rt_thread_startup(&hostpc_tx_task_tcb);
    rt_thread_startup(&hostpc_exec_task_tcb);
    rt_thread_startup(&hk_angle_report_task_tcb);
    return RT_EOK;
}
INIT_APP_EXPORT(hostpc_sys_init);
