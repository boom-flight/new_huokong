#include "dianhuo.h"
#include "hostpc.h"
#include <string.h>
// 火控业务全局状态
fire_state_feedback_t 			fire_feedback;			// 火控上报状态缓存结构体
uint8_t fire_charge_level[5] 	= {0};					// 1~4号弹药充电等级索引1~4
uint8_t fire_unlock_flag 		= FIRE_UNLOCK_NONE;		// 全局火控解锁总标志

static uint8_t fire_control_task_stack[512];
static struct rt_thread fire_control_task_tcb;

static int MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    // ========== 1. 设置所有输出引脚初始电平 ==========
    // 火控输出（全部先拉低）
    HAL_GPIO_WritePin(GPIOA, Fire_1_Pin | Charge_1_Pin | Charge_2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, Fire_2_Pin | Discharge_Pin, GPIO_PIN_RESET);  // Discharge 后面会单独拉高
    HAL_GPIO_WritePin(GPIOE, Fire_3_Pin | Charge_3_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, Fire_4_Pin | Charge_V_Pin | Charge_4_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, Check_V_Pin, GPIO_PIN_RESET);

    // Discharge 默认高电平（对应硬件安全）
    HAL_GPIO_WritePin(Discharge_GPIO_Port, Discharge_Pin, GPIO_PIN_SET);

    // ========== 2. 配置输出引脚 ==========
    // --- GPIOA: Fire_1(PA2), Charge_1(PA3), Charge_2(PA5) ---
    GPIO_InitStruct.Pin = Fire_1_Pin | Charge_1_Pin | Charge_2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // --- GPIOB: Fire_2(PB0), Discharge(PB12) ---
    GPIO_InitStruct.Pin = Fire_2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = Discharge_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;          // 原为上拉，保持
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(Discharge_GPIO_Port, &GPIO_InitStruct);

    // --- GPIOE: Fire_3(PE12), Charge_3(PE10) ---
    GPIO_InitStruct.Pin = Fire_3_Pin | Charge_3_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    // --- GPIOD: Fire_4(PD9), Charge_V(PD10), Charge_4(PD8) ---
    GPIO_InitStruct.Pin = Fire_4_Pin | Charge_V_Pin | Charge_4_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    // --- GPIOC: Check_V(PC5) ---
    GPIO_InitStruct.Pin = Check_V_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(Check_V_GPIO_Port, &GPIO_InitStruct);

   // ========== 3. 配置输入引脚 ==========
	// Weapon1_STA(PA4)
	GPIO_InitStruct.Pin = Weapon1_STA_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(Weapon1_STA_GPIO_Port, &GPIO_InitStruct);

	// Weapon2_STA(PE9)
	GPIO_InitStruct.Pin = Weapon2_STA_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(Weapon2_STA_GPIO_Port, &GPIO_InitStruct);

	// Weapon3_STA(PE14)
	GPIO_InitStruct.Pin = Weapon3_STA_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(Weapon3_STA_GPIO_Port, &GPIO_InitStruct);

	// Weapon4_STA(PB11)
	GPIO_InitStruct.Pin = Weapon4_STA_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(Weapon4_STA_GPIO_Port, &GPIO_InitStruct);

	// ---- 新增限位开关 ----
	GPIO_InitStruct.Pin = Limit_L_Pin | Limit_R_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;   // 推荐上拉，低电平触发
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    return RT_EOK;
}
INIT_BOARD_EXPORT(MX_GPIO_Init);

uint8_t Weapon_Sta = 0; // 武器状态寄存器 低四位对应1-4号炮位状态：1=有弹，0=无弹

