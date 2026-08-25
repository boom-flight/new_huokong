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

outside_object=$(find algorithm applications board drivers libraries packages \
    protocol rt-thread -type f \( -name '*.o' -o -name '*.obj' \) -print -quit)
test -z "$outside_object" \
    || fail "generated object is outside root build: $outside_object"
