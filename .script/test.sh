#!/usr/bin/env bash
# Clean, build, and run the strict native test executables.
source "$(dirname "$0")/env.sh"

if (( $# > 1 )); then
    printf 'usage: %s [executable-name]\n' "$0" >&2
    exit 2
fi

scons -C tests -c
rm -rf tests/build
scons -C tests -j"$(nproc)"

if (( $# == 1 )); then
    executable="tests/build/$1"
    if [[ "$1" == */* || ! -f "$executable" || ! -x "$executable" ]]; then
        printf 'test executable not found: %s\n' "$1" >&2
        exit 2
    fi
    "$executable"
    exit
fi

shopt -s nullglob
discovery_dir=${TEST_DISCOVERY_DIR:-tests/build}
candidates=("$discovery_dir"/test_*)
executables=()
for candidate in "${candidates[@]}"; do
    if [[ -f "$candidate" && -x "$candidate" ]]; then
        executables+=("$candidate")
    fi
done
if (( ${#executables[@]} == 0 )); then
    printf 'no C test executables found\n' >&2
    exit 1
fi
mapfile -t executables < <(printf '%s\n' "${executables[@]}" | LC_ALL=C sort)
for executable in "${executables[@]}"; do
    "$executable"
done
sh tests/test_check_size.sh
