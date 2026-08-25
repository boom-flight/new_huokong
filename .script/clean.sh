#!/usr/bin/env bash
# Remove STM32F103C8 build outputs.
source "$(dirname "$0")/env.sh"
scons -c
