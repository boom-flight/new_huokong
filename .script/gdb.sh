#!/usr/bin/env bash
# Start OpenOCD for the STM32F103C8 and attach GDB.
source "$(dirname "$0")/env.sh"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg &
OCD_PID=$!
trap 'kill $OCD_PID 2>/dev/null' EXIT
sleep 1
# Prefer the cross GDB name used by Cortex-Debug.
GDB=$(command -v arm-none-eabi-gdb || command -v gdb-multiarch)
"$GDB" rt-thread.elf \
    -ex "target extended-remote localhost:3333" \
    -ex "monitor reset halt"
