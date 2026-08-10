#include "motor.h"
#include "stm32f4xx_hal.h"
#include <limits.h>
#include <string.h>

// ===================== 全局变量 =====================
CAN_HandleTypeDef	hcan1;									// CAN1句柄
uint8_t				can_init_flag = RT_EBUSY;				// CAN初始化标志
uint8_t				g_can_rx_buf[CAN_RX_BUF_SIZE] = {0};	// CAN接收数据缓冲池
mg4005_motor_t 		motors[MAX_MOTOR_NUM] = {0};			// 电机状态结构体数组，存储两个电机的实时状态
															// 电机实时控制参数数组，由 can_tx_task 周期性发送
motor_real_ctrl_t 	motor_real_ctrl[MAX_MOTOR_NUM] = {{MOTOR_CTRL_IDLE, 0, 0, 5},
													  {MOTOR_CTRL_IDLE, 0, 0, 5}};
// ===================== 内核IPC资源 =====================
static	struct	rt_mutex	motor_ctrl_mutex;  		// 互斥锁：保护 motor_real_ctrl 的并发访问

// ===================== 任务栈 =====================
ALIGN(RT_ALIGN_SIZE)								// 内存 4K 对齐
static	uint8_t	can_tx_task_stack[1024];			// CAN 发送任务栈（处理所有控制指令发送）
static	uint8_t	multi_angle_read_task_stack[512];	// 多圈角度定时查询任务栈
static	struct	rt_thread can_tx_task_tcb;			// 静态分配两个线程的 TCB（线程控制块），分别管理 CAN 发送任务和角度查询任务
static	struct	rt_thread multi_angle_read_task_tcb;

/* ======================================================================
 * CAN 底层发送函数
 * 参数：id - CAN 标准帧 ID；msg - 数据指针；len - 数据长度（最大8）
 * 返回值：0=成功，非0=失败
 * ====================================================================== */
static uint8_t can_send_msg(uint32_t id, uint8_t *msg, uint8_t len)
{
    CAN_TxHeaderTypeDef tx_hdr = {0};
    uint32_t mailbox;
    tx_hdr.StdId = id;
    tx_hdr.IDE = CAN_ID_STD;    // 标准帧（11位ID）
    tx_hdr.RTR = CAN_RTR_DATA;  // 数据帧
    tx_hdr.DLC = len;           // 数据长度

    HAL_StatusTypeDef ret = HAL_CAN_AddTxMessage(&hcan1, &tx_hdr, msg, &mailbox);
    return (ret != HAL_OK);     // 返回0表示成功
}
/* ======================================================================
 * 任务1：定时查询多圈角度
 * 说明：交替查询 Yaw 和 Pitch 电机的多圈角度，避免同时发送导致 CAN 总线冲突
 * ====================================================================== */
static void multi_angle_read_task(void *parameter)
{
    while (1)
    {
        if (MOTOR_YAW_NO < MAX_MOTOR_NUM)
            motor_state_read(MOTOR_YAW_NO);
        rt_thread_mdelay(MOTOR_QUERY_DELAY_MS);
        if (MOTOR_PITCH_NO < MAX_MOTOR_NUM)
            motor_state_read(MOTOR_PITCH_NO);
        rt_thread_mdelay(READ_ANGLE_PERIOD_MS - MOTOR_QUERY_DELAY_MS);
    }
}
/* ======================================================================
 * 任务2：CAN 发送任务（核心控制循环，周期 5ms）
 * 说明：遍历两个电机，根据当前控制模式发送对应的 CAN 指令
 * 优先级：16（高于角度读取任务17，低于串口/IMU任务）
 * ====================================================================== */
