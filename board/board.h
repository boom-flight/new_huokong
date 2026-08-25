#ifndef BOARD_H
#define BOARD_H

#include <stm32f1xx.h>

#define STM32_FLASH_START_ADRESS ((uint32_t)0x08000000)
#define STM32_FLASH_SIZE (64 * 1024)
#define STM32_FLASH_END_ADDRESS (STM32_FLASH_START_ADRESS + STM32_FLASH_SIZE)

#define STM32_SRAM_SIZE 20
#define STM32_SRAM_END 0x20005000

extern int __bss_end;
#define HEAP_BEGIN ((void *)&__bss_end)
#define HEAP_END ((void *)0x20005000)

void SystemClock_Config(void);

#endif
