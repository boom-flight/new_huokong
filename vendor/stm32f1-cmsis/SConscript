from building import *
import os

# Import environment variables
Import('env')

# Get the current working directory
cwd = GetCurrentDir()

# Initialize include paths and source files list
path = [os.path.join(cwd, 'Include')]
src = [os.path.join(cwd, 'Source', 'Templates', 'system_stm32f1xx.c')]

# Map microcontroller units (MCUs) to their corresponding startup files
mcu_startup_files = {
    'STM32F100xB': 'startup_stm32f100xb.s',
    'STM32F100xE': 'startup_stm32f100xe.s',
    'STM32F101x6': 'startup_stm32f101x6.s',
    'STM32F101xB': 'startup_stm32f101xb.s',
    'STM32F101xE': 'startup_stm32f101xe.s',
    'STM32F101xG': 'startup_stm32f101xg.s',
    'STM32F102x6': 'startup_stm32f102x6.s',
    'STM32F102xB': 'startup_stm32f102xb.s',
    'STM32F103x6': 'startup_stm32f103x6.s',
    'STM32F103xB': 'startup_stm32f103xb.s',
    'STM32F103xE': 'startup_stm32f103xe.s',
    'STM32F103xG': 'startup_stm32f103xg.s',
    'STM32F105xC': 'startup_stm32f105xc.s',
    'STM32F107xC': 'startup_stm32f107xc.s',
}

# Check each defined MCU, match the platform and append the appropriate startup file
cpp_defines_tuple = env.get('CPPDEFINES', [])
cpp_defines_list = [item[0] if isinstance(item, tuple) else item for item in cpp_defines_tuple]
for mcu, startup_file in mcu_startup_files.items():
    if mcu in cpp_defines_list:
        if rtconfig.PLATFORM in ['gcc', 'llvm-arm']:
            src += [os.path.join(cwd, 'Source', 'Templates', 'gcc', startup_file)]
        elif rtconfig.PLATFORM in ['armcc', 'armclang']:
            src += [os.path.join(cwd, 'Source', 'Templates', 'arm', startup_file)]
        elif rtconfig.PLATFORM in ['iccarm']:
            src += [os.path.join(cwd, 'Source', 'Templates', 'iar', startup_file)]
        break

# Define the build group
group = DefineGroup('STM32F1-CMSIS', src, depend=['PKG_USING_STM32F1_CMSIS_DRIVER'], CPPPATH=path)

# Return the build group
Return('group')
