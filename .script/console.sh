#!/usr/bin/env bash
# USART1 console; pass a serial device or use /dev/ttyUSB0.
# Exit picocom with Ctrl+A, Ctrl+X.
source "$(dirname "$0")/env.sh"
picocom -b 115200 "${1:-/dev/ttyUSB0}"
