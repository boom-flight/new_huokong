Import('RTT_ROOT')
Import('env')
from building import *

objs = []
scripts = [
    'src/modules/attitude/SConscript',
    'src/modules/devices/bmi088/SConscript',
    'src/modules/protocols/imu_telemetry/SConscript',
    'src/platform/devices/SConscript',
    'src/platform/time/SConscript',
    'src/platform/transport/SConscript',
    'src/kernel/imu/SConscript',
    'applications/SConscript',
    'src/kernel/telemetry/SConscript',
    'board/SConscript',
    'src/modules/timing/SConscript',
    'src/modules/transport/SConscript',
    'packages/SConscript',
]

# STM32F100xB || STM32F100xE || STM32F101x6
# STM32F101xB || STM32F101xE || STM32F101xG
# STM32F102x6 || STM32F102xB || STM32F103x6
# STM32F103xB || STM32F103xE || STM32F103xG
# STM32F105xC || STM32F107xC)
# You can select chips from the list above
env.Append(CPPDEFINES=['STM32F103xB'])

for script in scripts:
    objs.extend(SConscript(script))

Return('objs')
