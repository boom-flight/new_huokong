#!/usr/bin/env bash
# 为 STM32F103C8 启动 OpenOCD 并连接 GDB。
source "$(dirname "$0")/env.sh"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg &
OCD_PID=$!
trap 'kill $OCD_PID 2>/dev/null' EXIT
sleep 1
# 优先使用 Cortex-Debug 采用的交叉 GDB 名称。
GDB=$(command -v arm-none-eabi-gdb || command -v gdb-multiarch)
"$GDB" build/firmware/huokong.elf \
    -ex "target extended-remote localhost:3333" \
    -ex "monitor reset halt"
