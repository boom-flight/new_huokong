#!/usr/bin/env bash
# 重新构建 STM32F103C8，并为 clangd 生成编译数据库。
source "$(dirname "$0")/env.sh"
"$(dirname "$0")/build.sh" --cdb
