#!/usr/bin/env bash
# 命令行调试：后台起 OpenOCD，前台 arm-none-eabi-gdb 连接（VS Code F5 的命令行替代）
source "$(dirname "$0")/env.sh"
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg &
OCD_PID=$!
trap 'kill $OCD_PID 2>/dev/null' EXIT
sleep 1
# Arch: arm-none-eabi-gdb；Ubuntu/Debian: gdb-multiarch
GDB=$(command -v arm-none-eabi-gdb || command -v gdb-multiarch)
"$GDB" rt-thread.elf \
    -ex "target extended-remote localhost:3333" \
    -ex "monitor reset halt"
