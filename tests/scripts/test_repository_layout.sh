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
grep -Fq -- '-T src/platform/board/stm32f103c8/linker_scripts/link.lds' rtconfig.py \
    || fail 'rtconfig.py has the wrong linker path'

if grep -Eq "applications/SConscript|board/SConscript|rsource ['\"]?(board/Kconfig|libraries/Kconfig)|-T board/linker_scripts/link[.]lds" \
    SConscript Kconfig rtconfig.py; then
    fail 'active root build/configuration still references an old path'
fi

for command in build test flash debug console check-size; do
    test -x "tools/$command.sh" || fail "missing tools/$command.sh"
done

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

outside_object=$(find . \
    \( -path './build' -o -path './.git' -o \
       -path './.superpowers/sdd' -o -path './.cache' -o \
       -path './.vendor-cache' -o -path './.cmsis' -o \
       -name __pycache__ \) -prune -o \
    -type f \( -name '*.o' -o -name '*.obj' \) -print | \
    awk 'NR == 1 { print; exit }')
test -z "$outside_object" \
    || fail "generated object is outside root build: $outside_object"
