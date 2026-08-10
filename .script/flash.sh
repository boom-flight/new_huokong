#!/usr/bin/env bash
# 编译并通过 ST-Link 烧录到 0x08000000
source "$(dirname "$0")/env.sh"
"$(dirname "$0")/build.sh"
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
        -c "program rtthread.bin 0x08000000 verify reset exit"
