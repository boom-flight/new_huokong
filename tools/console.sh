#!/usr/bin/env bash
# USART1 控制台；可传入串口设备，默认为 /dev/ttyUSB0。
# 使用 Ctrl+A、Ctrl+X 退出 picocom。
source "$(dirname "$0")/env.sh"
picocom -b 115200 "${1:-/dev/ttyUSB0}"
