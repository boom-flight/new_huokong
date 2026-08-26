#!/usr/bin/env bash
# 构建固件并通过自动识别的 SWD 探针烧录 STM32F103C8。
source "$(dirname "$0")/env.sh"
case "${1:-}" in
    *.axf) printf 'flash.sh accepts only the SCons BIN, not a Keil AXF\n' >&2; exit 2 ;;
esac
"$(dirname "$0")/build.sh"
"$(dirname "$0")/openocd.sh" -f target/stm32f1x.cfg \
    -c "program build/scons/firmware/huokong.bin 0x08000000 verify reset exit"
