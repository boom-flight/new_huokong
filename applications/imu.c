#include "stm32f4xx_hal.h"
#include "string.h"
#include "motor.h"
#include "imu.h"
/* 串口2及DMA句柄（全局，供中断和任务使用） */
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;
/* 信号量：用于同步DMA接收完成中断与解析任务 */
static struct rt_semaphore sem_imu_data_ready;
/* 任务栈和TCB（静态分配） */
static uint8_t imu_task_stack[512];
static struct rt_thread imu_task_tcb;
/* IMU串口接收缓冲区（DMA直接填充）和临时缓冲区（供解析任务使用） */
uint8_t imu_rx_buffer[IMU_RX_BUF_SIZE];
uint8_t imu_temp_buffer[IMU_RX_BUF_SIZE];
/* 全局IMU数据结构，存储解析后的加速度、角速度、欧拉角及有效标志 */
imu_jy62_ts imu_jy62;

/**
 * @brief  MSH命令：打印JY62当前欧拉角(roll/pitch/yaw)
 * @param  argc: 参数个数
 * @param  argv: 参数列表
 * @note   在RT-Thread控制台输入：imu_euler  回车即可打印
 */
void msh_imu_print_euler(int argc, char *argv[])
{
    // 任务级临界区保护：防止读取时IMU解析任务修改数据，出现半更新乱值
    rt_enter_critical();

    // 辅助宏：将浮点数转为整数+小数字符串（四舍五入，保留两位小数）
    #define FLOAT_TO_STR_INT(f)   ((int)(f))
    #define FLOAT_TO_STR_FRAC(f)  ((int)(((f) - (int)(f)) * 100 + ((f) >= 0 ? 0.5 : -0.5)))
	#define FLOAT_TO_STR_NFRAC(f) (FLOAT_TO_STR_FRAC(f) >= 0 ? FLOAT_TO_STR_FRAC(f) : -FLOAT_TO_STR_FRAC(f))
	
    float roll  = imu_jy62.roll;
    float pitch = imu_jy62.pitch;
    float yaw   = imu_jy62.yaw;

    rt_kprintf("\r\n============ JY62 Euler Angle ============\r\n");
    rt_kprintf("Data Valid  : %s\r\n", imu_jy62.data_valid ? "YES" : "NO");
    rt_kprintf("Roll        : %d.%02d °\r\n", FLOAT_TO_STR_INT(roll),  FLOAT_TO_STR_NFRAC(roll));
    rt_kprintf("Pitch       : %d.%02d °\r\n", FLOAT_TO_STR_INT(pitch), FLOAT_TO_STR_NFRAC(pitch));
    rt_kprintf("Yaw         : %d.%02d °\r\n", FLOAT_TO_STR_INT(yaw),   FLOAT_TO_STR_NFRAC(yaw));
    rt_kprintf("===========================================\r\n");
	rt_kprintf("first_motor_pitch: %d°\r\n", (int)((float)motor_angle_read(MOTOR_PITCH_NO) / 100.0f));
    rt_exit_critical();

    #undef FLOAT_TO_STR_INT
    #undef FLOAT_TO_STR_FRAC
}
MSH_CMD_EXPORT(msh_imu_print_euler, imu euler angle print);


/**
 * @brief 通过DMA发送一条指令给IMU（例如配置命令）
 * @param cmd 待发送的命令数据缓冲区指针
 * @param size 数据长度（字节）
 * @retval HAL状态，HAL_OK表示启动成功
 * @note  该函数会阻塞等待前一次DMA发送完成，防止冲突
 */
uint8_t imu_send_cmd_dma(uint8_t *cmd, uint16_t size)
{
    // 等待上一次DMA发送完成（关键！防止数据粘包/发送重叠）
    while(HAL_DMA_GetState(huart2.hdmatx) == HAL_DMA_STATE_BUSY);
    // 等待串口发送完成（防止串口寄存器忙）
    while(HAL_UART_GetState(&huart2) == HAL_UART_STATE_BUSY_TX);
    // 标准DMA发送核心函数（HAL库官方API，必用这个）
    return HAL_UART_Transmit_DMA(&huart2, cmd, size);
}

/**
 * @brief 解析JY62模块的三帧数据（加速度/角速度/角度）
 * @param buffer 原始数据缓冲区（长度为 IMU_RX_BUF_SIZE）
 * @param data_length 实际接收到的字节数（应为33）
 * @retval 0解析成功，非0表示失败（1长度错，2帧头错）
 * @note  该函数会将解析结果填入 imu_jy62 结构体，并置位 data_valid
 */