static void can_tx_task(void *parameter)
{
    uint8_t cmd_buf[8] = {0}; 		// 发送帧CAN数据缓存

    while (1)
    {    
		// 1. 遍历所有电机（0=Yaw，1=Pitch）
        for (uint8_t motor_no = 0; motor_no < MAX_MOTOR_NUM; motor_no++)
        {
			// -------------------------- 加锁：独占控制参数结构体 --------------------------
            rt_mutex_take(&motor_ctrl_mutex, RT_WAITING_FOREVER);
            uint32_t can_id = motors[motor_no].can_id;				// 当前电机对应的CAN标准ID
            motor_real_ctrl_t *ctrl = &motor_real_ctrl[motor_no];	// 指向当前电机控制参数
            memset(cmd_buf, 0, 8);									// 清空CAN发送缓冲区，防止残留旧数据
			// 分支 1：MOTOR_CTRL_ANGLE 角度闭环控制（持续发送 200Hz）
            if (ctrl->ctrl_mode == MOTOR_CTRL_ANGLE)
            {
                cmd_buf[0] = 0xA4;									// MG电机：绝对角度闭环指令码
                *(uint16_t *)&cmd_buf[2] = ctrl->max_speed;			// 2、3字节：最大限制转速 uint16小端
                *(int32_t  *)&cmd_buf[4] = ctrl->target_angle;		// 4~7字节：目标多圈绝对角度 int32小端
				can_send_msg(can_id, cmd_buf, 8);
            }
			// 分支 2：MOTOR_CTRL_SPEED 速度闭环控制（持续发送 200Hz）
            else if (ctrl->ctrl_mode == MOTOR_CTRL_SPEED)
            {
                cmd_buf[0] = 0xA2;									// MG电机：速度闭环指令码
                *(int32_t *)&cmd_buf[4] = ctrl->target_speed; 		// 4~7字节目标转速 int32
				can_send_msg(can_id, cmd_buf, 8);
            }
			// 分支 3：MOTOR_CTRL_READ_STATE 读取多圈角度查询指令
            else if (ctrl->ctrl_mode == MOTOR_CTRL_READ_STATE)
            {
                cmd_buf[0] = 0x92;
                can_send_msg(can_id, cmd_buf, 8);
                ctrl->ctrl_mode = MOTOR_CTRL_IDLE;
            }
            rt_mutex_release(&motor_ctrl_mutex);					// 释放互斥锁
        }// for电机循环结束
        rt_thread_mdelay(TASK_DELAY_5MS);							// 阻塞5ms，下一轮循环
    }// while(1)
}


/* ======================================================================
 * CAN 接收中断回调（在中断上下文中执行）
 * 说明：这是一个中断级的 CAN 数据分发器，它把收到的每一帧数据按 ID 分发给对应的电机
 * 并根据帧类型更新电机的角度或状态信息。整个过程在关中断保护下完成，确保数据不被破坏。
 * ====================================================================== */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    uint8_t	motor_idx = 0;							// 匹配到的电机下标 0=Yaw 1=Pitch	
	CAN_RxHeaderTypeDef	rx_hdr;						// HAL库CAN接收帧头结构体，存放ID、长度、帧类型等

    rt_base_t level = rt_hw_interrupt_disable();	// 关中断保护，线程安全
	// 判断 CAN 实例 + 成功读取硬件 FIFO 报文
    if (CAN1 == hcan->Instance && HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_hdr, g_can_rx_buf) == HAL_OK)
    {
		// 1.电机ID获取及匹配
        // 循环匹配对应电机（CAN ID匹配）
        for (motor_idx = 0; motor_idx < MAX_MOTOR_NUM; motor_idx++)
        {
            if (motors[motor_idx].can_id == rx_hdr.StdId)
            {
                break;
            }
        }
        if (motor_idx >= MAX_MOTOR_NUM) // 无匹配电机，直接退出
        {
            rt_hw_interrupt_enable(level);
            return;
        }
        // 2.解析指令
		// 分支 1：解析 0x92 多圈角度报文
        if (g_can_rx_buf[0] == 0x92)
        {
            int32_t angle = 0;	// 这里仅拷贝低 8 字节，最大圈数据为360,000
			// 步骤1：拷贝到栈局部（私有内存，全程不碰全局）
            memcpy(&angle, &g_can_rx_buf[1], sizeof(angle));
			// 步骤2：仅最后一步一次赋值写入全局
            motors[motor_idx].multi_angle = angle;
        }
		// 分支 2：解析 0x9C 电机状态反馈帧
		else if (g_can_rx_buf[0] == 0x9C ||
				 g_can_rx_buf[0] == 0xA2 ||
				 g_can_rx_buf[0] == 0xA4)
		{
			motors[motor_idx].temp     = (int8_t)g_can_rx_buf[1];
			// MG转矩电流 iq：buf2低字节，buf3高字节
			motors[motor_idx].current  = (int16_t)((g_can_rx_buf[3] << 8) | g_can_rx_buf[2]);
			// 转速 speed：buf4低字节，buf5高字节
			motors[motor_idx].speed    = (int16_t)((g_can_rx_buf[5] << 8) | g_can_rx_buf[4]);
			// 编码器 encoder：buf6低字节，buf7高字节
			motors[motor_idx].encoder  = (uint16_t)((g_can_rx_buf[7] << 8) | g_can_rx_buf[6]);
		}
    }
    rt_hw_interrupt_enable(level); // 开中断
} 
/* ======================================================================
 * 对外 API：设置电机目标角度（角度闭环）
 * 参数：motor_no - 电机编号；target_angle - 目标角度（0.01° 单位）
 *       max_speed - 最大转速（0.01 dps 单位，范围 0~MAX_SPEED_LIMIT）
 * 说明：内部将相对角度转换为绝对角度（基于电机当前多圈值）
 * ====================================================================== */
