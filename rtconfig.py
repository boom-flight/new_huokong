import os

ARCH = 'arm'
CPU = 'cortex-m3'
CROSS_TOOL = 'gcc'
BSP_LIBRARY_TYPE = None

PLATFORM = 'gcc'
EXEC_PATH = os.getenv('RTT_EXEC_PATH', '/usr/bin')
PREFIX = 'arm-none-eabi-'
CC = PREFIX + 'gcc'
AS = PREFIX + 'gcc'
AR = PREFIX + 'ar'
CXX = PREFIX + 'g++'
LINK = PREFIX + 'gcc'
TARGET_EXT = 'elf'
SIZE = PREFIX + 'size'
OBJDUMP = PREFIX + 'objdump'
OBJCPY = PREFIX + 'objcopy'

DEVICE = ' -mcpu=cortex-m3 -mthumb -ffunction-sections -fdata-sections'
CFLAGS = DEVICE + ' -Dgcc -std=gnu11'
AFLAGS = ' -c' + DEVICE + ' -x assembler-with-cpp'
LFLAGS = DEVICE + ' -Wl,--gc-sections,-Map=build/firmware/huokong.map,-cref,-u,Reset_Handler -T board/linker_scripts/link.lds'

CFLAGS += ' -O2 -gdwarf-2 -g'
AFLAGS += ' -gdwarf-2'
CXXFLAGS = CFLAGS
CPATH = ''
LPATH = ''
POST_ACTION = SIZE + ' $TARGET\n'
