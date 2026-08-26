#!/usr/bin/env bash
# 清理、构建并运行严格检查的本机测试程序。
source "$(dirname "$0")/env.sh"

if (( $# > 1 )); then
    printf 'usage: %s [executable-name]\n' "$0" >&2
    exit 2
fi

scons -f tests/SConstruct -c
scons -f tests/SConstruct -j"$(nproc)"

if (( $# == 1 )); then
    executable="build/host-tests/$1"
    if [[ "$1" == */* || ! -f "$executable" || ! -x "$executable" ]]; then
        printf 'test executable not found: %s\n' "$1" >&2
        exit 2
    fi
    "$executable"
    exit
fi

shopt -s nullglob
discovery_dir=${TEST_DISCOVERY_DIR:-build/host-tests}
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

sh tests/scripts/test_check_size.sh
if [[ ${SKIP_TEST_RUNNER_SELF_TEST:-0} != 1 ]]; then
    SKIP_TEST_RUNNER_SELF_TEST=1 sh tests/scripts/test_test_runner.sh
fi
sh tests/scripts/test_repository_layout.sh