uint8_t imu_data_parse(uint8_t* buffer, uint16_t data_length)
{
    int16_t raw_data;
    uint8_t check_sum = 0;

    // 1. 校验总长度（三帧连传固定33字节，你的原有判断正确）
    if (data_length != 33) {
        imu_jy62.data_valid = 0;
        return 1;
    }

    // 2. 校验三帧的帧头（每帧均以0x55开头，符合协议）
    if (buffer[0] != 0x55 || buffer[11] != 0x55 || buffer[22] != 0x55) {
        imu_jy62.data_valid = 0;
        return 2;
    }

    // ===================== 解析第一帧：加速度+温度帧（buffer[0]-buffer[10]）=====================
    if (buffer[1] == 0x51) {
        // 计算该帧校验和（前10字节累加和）
        check_sum = 0;
        for (uint8_t i = 0; i < 10; i++) check_sum += buffer[i];
        if (check_sum == buffer[10]) { // 校验通过
            // X轴加速度：((高字节<<8)|低字节)/32768*16g
            raw_data = (int16_t)(buffer[3] << 8 | buffer[2]);
            imu_jy62.acc_x = raw_data / 32768.0f * 16.0f;
            // Y轴加速度
            raw_data = (int16_t)(buffer[5] << 8 | buffer[4]);
            imu_jy62.acc_y = raw_data / 32768.0f * 16.0f;
            // Z轴加速度
            raw_data = (int16_t)(buffer[7] << 8 | buffer[6]);
            imu_jy62.acc_z = raw_data / 32768.0f * 16.0f;
            // 温度：((高字节<<8)|低字节)/340 + 36.53℃
            raw_data = (int16_t)(buffer[9] << 8 | buffer[8]);
            imu_jy62.temp = raw_data / 340.0f + 36.53f;
        }
    }

    // ===================== 解析第二帧：角速度帧（buffer[11]-buffer[21]）=====================
    if (buffer[12] == 0x52) {
        // 计算该帧校验和（第11-20字节累加和）
        check_sum = 0;
        for (uint8_t i = 11; i < 21; i++) check_sum += buffer[i];
        if (check_sum == buffer[21]) { // 校验通过
            // X轴角速度：((高字节<<8)|低字节)/32768*2000°/s
            raw_data = (int16_t)(buffer[14] << 8 | buffer[13]);
            imu_jy62.omega_x = raw_data / 32768.0f * 2000.0f;
            // Y轴角速度
            raw_data = (int16_t)(buffer[16] << 8 | buffer[15]);
            imu_jy62.omega_y = raw_data / 32768.0f * 2000.0f;
            // Z轴角速度
            raw_data = (int16_t)(buffer[18] << 8 | buffer[17]);
            imu_jy62.omega_z = raw_data / 32768.0f * 2000.0f;
        }
    }

    // ===================== 解析第三帧：角度帧（buffer[22]-buffer[32]）=====================
    if (buffer[23] == 0x53) {
        // 计算该帧校验和（第22-31字节累加和）
        check_sum = 0;
        for (uint8_t i = 22; i < 32; i++) check_sum += buffer[i];
        if (check_sum == buffer[32]) { // 校验通过
            // 横滚角(Roll)：((高字节<<8)|低字节)/32768*180°
            raw_data = (int16_t)(buffer[25] << 8 | buffer[24]);
            imu_jy62.roll = raw_data / 32768.0f * 180.0f;
            // 俯仰角(Pitch)
            raw_data = (int16_t)(buffer[27] << 8 | buffer[26]);
            imu_jy62.pitch = raw_data / 32768.0f * 180.0f;
            // 航向角(Yaw)
            raw_data = (int16_t)(buffer[29] << 8 | buffer[28]);
            imu_jy62.yaw = raw_data / 32768.0f * 180.0f;
        }
    }

    // 所有帧解析完成，置位有效标志
    imu_jy62.data_valid = 1;
    return 0;
}

/**
 * @brief IMU数据解析任务（线程函数），等待信号量，收到后解析数据
 * @param parameter 线程入口参数（未使用）
 * @note  该函数为 static，仅在本文件内可见，由 imu_init 创建并启动
 */
static void imu_parse_task(void *parameter)
{
    rt_err_t ret = RT_EOK;
	
	// ========== 原有初始化逻辑，完全不变 ==========
	__HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
	HAL_UART_Receive_DMA(&huart2, imu_rx_buffer, IMU_RX_BUF_SIZE);
	
    while(1)
    {
        // 永久等待：收到中断的信号量，才执行解析，无信号量则挂起，不占用CPU
        ret = rt_sem_take(&sem_imu_data_ready, RT_WAITING_FOREVER);
        if(ret == RT_EOK)
        {
			rt_enter_critical();
			
            // 调用你原来的解析函数，? 一行代码都没改！完美复用
            imu_data_parse(imu_temp_buffer, IMU_RX_BUF_SIZE);
			rt_exit_critical();
        }
    }
}