rt_err_t motor_set_multi_angle(uint8_t motor_no, int32_t target_angle, uint16_t max_speed)
{
	// 1. 前置判断：CAN 模块是否初始化完成
	if (can_init_flag != RT_EOK) 	return -RT_ERROR;
	// 2. 电机编号合法性校验
    if (motor_no >= MAX_MOTOR_NUM)	return -RT_ERROR;
    // 3. 限速上限钳位及下限钳位
    if (max_speed > MAX_SPEED_LIMIT)max_speed = MAX_SPEED_LIMIT;
	// 4.角度限幅（备用保护 后面要加且针对yaw和pitch不同适配）
//    if (target_angle > 3000)			target_angle =  3000;
//    else if (target_angle < -3000)	target_angle = -3000;
	// 5.定义一个绝对角度的局部变量，将其原本0.1°的角度单位转换为0.01°的角度单位
	int32_t abs_angle = MOTOR_ABS_FROM_REL(target_angle);
	// 6.实时控制参数数组赋值
    rt_mutex_take(&motor_ctrl_mutex, RT_WAITING_FOREVER);		// 永久等待获取互斥锁
	if (max_speed == 0){
		motor_real_ctrl[motor_no].ctrl_mode = MOTOR_CTRL_SPEED;
		motor_real_ctrl[motor_no].target_speed = 0;
	}else{
		motor_real_ctrl[motor_no].ctrl_mode = MOTOR_CTRL_ANGLE;	// 切换为持续角度闭环模式
		motor_real_ctrl[motor_no].target_angle = abs_angle;		// 写入换算完成的绝对多圈目标角度
		motor_real_ctrl[motor_no].max_speed = max_speed;		// 写入本次运动的最大限速
	}
    rt_mutex_release(&motor_ctrl_mutex);					// 释放互斥锁
    return RT_EOK;
}
/* ======================================================================
 * 对外 API：设置电机目标转速（速度闭环）
 * 参数：motor_no - 电机编号；speed_dps01 - 目标转速（0.01 dps 单位）
 * ====================================================================== */
rt_err_t motor_set_speed(uint8_t motor_no, int32_t speed_dps01)
{
	if (can_init_flag != RT_EOK) 	return -RT_ERROR;
    if (motor_no >= MAX_MOTOR_NUM)	return -RT_ERROR;
    rt_mutex_take(&motor_ctrl_mutex, RT_WAITING_FOREVER);
    motor_real_ctrl[motor_no].ctrl_mode = MOTOR_CTRL_SPEED;
    motor_real_ctrl[motor_no].target_speed = speed_dps01;
    rt_mutex_release(&motor_ctrl_mutex);
    return RT_EOK;
}
/* ======================================================================
 * 内部函数：触发一次角度读取（向电机发送 0x92 指令）
 * 说明：由 multi_angle_read_task 周期性调用
 * ====================================================================== */
rt_err_t motor_state_read(uint8_t motor_no)
{
	if (can_init_flag != RT_EOK) 	return -RT_ERROR;
    if (motor_no >= MAX_MOTOR_NUM)	return -RT_ERROR;
    rt_mutex_take(&motor_ctrl_mutex, RT_WAITING_FOREVER);
    motor_real_ctrl[motor_no].ctrl_mode = MOTOR_CTRL_READ_STATE;
    rt_mutex_release(&motor_ctrl_mutex);
    return RT_EOK;
}
/* ======================================================================
 * 对外 API：读取电机当前角度（相对角度，单位0.1°）
 * 说明：从 motors 结构体读取最新多圈角度，转换为相对角度并缩放到0.1°单位
 * ====================================================================== */
