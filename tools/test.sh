#!/usr/bin/env bash
# 运行主机测试 fixture，构建固件并运行严格检查。
source "$(dirname "$0")/env.sh"

if (( $# > 1 )); then
    printf 'usage: %s [executable-name]\n' "$0" >&2
    exit 2
fi

scons -f tests/SConstruct -c
scons -f tests/SConstruct -j"$(nproc)"

if (( $# == 1 )); then
    executable="build/scons/host-tests/$1"
    if [[ "$1" == */* || ! -f "$executable" || ! -x "$executable" ]]; then
        printf 'test executable not found: %s\n' "$1" >&2
        exit 2
    fi
    "$executable"
    exit
fi

shopt -s nullglob
discovery_dir=${TEST_DISCOVERY_DIR:-build/scons/host-tests}
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
firmware_build_script=${TEST_FIRMWARE_BUILD_SCRIPT:-./tools/build.sh}
"$firmware_build_script"
scons --cdb
python3 tests/scripts/test_dependency_boundaries.py
firmware_elf=build/scons/firmware/huokong.elf
firmware_map=build/scons/firmware/huokong.map
if [[ ! -f "$firmware_elf" || ! -f "$firmware_map" ]]; then
    printf 'link owner check requires existing ELF and Map files: %s, %s\n' \
        "$firmware_elf" "$firmware_map" >&2
    exit 1
fi
python3 tests/scripts/test_link_owners.py "$firmware_elf" "$firmware_map"
if [[ ${SKIP_TEST_RUNNER_SELF_TEST:-0} != 1 ]]; then
    SKIP_TEST_RUNNER_SELF_TEST=1 sh tests/scripts/test_test_runner.sh
fi
sh tests/scripts/test_repository_layout.sh
sh tests/scripts/test_no_imu_thread_logging.sh
sh tests/scripts/test_imu_platform_boundary.sh
sh tests/scripts/test_service_ownership.sh
sh tests/scripts/test_telemetry_state_ownership.sh
sh tools/keil-project-check.sh
"$PROJECT_ROOT/tests/test_debug_probe.sh"
