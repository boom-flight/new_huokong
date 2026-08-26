/**
 * @file board.h
 * @brief STM32F103C8 板级内存布局和系统时钟接口。
 * @note 板级适配使用 64 KiB Flash 和 20 KiB SRAM 的内存范围。
 */

#ifndef BOARD_H
#define BOARD_H

#include <stm32f1xx.h>

/** @brief STM32 Flash 起始地址。 */
#define STM32_FLASH_START_ADRESS ((uint32_t)0x08000000)
/** @brief STM32 Flash 容量，单位为字节。 */
#define STM32_FLASH_SIZE (64 * 1024)
/** @brief STM32 Flash 末尾地址（不包含该地址）。 */
#define STM32_FLASH_END_ADDRESS (STM32_FLASH_START_ADRESS + STM32_FLASH_SIZE)

/** @brief STM32 SRAM 容量，单位为 KiB。 */
#define STM32_SRAM_SIZE 20
/** @brief STM32 SRAM 末尾地址。 */
#define STM32_SRAM_END 0x20005000

/** @brief 链接器导出的 BSS 末尾符号，用于确定堆起始位置。 */
extern int __bss_end;
/** @brief RT-Thread 堆起始地址。 */
#define HEAP_BEGIN ((void *)&__bss_end)
/** @brief RT-Thread 堆末尾地址。 */
#define HEAP_END ((void *)0x20005000)

/**
 * @brief 配置 STM32F103C8 系统时钟。
 * @note 当前实现使用 8 MHz HSE，经 PLL 倍频至 72 MHz 系统时钟。
 */
void SystemClock_Config(void);

#endif
