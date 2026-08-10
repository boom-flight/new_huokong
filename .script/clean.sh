#!/usr/bin/env bash
# 清理编译产物
source "$(dirname "$0")/env.sh"
scons -c