// 硬件状态检测+防抖滤波 (原逻辑不变)
void check_control(void)
{
    int i;
    int num1_1 = 0, num1_2 = 0, num2_1 = 0, num2_2 = 0, num3_1 = 0, num3_2 = 0, num4_1 = 0, num4_2 = 0;
    Check_V_ON;
    rt_thread_mdelay(100);
    for (i = 0; i < 20; i++)
    {
        if (Weapon1_STA)
        {
            num1_1++;
            if (num1_1 > 15)
            {
                Weapon_Sta &= 0xfe;
                num1_1 = 0;
            }
        }
        else
        {
            num1_2++;
            if (num1_2 > 15)
            {
                Weapon_Sta |= 0x01;
                num1_2 = 0;
            }
        }

        if (Weapon2_STA)
        {
            num2_1++;
            if (num2_1 > 15)
            {
                Weapon_Sta &= 0xfd;
                num2_1 = 0;
            }
        }
        else
        {
            num2_2++;
            if (num2_2 > 15)
            {
                Weapon_Sta |= 0x02;
                num2_2 = 0;
            }
        }

        if (Weapon3_STA)
        {
            num3_1++;
            if (num3_1 > 15)
            {
                Weapon_Sta &= 0xfb;
                num3_1 = 0;
            }
        }
        else
        {
            num3_2++;
            if (num3_2 > 15)
            {
                Weapon_Sta |= 0x04;
                num3_2 = 0;
            }
        }

        if (Weapon4_STA)
        {
            num4_1++;
            if (num4_1 > 15)
            {
                Weapon_Sta &= 0xf7;
                num4_1 = 0;
            }
        }
        else
        {
            num4_2++;
            if (num4_2 > 15)
            {
                Weapon_Sta |= 0x08;
                num4_2 = 0;
            }
        }
    }
    Check_V_OFF;
}

void Singer_fire(void)
{
}
void Double_fire(void)
{
}
void All_fire(void)
{
}

// 全炮位充电
void Charge_control(void)
{
    Discharge_OFF;
    Charge_V_ON;
    Charge_1_ON;
    Charge_2_ON;
    Charge_3_ON;
    Charge_4_ON;
    rt_thread_mdelay(2000);
}

// 全炮位放电
void Discharge_control(void)
{
    Charge_V_OFF;
    Charge_1_OFF;
    Charge_2_OFF;
    Charge_3_OFF;
    Charge_4_OFF;
    Discharge_ON;
    rt_thread_mdelay(2000);
}

// 一级充电 不区分ID，全炮位统一一级充电
static void fire_charge_level1(uint8_t fire_id)
{
    Discharge_OFF;
    Charge_V_ON;
	
    rt_thread_mdelay(1000); // 一级充电延时
}

// 指定ID二级充电 (单个蛋)
static void fire_charge_level2(uint8_t fire_id)
{
    switch (fire_id)
    {
    case 1:
        Charge_1_ON;
        break;
    case 2:
        Charge_2_ON;
        break;
    case 3:
        Charge_3_ON;
        break;
    case 4:
        Charge_4_ON;
        break;
    default:
        return;
    }
    rt_thread_mdelay(1000);
}

// 指定ID精准点火 (单个蛋)
static void fire_shot(uint8_t fire_id)
{
    switch (fire_id)
    {
    case 1:
        Fire_1_ON;
        rt_thread_mdelay(100);
        Fire_1_OFF;
        break;
    case 2:
        Fire_2_ON;
        rt_thread_mdelay(100);
        Fire_2_OFF;
        break;
    case 3:
        Fire_3_ON;
        rt_thread_mdelay(100);
        Fire_3_OFF;
        break;
    case 4:
        Fire_4_ON;
        rt_thread_mdelay(100);
        Fire_4_OFF;
        break;
    default:
        return;
    }
    fire_charge_level[fire_id] = CHARGE_LEVEL_NONE;
}

