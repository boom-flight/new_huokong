#!/usr/bin/env bash
# 自动发现 Foxglove USART1 串口并启动 bridge。
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

exec python3 "$PROJECT_ROOT/tools/foxglove_debug_bridge.py" \
    --device "${FOXGLOVE_DEVICE:-auto}" "$@"
