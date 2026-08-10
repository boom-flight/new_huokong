#!/usr/bin/env bash
# msh 串口控制台（UART6），可传串口设备参数，默认 /dev/ttyUSB0
# 退出：Ctrl+A Ctrl+X
source "$(dirname "$0")/env.sh"
picocom -b 115200 "${1:-/dev/ttyUSB0}"
