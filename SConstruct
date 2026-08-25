import os
import sys

import rtconfig

RTT_ROOT = os.getenv('RTT_ROOT', os.path.join(os.getcwd(), 'rt-thread'))
sys.path.append(os.path.join(RTT_ROOT, 'tools'))
from building import *


def bsp_pkg_check():
    required = [
        'packages/CMSIS-Core-latest',
        'packages/stm32f1_cmsis_driver-latest',
        'packages/stm32f1_hal_driver-latest',
    ]
    if not all(os.path.isdir(path) for path in required):
        print('Vendored dependency package is missing; restore the pinned source snapshot.')
        Exit(1)


RegisterPreBuildingAction(bsp_pkg_check)

TARGET = 'rt-thread.' + rtconfig.TARGET_EXT
DefaultEnvironment(tools=[])
env = Environment(
    tools=['mingw'],
    AS=rtconfig.AS,
    ASFLAGS=rtconfig.AFLAGS,
    CC=rtconfig.CC,
    CFLAGS=rtconfig.CFLAGS,
    AR=rtconfig.AR,
    ARFLAGS='-rc',
    CXX=rtconfig.CXX,
    CXXFLAGS=rtconfig.CXXFLAGS,
    LINK=rtconfig.LINK,
    LINKFLAGS=rtconfig.LFLAGS,
)
env.PrependENVPath('PATH', rtconfig.EXEC_PATH)
env.AppendUnique(CPPPATH=['algorithm', 'drivers', 'protocol'])
env.AppendUnique(LIBS=['m'])

Export('env')
Export('RTT_ROOT')
Export('rtconfig')

objs = PrepareBuilding(env, RTT_ROOT, has_libcpu=False)
objs.extend(SConscript('libraries/HAL_Drivers/SConscript',
                       variant_dir='build/libraries/HAL_Drivers', duplicate=0))

direct_hal = [
    'packages/stm32f1_hal_driver-latest/Src/stm32f1xx_hal_spi.c',
    'packages/stm32f1_hal_driver-latest/Src/stm32f1xx_hal_tim.c',
    'packages/stm32f1_hal_driver-latest/Src/stm32f1xx_hal_tim_ex.c',
]
objs.extend(DefineGroup('Direct HAL', direct_hal, depend=[''],
    CPPPATH=['packages/stm32f1_hal_driver-latest/Inc'],
    CPPDEFINES=['USE_HAL_DRIVER']))

DoBuilding(TARGET, objs)
