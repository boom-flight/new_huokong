#!/usr/bin/env bash
# 构建 STM32F103C8 固件，额外参数原样传给 SCons。
source "$(dirname "$0")/env.sh"
scons -j"$(nproc)" "$@"
"$(dirname "$0")/check-size.sh" build/scons/firmware/huokong.elf
python3 tests/scripts/test_link_owners.py \
    build/scons/firmware/huokong.elf build/scons/firmware/huokong.map
rm -rf build/kernel
