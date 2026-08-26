Import('RTT_ROOT')
Import('env')
from building import *

objs = []
board_script = 'src/platform/board/stm32f103c8/SConscript'
# Apply board compile settings before platform SConscripts clone the environment.
board_group = SConscript(board_script)
scripts = [
    'src/modules/attitude/SConscript',
    'src/modules/devices/bmi088/SConscript',
    'src/modules/protocols/imu_telemetry/SConscript',
    'src/platform/devices/SConscript',
    'src/platform/time/SConscript',
    'src/platform/transport/SConscript',
    'src/kernel/logging/SConscript',
    'src/kernel/imu/SConscript',
    'src/app/SConscript',
    'src/kernel/telemetry/SConscript',
    board_script,
    'src/modules/timing/SConscript',
    'src/modules/transport/SConscript',
    'vendor/cmsis-core/SConscript',
    'vendor/stm32f1-cmsis/SConscript',
    'vendor/stm32f1-hal/SConscript',
]

for script in scripts:
    if script == board_script:
        objs.extend(board_group)
    else:
        objs.extend(SConscript(script))

Return('objs')
