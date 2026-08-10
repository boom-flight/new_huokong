#!/usr/bin/env bash
# 全量重编 + 生成 compile_commands.json（clangd 索引用）
source "$(dirname "$0")/env.sh"
scons -c
bear -- scons -j"$(nproc)"