int32_t motor_angle_read(uint8_t motor_no)
{
	if (can_init_flag != RT_EOK) 	return -RT_ERROR;
    if (motor_no >= MAX_MOTOR_NUM)	return -RT_ERROR;
    rt_enter_critical();
    int32_t angle = MOTOR_REL_FROM_ABS(motors[motor_no].multi_angle);
    rt_exit_critical();
    return angle;
}
/* ======================================================================
 * CAN 中断服务函数（连接 HAL 库和硬件中断向量）
 * ====================================================================== */
void CAN1_RX0_IRQHandler(void)   // 更正为 F4 标准名称
{
    HAL_CAN_IRQHandler(&hcan1);
}
/* ======================================================================
 * CAN 硬件初始化（GPIO、时钟、滤波器、中断）
 * 说明：波特率 = 42MHz / (5 * (1+4+4)) = 1Mbps
 * ====================================================================== */
static int MX_CAN1_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	__HAL_RCC_CAN1_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();

	GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	
	hcan1.Instance = CAN1;
	hcan1.Init.Prescaler = 5;
	hcan1.Init.Mode = CAN_MODE_NORMAL;
	hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
	hcan1.Init.TimeSeg1 = CAN_BS1_4TQ;
	hcan1.Init.TimeSeg2 = CAN_BS2_4TQ;
	hcan1.Init.AutoRetransmission = ENABLE;

	hcan1.Init.TimeTriggeredMode = DISABLE;
	hcan1.Init.AutoBusOff = DISABLE;
	hcan1.Init.AutoWakeUp = DISABLE;
	hcan1.Init.ReceiveFifoLocked = DISABLE;
	hcan1.Init.TransmitFifoPriority = DISABLE;
	
	if (HAL_CAN_Init(&hcan1) != HAL_OK)
        while (1)
            ;
	
	HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
	
	CAN_FilterTypeDef sFilterConfig	  = {0}; 					// 定义 HAL 库过滤器配置结构体
    // 配置过滤器参数
    sFilterConfig.FilterBank            = 0; 						// 过滤器编号0
    sFilterConfig.FilterMode            = CAN_FILTERMODE_IDMASK;	// 使用标识符掩码模式
    sFilterConfig.FilterScale           = CAN_FILTERSCALE_32BIT;	// 使用32位过滤器
//    sFilterConfig.FilterIdHigh          = 0x0000; 				// 设置ID高16位
//    sFilterConfig.FilterIdLow           = 0x0000;  				// 设置ID低16位
//    sFilterConfig.FilterMaskIdHigh      = 0x0000; 				// 掩码高16位
//    sFilterConfig.FilterMaskIdLow       = 0x0000;  				// 掩码低16位
    sFilterConfig.FilterFIFOAssignment  = CAN_FILTER_FIFO0; 		// 过滤器关联到 FIFO0
    sFilterConfig.FilterActivation      = ENABLE; 				// 激活过滤器
    sFilterConfig.SlaveStartFilterBank  = 14; 					// 从过滤器起始编号（用于多CAN）或许不需要

    // 调用 HAL 库函数配置过滤器
    if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK)
        while (1)
            ;
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
        while (1)
            ;
    if (HAL_CAN_Start(&hcan1) != HAL_OK)
        while (1)
            ;

    motors[MOTOR_YAW_NO].can_id = MOTOR_YAW_CAN_ID;
    motors[MOTOR_PITCH_NO].can_id = MOTOR_PITCH_CAN_ID;
    return RT_EOK;
}
/* ======================================================================
 * CAN 模块初始化入口（系统启动时自动调用）
 * 说明：初始化硬件、互斥锁、创建两个任务
 * ====================================================================== */
int can_init(void)
{
    MX_CAN1_Init();
    // 只初始化【唯一】必须的互斥锁
    rt_mutex_init(&motor_ctrl_mutex, "motor_mtx", RT_IPC_FLAG_FIFO);

    // 只创建2个核心任务，删除接收线程
    rt_thread_init(	&can_tx_task_tcb, 
					"motor_ctrl", 
					can_tx_task, 
					RT_NULL,	
					can_tx_task_stack,
					sizeof(can_tx_task_stack),
					16,
					10);
    rt_thread_init(	&multi_angle_read_task_tcb,
					"motor_read",
					multi_angle_read_task,
					RT_NULL,
					multi_angle_read_task_stack,
					sizeof(multi_angle_read_task_stack),
					17,
					10);

    // 只启动2个任务
    rt_thread_startup(&can_tx_task_tcb);
    rt_thread_startup(&multi_angle_read_task_tcb);
	can_init_flag = RT_EOK;

    return RT_EOK;
}
INIT_APP_EXPORT(can_init);
