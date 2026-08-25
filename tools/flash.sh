#!/usr/bin/env bash
# 构建固件并通过 ST-Link 烧录 STM32F103C8。
source "$(dirname "$0")/env.sh"
"$(dirname "$0")/build.sh"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program build/firmware/huokong.bin 0x08000000 verify reset exit"
