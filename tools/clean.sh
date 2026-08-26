#!/usr/bin/env bash
# 清理 STM32F103C8 构建产物。
source "$(dirname "$0")/env.sh"
scons -c
rm -rf build/scons
rm -rf build/kernel
