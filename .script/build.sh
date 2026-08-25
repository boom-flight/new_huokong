#!/usr/bin/env bash
# Build the STM32F103C8 firmware; extra arguments are passed to SCons.
source "$(dirname "$0")/env.sh"
scons -j"$(nproc)" "$@"
"$(dirname "$0")/check-size.sh" rt-thread.elf
