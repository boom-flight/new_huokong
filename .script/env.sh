#!/usr/bin/env bash
# Shared GCC environment for all project scripts.
set -euo pipefail
export PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export RTT_EXEC_PATH=/usr/bin
export RTT_CC=gcc
cd "$PROJECT_ROOT"
