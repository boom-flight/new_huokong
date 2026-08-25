#!/usr/bin/env bash
# Build and flash the STM32F103C8 through ST-Link.
source "$(dirname "$0")/env.sh"
"$(dirname "$0")/build.sh"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program rtthread.bin 0x08000000 verify reset exit"
