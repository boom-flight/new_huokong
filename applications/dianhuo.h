#ifndef __DIANHUO_H
#define __DIANHUO_H

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "stm32f4xx_hal.h"

#define Fire_1_Pin            GPIO_PIN_2
#define Fire_1_GPIO_Port      GPIOA
#define Fire_2_Pin            GPIO_PIN_0
#define Fire_2_GPIO_Port      GPIOB
#define Fire_3_Pin            GPIO_PIN_12
#define Fire_3_GPIO_Port      GPIOE
#define Fire_4_Pin            GPIO_PIN_9
#define Fire_4_GPIO_Port      GPIOD
#define Charge_V_Pin          GPIO_PIN_10
#define Charge_V_GPIO_Port    GPIOD
#define Charge_1_Pin          GPIO_PIN_3
#define Charge_1_GPIO_Port    GPIOA
#define Charge_2_Pin          GPIO_PIN_5
#define Charge_2_GPIO_Port    GPIOA
#define Charge_3_Pin          GPIO_PIN_10
#define Charge_3_GPIO_Port    GPIOE
#define Charge_4_Pin          GPIO_PIN_8
#define Charge_4_GPIO_Port    GPIOD
#define Discharge_Pin         GPIO_PIN_12
#define Discharge_GPIO_Port   GPIOB
#define Check_V_Pin           GPIO_PIN_5
#define Check_V_GPIO_Port     GPIOC
#define Weapon1_STA_Pin       GPIO_PIN_4
#define Weapon1_STA_GPIO_Port GPIOA
#define Weapon2_STA_Pin       GPIO_PIN_9
#define Weapon2_STA_GPIO_Port GPIOE
#define Weapon3_STA_Pin       GPIO_PIN_14
#define Weapon3_STA_GPIO_Port GPIOE
#define Weapon4_STA_Pin       GPIO_PIN_11
#define Weapon4_STA_GPIO_Port GPIOB
#define Limit_L_Pin           GPIO_PIN_1
#define Limit_L_GPIO_Port     GPIOC
#define Limit_R_Pin           GPIO_PIN_3
#define Limit_R_GPIO_Port     GPIOC
// 引脚操作宏
#define Fire_1_OFF    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET)
#define Fire_1_ON     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET)
#define Fire_2_OFF    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)
#define Fire_2_ON     HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET)
#define Fire_3_OFF    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_12, GPIO_PIN_RESET)
#define Fire_3_ON     HAL_GPIO_WritePin(GPIOE, GPIO_PIN_12, GPIO_PIN_SET)
#define Fire_4_OFF    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_9, GPIO_PIN_RESET)
#define Fire_4_ON     HAL_GPIO_WritePin(GPIOD, GPIO_PIN_9, GPIO_PIN_SET)
#define Charge_V_OFF  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_RESET)
#define Charge_V_ON   HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_SET)
#define Charge_1_OFF  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET)
#define Charge_1_ON   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET)
#define Charge_2_OFF  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET)
#define Charge_2_ON   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET)
#define Charge_3_OFF  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_RESET)
#define Charge_3_ON   HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_SET)
#define Charge_4_OFF  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8, GPIO_PIN_RESET)
#define Charge_4_ON   HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8, GPIO_PIN_SET)
#define Discharge_OFF HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET)
#define Discharge_ON  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET)
#define Check_V_OFF   HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET)
#define Check_V_ON    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET)
#define Weapon1_STA   HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4)
#define Weapon2_STA   HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_9)
#define Weapon3_STA   HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_14)
#define Weapon4_STA   HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11)
#define Limit_L   HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1)
#define Limit_R   HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_3)
/************************ 协议指令码定义 严格对齐指令表 ************************/
#define CMD_FIRE_0_DEC               	0x01 // 0解指令 [火控总解锁] 必须先执行
#define CMD_FIRE_1_DEC               	0x02 // 一解指令(指定蛋ID)[一级充电]
#define CMD_FIRE_2_DEC              	0x03 // 二解指令(指定蛋ID)[二级充电]
#define CMD_DIANHUO                  	0x04 // 点火指令(指定蛋ID)[精准点火]
#define CMD_QUERY_FIRE_STATE         	0x05 // 查询火控状态指令
#define CMD_FIRE_DISCHARGE		    	0x08 // 泄放
/************************ 火控蛋ID定义 ************************/
#define FIRE_ID_1 0x01
#define FIRE_ID_2 0x02
#define FIRE_ID_3 0x03
#define FIRE_ID_4 0x04
/************************ 新增：火控充电层级状态宏 核心互锁 ************************/
#define FIRE_UNLOCK_NONE  0x00 // 火控未解锁(默认状态，所有火控指令无效)
#define FIRE_UNLOCK_OK    0x01 // 0解完成，火控总解锁

#define CHARGE_LEVEL_NONE 0x00 // 无充电
#define CHARGE_LEVEL_1    0x01 // 一级充电完成(1解)
#define CHARGE_LEVEL_2    0x02 // 二级充电完成(2解)

/************************ 火控状态反馈结构体 ************************/
typedef struct
{
    uint8_t fire_unlock_state; // 字节0：火控总解锁状态（0=未解锁/1=已解锁）
    uint8_t egg_has_byte;      // 字节1：有无弹状态（低4位）→ bit0=蛋1, bit1=蛋2, bit2=蛋3, bit3=蛋4（1=有弹/0=无弹）
    uint8_t egg_charge[5];     // 字节2-5：1-4号弹充电层级 → [1]=蛋1, [2]=蛋2, [3]=蛋3, [4]=蛋4（0=未充电/1=一级/2=二级）
} fire_state_feedback_t;

extern fire_state_feedback_t fire_feedback; // 火控状态反馈结构体

// 函数声明
void check_control(void);
void Singer_fire(void);
void Double_fire(void);
void All_fire(void);
void Charge_control(void);
void Discharge_control(void);
void fire_control_task(void *parameter);

#endif
