#!/usr/bin/env sh
set -eu

fail() {
    printf 'repository layout check failed: %s\n' "$1" >&2
    exit 1
}

grep -q 'os.listdir' SConscript && fail 'root SConscript scans directories'
grep -q "TARGET = os.path.join('build', 'firmware', 'huokong.'" SConstruct \
    || fail 'firmware target is outside build/firmware'
grep -q 'SConsignFile' SConstruct || fail 'firmware SCons database is not redirected'
grep -q 'build/firmware/huokong.map' rtconfig.py || fail 'map path is not centralized'

test ! -d board || fail 'old board/ directory still exists'
test ! -d applications || fail 'old applications/ directory still exists'
test ! -e libraries/Kconfig || fail 'old libraries/Kconfig still exists'
test -f vendor/rt-thread/tools/building.py || fail 'missing vendored RT-Thread build entry point'
test ! -d rt-thread || fail 'root rt-thread/ compatibility directory still exists'
test -d vendor/cmsis-core || fail 'missing vendored CMSIS-Core snapshot'
test -d vendor/stm32f1-cmsis || fail 'missing vendored STM32F1 CMSIS snapshot'
test -d vendor/stm32f1-hal || fail 'missing vendored STM32F1 HAL snapshot'
stm32_driver_root=vendor/rt-thread-stm32-drivers
stm32_driver_patch=vendor/patches/rt-thread-stm32-drivers-exti15-10-owner.patch
test -f "$stm32_driver_root/SConscript" \
    || fail 'missing vendored RT-Thread STM32 driver snapshot'
test -f "$stm32_driver_patch" \
    || fail 'missing RT-Thread STM32 driver EXTI ownership patch'
test ! -d libraries/HAL_Drivers \
    || fail 'old libraries/HAL_Drivers directory still exists'
test -f vendor/manifest.md || fail 'missing vendor provenance manifest'
test ! -d packages || fail 'root packages/ directory still exists'

for document in \
    docs/architecture/overview.md \
    docs/architecture/dependency-rules.md \
    docs/development/build-test-debug.md \
    docs/protocols/imu-telemetry-v1.md \
    docs/requirements/firmware-behavior.md \
    docs/hardware/acceptance.md \
    docs/hardware/source/README.md; do
    test -s "$document" || fail "missing active document: $document"
done
grep -Fq '待上板验证' docs/hardware/acceptance.md \
    || fail 'hardware acceptance status is not pending board validation'

test -f src/app/main.c || fail 'missing src/app/main.c'
test -f src/app/SConscript || fail 'missing src/app/SConscript'
board_root=src/platform/board/stm32f103c8
test -f "$board_root/SConscript" || fail 'missing board SConscript'
test -f "$board_root/Kconfig" || fail 'missing board Kconfig'
test -f "$board_root/soc/Kconfig" || fail 'missing board SoC Kconfig'
test -f "$board_root/linker_scripts/link.lds" || fail 'missing board linker script'

grep -Fq "'$board_root/SConscript'" SConscript \
    || fail 'root SConscript does not include the board manifest'
grep -Fq "'src/app/SConscript'" SConscript \
    || fail 'root SConscript does not include the app manifest'
grep -Fq 'BSP_DIR := src/platform/board/stm32f103c8' Kconfig \
    || fail 'root Kconfig has the wrong BSP_DIR'
grep -Fq 'rsource "$(BSP_DIR)/soc/Kconfig"' Kconfig \
    || fail 'root Kconfig does not include the board SoC Kconfig'
grep -Fq 'rsource "$(BSP_DIR)/Kconfig"' Kconfig \
    || fail 'root Kconfig does not include the board Kconfig'
grep -Fq 'source "vendor/rt-thread-stm32-drivers/drivers/Kconfig"' \
    "$board_root/Kconfig" \
    || fail 'board Kconfig does not include the vendored STM32 drivers'
grep -Fq 'CONFIG_BSP_GPIO_EXTI15_10_EXTERNAL=y' .config \
    || fail 'external EXTI15_10 ownership is not enabled'
stm32_gpio_driver="$stm32_driver_root/drivers/drv_gpio.c"
guarded_exti_handler=$(awk '
    /^#ifndef BSP_GPIO_EXTI15_10_EXTERNAL$/ { in_guard = 1 }
    in_guard { print }
    in_guard && /^#endif$/ { exit }
' "$stm32_gpio_driver")
for required_line in \
    '#ifndef BSP_GPIO_EXTI15_10_EXTERNAL' \
    'void EXTI15_10_IRQHandler(void)' \
    'HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);' \
    'HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);' \
    'HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);' \
    'HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);' \
    'HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);' \
    'HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);' \
    '#endif'; do
    printf '%s\n' "$guarded_exti_handler" | grep -Fq "$required_line" \
        || fail "STM32 GPIO driver guard is incomplete: $required_line"
done
grep -Fq -- '-T src/platform/board/stm32f103c8/linker_scripts/link.lds' rtconfig.py \
    || fail 'rtconfig.py has the wrong linker path'

if grep -Eq "applications/SConscript|board/SConscript|rsource ['\"]?(board/Kconfig|libraries/Kconfig)|-T board/linker_scripts/link[.]lds" \
    SConscript Kconfig rtconfig.py; then
    fail 'active root build/configuration still references an old path'
fi

for command in build test flash debug console check-size; do
    test -x "tools/$command.sh" || fail "missing tools/$command.sh"
done

test -x tests/scripts/test_check_size.sh \
    || fail 'size test is not under tests/scripts'
test -x tests/scripts/test_test_runner.sh \
    || fail 'test runner self-test is not under tests/scripts'
test -x tests/scripts/test_layout_fail_closed.sh \
    || fail 'layout fail-closed self-test is missing'

if git grep -nE '\.script/|rt-thread\.elf|rtthread\.bin|rt-thread\.map|tests/build' -- \
    README.md .vscode .devcontainer docs/architecture docs/development \
    docs/hardware docs/protocols docs/requirements; then
    fail 'active entry points reference a retired path or artifact'
fi

test ! -d .script || fail '.script compatibility directory still exists'
test ! -d tests/build || fail 'host test artifacts are outside root build'

if ! scons_tree=$(scons -n -Q --tree=all 2>&1); then
    fail 'cannot inspect planned firmware targets'
fi
planned_outside_object=$(printf '%s\n' "$scons_tree" | awk '
    /[+]-.*\.(o|obj)$/ {
        path = $0
        sub(/^.*[+]-/, "", path)
        if (path !~ /^build\//) {
            print path
            exit
        }
    }
')
test -z "$planned_outside_object" \
    || fail "planned object is outside root build: $planned_outside_object"

if ! object_files=$(find . \
    \( -path './build' -o -path './.git' -o \
       -path './.superpowers/sdd' -o -path './.cache' -o \
       -path './.vendor-cache' -o -path './.cmsis' -o \
       -name __pycache__ \) -prune -o \
     -type f \( -name '*.o' -o -name '*.obj' \) -print); then
    fail 'cannot scan generated objects'
fi
outside_object=$(printf '%s\n' "$object_files" | \
    awk 'NR == 1 { print; exit }')
test -z "$outside_object" \
    || fail "generated object is outside root build: $outside_object"
