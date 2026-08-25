#!/usr/bin/env bash
# 所有项目脚本共用的 GCC 环境。
set -euo pipefail
export PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export RTT_EXEC_PATH=/usr/bin
export RTT_CC=gcc
cd "$PROJECT_ROOT"