/**
 * @brief IMU模块初始化函数，创建解析任务并初始化信号量
 * @retval RT_EOK 固定成功
 * @note  该函数被 INIT_APP_EXPORT 宏修饰，在应用程序初始化阶段自动调用
 */
int imu_init(void)
{
    rt_sem_init(&sem_imu_data_ready, "sem_imu", 0, RT_IPC_FLAG_FIFO);
    
    rt_thread_init(&imu_task_tcb,
                   "imu_task",    			// 任务名
                   imu_parse_task,			// 任务函数
                   RT_NULL,       			// 任务参数
                   &imu_task_stack[0],		// 任务栈起始地址
                   sizeof(imu_task_stack),	// 栈大小
                   13,             			// 优先级1 (和云台规划一致)原来18
                   10);           			// 时间片，高优先级用不到，填10即可
    
    rt_thread_startup(&imu_task_tcb); // 启动任务
				   
	return RT_EOK;

}
INIT_APP_EXPORT(imu_init);

/**
 * @brief USART2空闲中断处理函数（在串口中断中被调用）
 * @param huart 串口句柄（此处固定为&huart2）
 * @note  该函数在中断上下文执行，检测到空闲中断后停止DMA，拷贝数据并释放信号量
 */
void USER_IMU_UART_IRQHandler(UART_HandleTypeDef *huart)
{
	rt_base_t level;
    if (USART2 == huart->Instance) 
    {
        if (RESET != __HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE)) 
        {
            __HAL_UART_CLEAR_IDLEFLAG(&huart2); // 清除空闲中断标志
            HAL_UART_DMAStop(&huart2);          // 停止DMA传输
            
			uint8_t data_length = IMU_RX_BUF_SIZE  - __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
            
			// 数据有效则拷贝到临时缓冲区，JY62固定33字节，直接截取
			if (data_length >= IMU_RX_BUF_SIZE)
			{
				level = rt_hw_interrupt_disable();
				memcpy(imu_temp_buffer, imu_rx_buffer, IMU_RX_BUF_SIZE);
                
                rt_sem_release(&sem_imu_data_ready);
				rt_hw_interrupt_enable(level);
			}
    
			HAL_UART_Receive_DMA(&huart2, imu_rx_buffer, IMU_RX_BUF_SIZE); // 重启DMA接收
        }
    }
}

/**
 * @brief 初始化USART2的硬件资源（GPIO、时钟、UART参数、DMA及中断）
 * @note  该函数被 INIT_BOARD_EXPORT 宏修饰，在系统启动早期自动执行
 * @retval RT_EOK 固定返回成功
 */
static int MX_USART2_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();          // 使能 GPIOD 时钟

    /* PD5 - USART2_TX (AF7) */
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* PD6 - USART2_RX (AF7) */
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;    // RX 也可用 AF_PP
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* UART 配置（不变） */
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        rt_kprintf("uart2 init failed!\r\n");
    }

    /* DMA 时钟 */
    __HAL_RCC_DMA1_CLK_ENABLE();   // 只需 DMA1，DMA2 不需要

    /* ---- 修改 1：RX 使用 DMA1_Stream5，Channel 4 ---- */
    hdma_usart2_rx.Instance = DMA1_Stream5;
    hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4;          // 新增！
    hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode = DMA_NORMAL;
    hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK) {
        rt_kprintf("hdma_usart2_rx init failed\r\n");
    }
    __HAL_LINKDMA(&huart2, hdmarx, hdma_usart2_rx);

    /* ---- 修改 2：TX 使用 DMA1_Stream6，Channel 4 ---- */
    hdma_usart2_tx.Instance = DMA1_Stream6;
    hdma_usart2_tx.Init.Channel = DMA_CHANNEL_4;          // 新增！
    hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK) {
        rt_kprintf("hdma_usart2_tx init failed\r\n");
    }
    __HAL_LINKDMA(&huart2, hdmatx, hdma_usart2_tx);

    /* ---- 修改 3：中断号 ---- */
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 3, 0);   // RX 中断
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 3, 0);   // TX 中断
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

    HAL_NVIC_SetPriority(USART2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    return RT_EOK;
}
INIT_BOARD_EXPORT(MX_USART2_UART_Init);

/**
  * @brief DMA RX 中断服务函数
  */
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

/**
  * @brief DMA TX 中断服务函数.
  */
void DMA1_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_tx);
}

/**
  * @brief USART2全局中断入口
  */
void USART2_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart2);
	USER_IMU_UART_IRQHandler(&huart2);	// 调用自定义空闲中断处理
}

