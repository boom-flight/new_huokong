#!/usr/bin/env bash
# 不经 VS Code 直接进入开发容器（首次自动安装工具链）；VS Code 用户直接 Reopen in Container
set -euo pipefail
cd "$(dirname "$0")/../.devcontainer"
docker compose up -d develop
docker compose exec develop bash -c \
    'command -v arm-none-eabi-gcc >/dev/null || bash tools/setup-ubuntu.sh; exec zsh'
