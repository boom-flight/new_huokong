#!/usr/bin/env bash
# Rebuild STM32F103C8 and regenerate compile_commands.json for clangd.
source "$(dirname "$0")/env.sh"
"$(dirname "$0")/clean.sh"
bear -- "$(dirname "$0")/build.sh"