void fire_control_task(void *parameter)
{
    uint8_t fire_mask = 0;
    uint8_t egg_idx = 0;
    uint8_t has_egg = 0;
    check_control();
    Discharge_control();
    rt_thread_mdelay(500);

    while (1)
    {
        rt_thread_mdelay(3);
        check_control();

        fire_feedback.egg_charge[1] = fire_charge_level[1];
        fire_feedback.egg_charge[2] = fire_charge_level[2];
        fire_feedback.egg_charge[3] = fire_charge_level[3];
        fire_feedback.egg_charge[4] = fire_charge_level[4];

        fire_feedback.fire_unlock_state = fire_unlock_flag;

        fire_feedback.egg_has_byte = Weapon_Sta & 0x0F; // 只取低4位（1-4号蛋有无弹）

        rt_enter_critical();
		
        // ========== 泄放指令 ==========
        if (hostpc_cmd.cmd_code == CMD_FIRE_DISCHARGE && hostpc_cmd.cmd_valid == RT_TRUE)
        {
            rt_exit_critical();
            Discharge_control();
            rt_kprintf("执行泄放指令！\r\n");
            hostpc_send_fire_state(&fire_feedback);   // 修正点①
            rt_enter_critical();
            hostpc_cmd.cmd_valid = RT_FALSE;
            rt_exit_critical();
            continue;
        }
		
        // ========== 0解指令处理 ==========
        if (hostpc_cmd.cmd_code == CMD_FIRE_0_DEC && hostpc_cmd.cmd_valid == RT_TRUE)
        {
            fire_unlock_flag = FIRE_UNLOCK_OK;
            memset(fire_charge_level, 0, sizeof(fire_charge_level));
            rt_exit_critical();

            Discharge_control();
            rt_kprintf("火控0解完成，总解锁成功！\r\n");

            fire_feedback.fire_unlock_state = fire_unlock_flag;
            fire_feedback.egg_has_byte = Weapon_Sta & 0x0F; // 只取低4位（1-4号蛋有无弹）

            // 反馈0解后的完整状态
            hostpc_send_fire_state(&fire_feedback);

            rt_enter_critical();
            hostpc_cmd.cmd_valid = RT_FALSE;
            rt_exit_critical();
        }
        else if (hostpc_cmd.cmd_code == CMD_QUERY_FIRE_STATE && hostpc_cmd.cmd_valid == RT_TRUE)
        {
            rt_exit_critical();
            rt_kprintf("收到查询指令，反馈所有蛋状态：有无弹=0x%02X，解锁状态=%d\r\n",
                       fire_feedback.egg_has_byte, fire_feedback.fire_unlock_state);

            fire_feedback.egg_charge[1] = fire_charge_level[1];
            fire_feedback.egg_charge[2] = fire_charge_level[2];
            fire_feedback.egg_charge[3] = fire_charge_level[3];
            fire_feedback.egg_charge[4] = fire_charge_level[4];

            fire_feedback.egg_has_byte = Weapon_Sta & 0x0F; // 只取低4位（1-4号蛋有无弹）

            // 反馈完整的火控状态（有无弹+充电层级+解锁状态）
            hostpc_send_fire_state(&fire_feedback);

            rt_enter_critical();
            hostpc_cmd.cmd_valid = RT_FALSE;
            rt_exit_critical();
        }

        // ========== 已解锁：处理充电/点火/查询 ==========
        else if (fire_unlock_flag == FIRE_UNLOCK_OK)
        {
            fire_mask = hostpc_cmd.fire_id;
            rt_exit_critical();

            // ========== 1. 一级充电 ==========
            rt_enter_critical();
            if (hostpc_cmd.cmd_code == CMD_FIRE_1_DEC && hostpc_cmd.cmd_valid == RT_TRUE)
            {
                rt_exit_critical();
                fire_charge_level1(fire_mask);
                rt_enter_critical();
                fire_charge_level[1] = CHARGE_LEVEL_1;
                fire_charge_level[2] = CHARGE_LEVEL_1;
                fire_charge_level[3] = CHARGE_LEVEL_1;
                fire_charge_level[4] = CHARGE_LEVEL_1;
                rt_exit_critical();
                rt_kprintf("所有炮位 一级充电完成(1解)\r\n");

                fire_feedback.egg_charge[1] = fire_charge_level[1];
                fire_feedback.egg_charge[2] = fire_charge_level[2];
                fire_feedback.egg_charge[3] = fire_charge_level[3];
                fire_feedback.egg_charge[4] = fire_charge_level[4];

                fire_feedback.egg_has_byte = Weapon_Sta & 0x0F; // 只取低4位（1-4号蛋有无弹）

                // 反馈一级充电后的状态
                hostpc_send_fire_state(&fire_feedback);

                rt_enter_critical();
                hostpc_cmd.cmd_valid = RT_FALSE;
                rt_exit_critical();
            }
            // ========== 2. 二级充电 ==========
            else if (hostpc_cmd.cmd_code == CMD_FIRE_2_DEC && hostpc_cmd.cmd_valid == RT_TRUE && fire_mask != 0)
            {
                rt_exit_critical();
                for (egg_idx = 1; egg_idx <= 4; egg_idx++)
                {
                    if (fire_mask & (0x01 << (egg_idx - 1)))
                    {
                        rt_enter_critical();
                        has_egg = (Weapon_Sta & (0x01 << (egg_idx - 1))) ? 1 : 0;
                        if (!has_egg)
                        {
                            rt_exit_critical();
                            rt_kprintf("蛋ID:%d 无弹，禁止二级充电！\r\n", egg_idx);
                            continue;
                        }
                        if (fire_charge_level[egg_idx] == CHARGE_LEVEL_1)
                        {
                            rt_exit_critical();
                            fire_charge_level2(egg_idx);
                            rt_enter_critical();
                            fire_charge_level[egg_idx] = CHARGE_LEVEL_2;
                            rt_exit_critical();
                            rt_kprintf("蛋ID:%d 二级充电完成(2解)\r\n", egg_idx);
                        }
                        else
                        {
                            rt_exit_critical();
                            rt_kprintf("蛋ID:%d 有弹但未完成一级充电，禁止二级充电！\r\n", egg_idx);
                        }
                    }
                }

                fire_feedback.egg_charge[1] = fire_charge_level[1];
                fire_feedback.egg_charge[2] = fire_charge_level[2];
                fire_feedback.egg_charge[3] = fire_charge_level[3];
                fire_feedback.egg_charge[4] = fire_charge_level[4];

                fire_feedback.egg_has_byte = Weapon_Sta & 0x0F; // 只取低4位（1-4号蛋有无弹）

                // 反馈二级充电后的状态
                hostpc_send_fire_state(&fire_feedback);

                rt_enter_critical();
                hostpc_cmd.cmd_valid = RT_FALSE;
                rt_exit_critical();
            }
            // ========== 3. 点火 ==========
            else if (hostpc_cmd.cmd_code == CMD_DIANHUO && hostpc_cmd.cmd_valid == RT_TRUE && fire_mask != 0)
            {
                rt_exit_critical();
                for (egg_idx = 1; egg_idx <= 4; egg_idx++)
                {
                    if (fire_mask & (0x01 << (egg_idx - 1)))
                    {
                        rt_enter_critical();
                        has_egg = (Weapon_Sta & (0x01 << (egg_idx - 1))) ? 1 : 0;
                        if (!has_egg)
                        {
                            rt_exit_critical();
                            rt_kprintf("蛋ID:%d 无弹，禁止点火！\r\n", egg_idx);
                            continue;
                        }
                        if (fire_charge_level[egg_idx] == CHARGE_LEVEL_2)
                        {
                            rt_exit_critical();
                            fire_shot(egg_idx);
                            rt_kprintf("蛋ID:%d 点火成功！\r\n", egg_idx);
                        }
                        else
                        {
                            rt_exit_critical();
                            rt_kprintf("蛋ID:%d 有弹但未完成二级充电，禁止点火！\r\n", egg_idx);
                        }
                    }
                }

                fire_feedback.egg_charge[1] = fire_charge_level[1];
                fire_feedback.egg_charge[2] = fire_charge_level[2];
                fire_feedback.egg_charge[3] = fire_charge_level[3];
                fire_feedback.egg_charge[4] = fire_charge_level[4];

                fire_feedback.egg_has_byte = Weapon_Sta & 0x0F; // 只取低4位（1-4号蛋有无弹）

                // 反馈点火后的状态
                hostpc_send_fire_state(&fire_feedback);

                rt_enter_critical();
                hostpc_cmd.cmd_valid = RT_FALSE;
                rt_exit_critical();
            }
            else
            {
                rt_exit_critical();
            }
        }
        // ========== 未解锁：提示并置无效指令 ==========
        else
        {
            rt_exit_critical();
            rt_enter_critical();
            fire_mask = hostpc_cmd.fire_id;
            if (fire_mask != 0 && (hostpc_cmd.cmd_code == CMD_FIRE_1_DEC || hostpc_cmd.cmd_code == CMD_FIRE_2_DEC ||
                                   hostpc_cmd.cmd_code == CMD_DIANHUO || hostpc_cmd.cmd_code == CMD_QUERY_FIRE_STATE))
            {
                rt_exit_critical();
                rt_kprintf("火控未解锁，请先执行0解指令！\r\n");

                rt_enter_critical();
                hostpc_cmd.cmd_valid = RT_FALSE;
                rt_exit_critical();
            }
            else
            {
                rt_exit_critical();
            }
        }
    }
}
int fire_control_init(void)
{// 火控业务线程 优先级15
    rt_thread_init(	&fire_control_task_tcb,
					"fire_ctl",
					fire_control_task,
					RT_NULL,
					fire_control_task_stack,
					sizeof(fire_control_task_stack),
					15,
					10);
    rt_thread_startup(&fire_control_task_tcb);
    return RT_EOK;
}
INIT_APP_EXPORT(fire_control_init);
