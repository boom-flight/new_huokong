#!/usr/bin/env bash
# RT-Thread 内核/BSP 配置界面
source "$(dirname "$0")/env.sh"
scons --menuconfig
