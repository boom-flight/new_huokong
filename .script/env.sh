#!/usr/bin/env bash
# 公共环境：所有脚本 source 本文件
set -euo pipefail
export PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export RTT_EXEC_PATH=/usr/bin
export RTT_CC=gcc
cd "$PROJECT_ROOT"
