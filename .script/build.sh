#!/usr/bin/env bash
# 编译固件，产物 rt-thread.elf + rtthread.bin；额外参数透传给 scons
source "$(dirname "$0")/env.sh"
scons -j"$(nproc)" "$@"
