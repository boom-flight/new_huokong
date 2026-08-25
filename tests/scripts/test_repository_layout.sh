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
