#!/usr/bin/env bash
# Ubuntu 环境一键配置：嵌入式工具链 + 烧录/调试/串口工具
# 用途：devcontainer postCreate（qzhhhi/rmcs-develop 容器）或裸 Ubuntu 机器；可重复执行
set -euo pipefail

sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    scons gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi \
    gdb-multiarch openocd bear picocom

# Ubuntu 没有 arm-none-eabi-gdb 包，做符号链接让 Cortex-Debug 与 .script/gdb.sh 直接可用
command -v arm-none-eabi-gdb >/dev/null || \
    sudo ln -sf "$(command -v gdb-multiarch)" /usr/local/bin/arm-none-eabi-gdb

# 串口权限（裸 Ubuntu 需注销重登生效；privileged 容器内通常无感）
sudo usermod -aG dialout "$USER" 2>/dev/null || true

# AI 工具链（配置/登录态由 docker-compose.yml 挂载宿主机目录提供）；缺 npm 则先装
command -v npm >/dev/null || \
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends nodejs npm
sudo npm install -g @anthropic-ai/claude-code @openai/codex opencode-ai

echo "setup-ubuntu 完成：$(arm-none-eabi-gcc --version | head -1)"
