#!/usr/bin/env bash
# 构建 STM32F103C8 固件，额外参数原样传给 SCons。
source "$(dirname "$0")/env.sh"
scons -j"$(nproc)" "$@"
"$(dirname "$0")/check-size.sh" build/firmware/huokong.elf
